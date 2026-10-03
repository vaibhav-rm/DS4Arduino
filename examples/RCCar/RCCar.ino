#include <DS4Arduino.h>

// ========================================
// TB6612FNG
// ========================================

// Left motor
#define PWMA 5
#define AIN1 18
#define AIN2 19

// Right motor
#define PWMB 23
#define BIN1 21
#define BIN2 22

// Standby
#define STBY 17

// LEDC PWM channels (Arduino-ESP32 2.x/3.x compatible, see setup)
#define CH_A 0
#define CH_B 1
#define PWM_FREQ 1000
#define PWM_RES 8  // duty 0..255

// PWM write that matches the attach style below: on core 3.x ledcWrite
// takes a PIN, on 2.x it takes the channel. Mixing them up drives the
// wrong pins (e.g. channels 0/1 hit GPIO0/GPIO1) and the car sits dead.
static inline void pwmA(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWMA, duty);
#else
  ledcWrite(CH_A, duty);
#endif
}

static inline void pwmB(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PWMB, duty);
#else
  ledcWrite(CH_B, duty);
#endif
}

// ========================================
// SETTINGS
// ========================================

#define DEADZONE 18   // raw stick units away from centre (128)
#define MAX_SPEED 255

// This car has its right motor leads flipped at the B01/B02 terminals,
// so a "forward" command physically drives it backward. Compensate here
// instead of rewiring: 1 = right motor reversed, 0 = wired straight.
#define RIGHT_MOTOR_REVERSED 1

DS4Controller ds4;

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);
  for (int i = 0; i < 20 && !Serial; i++) delay(100);

  // Motor pins
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);

  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  pinMode(STBY, OUTPUT);

  // PWM channels (API differs between Arduino-ESP32 2.x and 3.x)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PWMA, PWM_FREQ, PWM_RES);
  ledcAttach(PWMB, PWM_FREQ, PWM_RES);
#else
  ledcSetup(CH_A, PWM_FREQ, PWM_RES);
  ledcSetup(CH_B, PWM_FREQ, PWM_RES);
  ledcAttachPin(PWMA, CH_A);
  ledcAttachPin(PWMB, CH_B);
#endif

  // Enable TB6612FNG
  digitalWrite(STBY, HIGH);

  stopMotors();

  // DS4: no MAC address needed. Pairing is SHARE + PS (see below).
  ds4.begin();

  Serial.println("================================");
  Serial.println("       DS4 ESP32 RC CAR");
  Serial.println("================================");
  Serial.println("Hold SHARE + PS until the light bar flashes white.");
}

// ========================================
// LOOP
// ========================================

void loop() {

  ds4.update();

  // Safety stop if controller disconnects
  if (!ds4.connected()) {
    stopMotors();
    static unsigned long lastw = 0;
    if (millis() - lastw > 2000) {
      lastw = millis();
      Serial.print("Waiting for DS4... ");
      Serial.println(ds4.statusText());
    }
    delay(50);
    return;
  }

  // ======================================
  // LEFT STICK Y
  // Forward / Reverse
  // DS4 raw: 0 = up, 255 = down, 128 = centre
  // ======================================

  int throttle = 128 - (int)ds4.leftStickY();  // UP = positive

  // ======================================
  // RIGHT STICK X
  // Left / Right
  // DS4 raw: 0 = left, 255 = right
  // ======================================

  int steering = (int)ds4.rightStickX() - 128;  // RIGHT = positive

  // ======================================
  // DEADZONE
  // ======================================

  if (abs(throttle) < DEADZONE)
    throttle = 0;

  if (abs(steering) < DEADZONE)
    steering = 0;

  // ======================================
  // CONVERT to -255..+255
  // ======================================

  throttle = constrain(throttle * 2, -MAX_SPEED, MAX_SPEED);
  steering = constrain(steering * 2, -MAX_SPEED, MAX_SPEED);

  // ======================================
  // DIFFERENTIAL STEERING
  // ======================================

  int leftSpeed =
    throttle + steering;

  int rightSpeed =
    throttle - steering;

  leftSpeed = constrain(
    leftSpeed,
    -MAX_SPEED,
    MAX_SPEED
  );

  rightSpeed = constrain(
    rightSpeed,
    -MAX_SPEED,
    MAX_SPEED
  );

  // ======================================
  // MOTOR CONTROL
  // ======================================

  setLeftMotor(leftSpeed);
  setRightMotor(rightSpeed);

  // Debug, throttled so the serial link stays readable
  static unsigned long lastd = 0;
  if (millis() - lastd > 100) {
    lastd = millis();
    Serial.printf(
      "T:%4d S:%4d L:%4d R:%4d\n",
      throttle,
      steering,
      leftSpeed,
      rightSpeed
    );
  }

  delay(10);
}

// ========================================
// LEFT MOTOR
// ========================================

void setLeftMotor(int speed) {

  speed = constrain(speed, -255, 255);

  if (speed > 0) {

    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);

    pwmA(speed);

  } else if (speed < 0) {

    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, HIGH);

    pwmA(-speed);

  } else {

    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);

    pwmA(0);
  }
}

// ========================================
// RIGHT MOTOR
// ========================================

void setRightMotor(int speed) {

  speed = constrain(speed, -255, 255);

#if RIGHT_MOTOR_REVERSED
  speed = -speed;
#endif

  if (speed > 0) {

    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);

    pwmB(speed);

  } else if (speed < 0) {

    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, HIGH);

    pwmB(-speed);

  } else {

    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);

    pwmB(0);
  }
}

// ========================================
// STOP
// ========================================

void stopMotors() {

  pwmA(0);
  pwmB(0);

  digitalWrite(AIN1, LOW);
  digitalWrite(AIN2, LOW);

  digitalWrite(BIN1, LOW);
  digitalWrite(BIN2, LOW);
}
