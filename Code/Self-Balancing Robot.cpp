#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>

Adafruit_MPU6050 mpu;


WebServer server(80);


const char* ssid = "RobotESP32";
const char* password = "12345678";

// ================= MOTOR PINS =================
#define PWMA 25
#define AIN1 26
#define AIN2 27

#define PWMB 14
#define BIN1 12
#define BIN2 13

// ================= ENCODER PINS =================
#define ENCA_PHASE_A 34
#define ENCA_PHASE_B 35
#define ENCB_PHASE_A 32
#define ENCB_PHASE_B 33

volatile long encoderLeftCount = 0;
volatile long encoderRightCount = 0;

// ================= TUNING & CONFIGURATION =================
const bool INVERT_MOTOR_DIRECTION = false; 

float Kp_angle = 33.0;  
float Ki_angle = 0.0;  
float Kd_angle = 0.7;   

float Kp_position = 0.05;  
float Kd_position = 0.1;   

float angleOffset = -1.0; 
float gyroOffset = 0.0;

// ================= RTOS SHARED VARIABLES =================
volatile float moveOffset = 0.0;     
volatile float steeringOffset = 0.0; 

// ================= FILTERS & TIMING =================
float angle = 0.0;
float angleIntegral = 0.0;

float lastAngle = 0;
float angleVelocity = 0;

float instabilityScore = 0;

TaskHandle_t BalanceTaskHandle = NULL;
TaskHandle_t WebServerTaskHandle = NULL;

void vBalanceTask(void *pvParameters);
void vWebServerTask(void *pvParameters);

// =====================================================
// ENCODER INTERRUPTS (ISR)
// =====================================================
void IRAM_ATTR readEncoderLeft() {
    if (digitalRead(ENCA_PHASE_B) > 0) encoderLeftCount++;
    else encoderLeftCount--;
}

void IRAM_ATTR readEncoderRight() {
    if (digitalRead(ENCB_PHASE_B) > 0) encoderRightCount--;
    else encoderRightCount++;
}
void autoTunePID()
{
    if (instabilityScore > 30) {
        // too unstable → reduce Kp
        Kp_angle *= 0.98;
    }

    if (abs(angleVelocity) > 80) {
        // oscillation → increase damping
        Kd_angle += 0.05;
    }

    if (abs(angle) < 2 && abs(angleVelocity) < 5) {
        // stable → slightly increase Kp for strength
        Kp_angle += 0.02;
    }

    // limits
    Kp_angle = constrain(Kp_angle, 5, 80);
    Ki_angle = constrain(Ki_angle, 0, 30);
    Kd_angle = constrain(Kd_angle, 0, 10);
}

// =====================================================
// MOTOR CONTROL
// =====================================================
void setMotor(int speedLeft, int speedRight)
{
    if (INVERT_MOTOR_DIRECTION) {
        speedLeft = -speedLeft;
        speedRight = -speedRight;
    }

    if (speedLeft > 0) speedLeft += 35; 
    else if (speedLeft < 0) speedLeft -= 35;

    if (speedRight > 0) speedRight += 35; 
    else if (speedRight < 0) speedRight -= 35;

    speedLeft = constrain(speedLeft, -255, 255);
    speedRight = constrain(speedRight, -255, 255);

    if (speedLeft > 0) {
        digitalWrite(AIN1, LOW); digitalWrite(AIN2, HIGH);
        analogWrite(PWMA, speedLeft);
    } else if (speedLeft < 0) {
        digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
        analogWrite(PWMA, -speedLeft);
    } else {
        digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
        analogWrite(PWMA, 0);
    }

    if (speedRight > 0) {
        digitalWrite(BIN1, LOW); digitalWrite(BIN2, HIGH);
        analogWrite(PWMB, speedRight);
    } else if (speedRight < 0) {
        digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
        analogWrite(PWMB, -speedRight);
    } else {
        digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW);
        analogWrite(PWMB, 0);
    }
}

void calibrateGyro()
{
    float sum = 0;
    sensors_event_t a, g, temp;
    Serial.println("Calibrating gyro... Keep robot perfectly still!");
    for (int i = 0; i < 300; i++)
    {
        mpu.getEvent(&a, &g, &temp);
        sum += g.gyro.x * 180.0 / PI;
        delay(2);
    }
    gyroOffset = -(sum / 300.0);
}

// =====================================================
// HTML 
// =====================================================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Robot Control</title>

<style>
body{
    margin:0;
    font-family:Arial;
    background:#0f1115;
    color:white;
    text-align:center;
}

h2{
    color:#00ffe1;
    margin:15px;
}

.box{
    margin:15px;
    padding:15px;
    background:#161a22;
    border-radius:15px;
    box-shadow:0 0 10px #000;
}

.grid{
    display:grid;
    grid-template-columns:70px 70px 70px;
    gap:10px;
    justify-content:center;
    margin-top:10px;
}

.btn{
    height:60px;
    border:none;
    border-radius:12px;
    font-size:22px;
    font-weight:bold;
    background:#2a2f3a;
    color:white;
    transition:0.1s;
}

.btn:active{
    transform:scale(0.95);
    background:#00ffe1;
    color:black;
}

.stop{ background:#ff3b3b; }

.empty{
    background:transparent;
    box-shadow:none;
}

.slider{
    width:80%;
}

.value{
    color:#00ffe1;
    font-weight:bold;
}
</style>

</head>

<body>

<h2>🤖 ESP32 Robot Control</h2>

<div class="box">
    <h2>Movement</h2>
    <button class="btn"
    onpointerdown="cmd('TL')"
    onpointerup="cmd('S')">⟲</button>

    <button class="btn"
    onpointerdown="cmd('TR')"
    onpointerup="cmd('S')">⟳</button>
    <div class="grid">
        <button class="btn empty"></button>
        <button class="btn" onpointerdown="cmd('F')" onpointerup="cmd('S')">↑</button>
        <button class="btn empty"></button>

        <button class="btn" onpointerdown="cmd('L')" onpointerup="cmd('S')">←</button>
        <button class="btn stop" onclick="cmd('S')">■</button>
        <button class="btn" onpointerdown="cmd('R')" onpointerup="cmd('S')">→</button>

        <button class="btn empty"></button>
        <button class="btn" onpointerdown="cmd('B')" onpointerup="cmd('S')">↓</button>
        <button class="btn empty"></button>
    </div>
</div>

<div class="box">
    <h3>PID Tuning</h3>

    KP:
    <input type="range" min="0" max="100" step="0.5" value="35"
        class="slider" id="kp" oninput="sendPID()">
    <span id="kpVal">35</span><br>

    KI:
    <input type="range" min="0" max="50" step="0.5" value="10"
        class="slider" id="ki" oninput="sendPID()">
    <span id="kiVal">10</span><br>

    KD:
    <input type="range" min="0" max="10" step="0.1" value="1.8"
        class="slider" id="kd" oninput="sendPID()">
    <span id="kdVal">1.8</span><br>
</div>

<script>

function cmd(c){
    fetch("/action?cmd=" + c);
}

function sendPID(){
    let kp = document.getElementById("kp").value;
    let ki = document.getElementById("ki").value;
    let kd = document.getElementById("kd").value;

    document.getElementById("kpVal").innerText = kp;
    document.getElementById("kiVal").innerText = ki;
    document.getElementById("kdVal").innerText = kd;

    fetch(`/pid?kp=${kp}&ki=${ki}&kd=${kd}`);
}

// 🔥 NEW: load real values from ESP32
function loadPID(){
    fetch("/pid_get")
    .then(res => res.json())
    .then(data => {
        document.getElementById("kp").value = data.kp;
        document.getElementById("ki").value = data.ki;
        document.getElementById("kd").value = data.kd;

        document.getElementById("kpVal").innerText = data.kp;
        document.getElementById("kiVal").innerText = data.ki;
        document.getElementById("kdVal").innerText = data.kd;
    });
}

// run when page opens
window.onload = loadPID;

document.addEventListener("pointerup", () => cmd('S'));
document.addEventListener("pointercancel", () => cmd('S'));

</script>

</body>
</html>
)rawliteral";

void handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", HTML_PAGE);
}
void handlePID() {
    if (server.hasArg("kp")) Kp_angle = server.arg("kp").toFloat();
    if (server.hasArg("ki")) Ki_angle = server.arg("ki").toFloat();
    if (server.hasArg("kd")) Kd_angle = server.arg("kd").toFloat();

    server.send(200, "text/plain", "PID updated");
}
void handleGetPID() {
    String json = "{";
    json += "\"kp\":" + String(Kp_angle, 2) + ",";
    json += "\"ki\":" + String(Ki_angle, 2) + ",";
    json += "\"kd\":" + String(Kd_angle, 2);
    json += "}";

    server.send(200, "application/json", json);
}

void handleAction(){
    if(server.hasArg("cmd")){
        String c = server.arg("cmd");

        if(c=="F") moveOffset = -5.0;
        else if(c=="B") moveOffset = 5.0;
        else if(c=="L") steeringOffset = 20;
        else if(c=="R") steeringOffset = -20;
        else if(c=="S"){
    moveOffset = 0;
    steeringOffset = 0;

    angleIntegral = 0;
    encoderLeftCount = 0;
    encoderRightCount = 0;

    lastAngle = angle;

    setMotor(0,0);
}
if(c=="TL"){
    moveOffset = 0;
    steeringOffset = 40;
}
else if(c=="TR"){
    moveOffset = 0;
    steeringOffset = -40;
}
    }
    server.send(200,"text/plain","OK");
}
// =====================================================
// SETUP
// =====================================================
void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!mpu.begin()) {
        Serial.println("MPU6050 NOT FOUND!");
        while (1);
    }

    pinMode(AIN1, OUTPUT);   pinMode(AIN2, OUTPUT);
    pinMode(BIN1, OUTPUT);   pinMode(BIN2, OUTPUT);
    pinMode(PWMA, OUTPUT);   pinMode(PWMB, OUTPUT);

    pinMode(ENCA_PHASE_A, INPUT_PULLUP);
    pinMode(ENCA_PHASE_B, INPUT_PULLUP);
    pinMode(ENCB_PHASE_A, INPUT_PULLUP);
    pinMode(ENCB_PHASE_B, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(ENCA_PHASE_A), readEncoderLeft, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCB_PHASE_A), readEncoderRight, RISING);

    calibrateGyro();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    angle = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
    angle -= angleOffset;

   
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    server.on("/", handleRoot);
    server.on("/action", handleAction);
    server.on("/pid", handlePID);
    server.on("/pid_get", handleGetPID);

    // -----------------------------------------------------------------
    // RTOS TASKS
    // -----------------------------------------------------------------
    xTaskCreatePinnedToCore(vBalanceTask, "BalanceTask", 4096, NULL, 5, &BalanceTaskHandle, 1); // Core 1
    xTaskCreatePinnedToCore(vWebServerTask, "WebServerTask", 4096, NULL, 1, &WebServerTaskHandle, 0); // Core 0
}

void loop() {
    vTaskDelete(NULL); 
}

// =====================================================
// THREAD 1:  (Core 1)
// =====================================================
void vBalanceTask(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(10); 
    xLastWakeTime = xTaskGetTickCount();
    float dt = 0.01f;

    for(;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        sensors_event_t a, g, temp;
        mpu.getEvent(&a, &g, &temp);

        float accAngle = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
        accAngle -= angleOffset;
        float gyroRate = (g.gyro.x * 180.0 / PI) + gyroOffset;
        angle = 0.98f * (angle + gyroRate * dt) + 0.02f * accAngle;
        angleVelocity = (angle - lastAngle) / dt;
        lastAngle = angle;
        long currentPosition = (encoderLeftCount + encoderRightCount) / 2;
        long positionVelocity = (encoderLeftCount - encoderRightCount); 

        float targetAngle = moveOffset;
        targetAngle = constrain(targetAngle, -6.0, 6.0); 

        float angleError = angle - targetAngle;

        if (abs(angleError) < 10.0) {
            angleIntegral += angleError * dt;
            angleIntegral = constrain(angleIntegral, -100.0, 100.0);
        } else {
            angleIntegral = 0;
        }

        float baseOutput = -(Kp_angle * angleError + Ki_angle * angleIntegral + Kd_angle * gyroRate);

        int motorLeftSpeed = (int)(baseOutput + steeringOffset);
        int motorRightSpeed = (int)(baseOutput - steeringOffset);
        static uint32_t lastPrint = 0;

if(millis() - lastPrint > 200){
    lastPrint = millis();

    Serial.print("Angle=");
    Serial.print(angle);

    Serial.print(" Gyro=");
    Serial.print(gyroRate);

    Serial.print(" Kp=");
    Serial.print(Kp_angle);

    Serial.print(" Ki=");
    Serial.print(Ki_angle);

    Serial.print(" Kd=");
    Serial.print(Kd_angle);

    Serial.print(" Move=");
    Serial.print(moveOffset);

    Serial.print(" Enc=");
    Serial.println((encoderLeftCount + encoderRightCount)/2);
}
        if (abs(angle) > 40.0) {
            motorLeftSpeed = 0;
            motorRightSpeed = 0;
            angleIntegral = 0;
            encoderLeftCount = 0;  
            encoderRightCount = 0;
        }

        setMotor(motorLeftSpeed, motorRightSpeed);
    }
}

// =====================================================
// THREAD 2: (Core 0)
// =====================================================
void vWebServerTask(void *pvParameters) {
    server.begin();
    
    for(;;) {
        server.handleClient(); 
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}