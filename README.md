# Self-Balancing-Robot
Built on ESP32 using MPU6050 and TB6612FNG supporting wireless PID parameter adjustment and live telemetry broadcasting.

## Hardware
The project combines:  
- MPU6050
- TB6612FNG
- ESP32
- Two batteries in series
- DC/DC Buck Converter Step Down Voltage Regulator Module Input 5V-30V Output 5V.\
- On/Off switch
- 2 sets of TT motor with wheels


To hold every component in place I designed a three-story body of a robot:  
- the lower one holds ESP32.
- the middle one hold MPU6050 in the perfect center of the robot layed horizontally and TB6612FNG on the side.
- the upper holds batteries and a buck next to it. Weight on top makes the robot react to the fall earlier.

Some would think the batteries at the lowest floor would make the system more stable, and that could totally be true :)
  
On each level there is a hole where wires go through to make sure that they are kept inside the body of the robot and not exposed to the outside world.


## Software





## Pinout

| Buck | Battery |
|------|---------|
|  IN+ | positive|
|  IN- | TB6612FNG's GND|
| OUT+ |  ESP32's VIN|
| OUT- | TB6612FNG's GND|


| Battery | other |
|---------|------|
|positive | Buck IN+ |
|negative | TB6612FNG's GND|


| MPU6050 | ESP32  |
|---------|--------|
| VCCIN   | 3.3V   |
|  GND    | GND    |
|  SCL    | GPIO 22|
|  SDA    | GPIO 21|


| ESP32 | TB6612FNG|
|-------|----------|
|GPIO 25| PWMA|
|GPIO 27| AIN2|
|GPIO 26| AIN1|
| 3.3V  | STBY|
|GPIO 12| BIN1|
|GPIO 13| BIN2|
|GPIO 14| PWMB|
|GND    |  GND|


| TB6612FNG | Other |
|---------|-------|
|VM       |   battery+ & Buck's IN+|
|VCC      |   3.3V|
|GND      |   Buck's IN- & OUT-|
|A01      |  motor A leg|
|A02      |  motor A other leg|
|B02      |  motor B leg|
|B01      |  motor B other leg|
|GND      | Battery-|

**P.S.** Between battery+ and TB6612FNG's VM, I have inserted a switch.
