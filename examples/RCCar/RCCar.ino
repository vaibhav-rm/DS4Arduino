// RCCar: drive a TB6612FNG dual-motor car from a DualShock 4.
//
// Mapping (example only, NOT part of the core library):
//   Left stick Y  -> forward / reverse (both motors)
//   Right stick X -> steering (differential mix)
//
// TB6612FNG wiring:
//   PWMA = GPIO 5,  AIN1 = GPIO 18, AIN2 = GPIO 19
//   PWMB = GPIO 23, BIN1 = GPIO 21, BIN2 = GPIO 22
//   STBY = GPIO 17
//
// Pairing: flash, power the ESP32, hold SHARE+PS until the light bar
// flashes, wait for connection. Left stick full-up = full forward.
#include <DS4Arduino.h>

DS4Controller ds4;

// Motor pins (TB6612FNG).
static const int PWMA = 5;
static const int AIN1 = 18;
static const int AIN2 = 19;
static const int PWMB = 23;
static const int BIN1 = 21;
static const int BIN2 = 22;
static const int STBY = 17;

// LEDC PWM channels (Arduino-ESP32 2.x/3.x compatible via ledc* API).
static const int CH_A = 0;
static const int CH_B = 1;
static const int PWM_FREQ = 20000;
static const int PWM_RES = 8;  // 0..255

static int stickToSigned(uint8_t v) {
    // 0..255 centre 128 -> -255..+255 (Y up = positive forward).
    int s = (int)v - 128;
    // Small deadband around centre.
    if (s > -6 && s < 6) s = 0;
    return s * 2 > 255 ? 255 : (s * 2 < -255 ? -255 : s * 2);
}

static void motorWrite(int pwmPinChannel, int in1, int in2, int speed) {
    // speed: -255..+255
    if (speed > 0) {
        digitalWrite(in1, HIGH);
        digitalWrite(in2, LOW);
        ledcWrite(pwmPinChannel, speed);
    } else if (speed < 0) {
        digitalWrite(in1, LOW);
        digitalWrite(in2, HIGH);
        ledcWrite(pwmPinChannel, -speed);
    } else {
        digitalWrite(in1, LOW);
        digitalWrite(in2, LOW);
        ledcWrite(pwmPinChannel, 0);
    }
}

void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 20 && !Serial; i++) delay(100);

    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    pinMode(BIN1, OUTPUT);
    pinMode(BIN2, OUTPUT);
    pinMode(STBY, OUTPUT);
    digitalWrite(STBY, HIGH);  // enable driver

    // LEDC PWM setup (API differs between Arduino-ESP32 2.x and 3.x).
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(PWMA, PWM_FREQ, PWM_RES, CH_A);
    ledcAttachChannel(PWMB, PWM_FREQ, PWM_RES, CH_B);
#else
    ledcSetup(CH_A, PWM_FREQ, PWM_RES);
    ledcSetup(CH_B, PWM_FREQ, PWM_RES);
    ledcAttachPin(PWMA, CH_A);
    ledcAttachPin(PWMB, CH_B);
#endif

    ds4.begin();
    Serial.println("RCCar ready. Hold SHARE+PS to pair.");
}

void loop() {
    ds4.update();

    if (!ds4.connected()) {
        // Safety: stop motors when link is down.
        motorWrite(CH_A, AIN1, AIN2, 0);
        motorWrite(CH_B, BIN1, BIN2, 0);
        delay(50);
        return;
    }

    // NOTE: DS4 Y axis: 0 = up. leftStickY() returns raw 0..255.
    int forward = -stickToSigned(ds4.leftStickY());   // up = +forward
    int steer = stickToSigned(ds4.rightStickX());      // right = +

    int left = forward + steer / 2;
    int right = forward - steer / 2;
    left = constrain(left, -255, 255);
    right = constrain(right, -255, 255);

    motorWrite(CH_A, AIN1, AIN2, left);
    motorWrite(CH_B, BIN1, BIN2, right);

    static unsigned long last = 0;
    if (millis() - last > 250) {
        last = millis();
        Serial.printf("LY=%d RX=%d fwd=%d str=%d L=%d R=%d batt=%d\n",
                      ds4.leftStickY(), ds4.rightStickX(),
                      forward, steer, left, right, ds4.batteryLevel());
    }
}
