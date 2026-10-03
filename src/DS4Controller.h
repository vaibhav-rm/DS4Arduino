#pragma once

// DS4Controller: the public Arduino API.
//
// Sketch usage:
//
//   #include <DS4Arduino.h>
//   DS4Controller ds4;
//   void setup() { Serial.begin(115200); ds4.begin(); }
//   void loop() {
//       ds4.update();
//       if (ds4.connected()) { Serial.println(ds4.leftStickX()); }
//   }
//
// Threading: update() must be called from loop(). It drains the
// Bluetooth report queue (filled from BT callbacks) and parses each
// report once. Never blocks the BT task with Serial printing or
// motor control.

#include "DS4Types.h"
#include "DS4Bluetooth.h"
#include "DS4Parser.h"
#include "DS4HID.h"

class DS4Controller {
public:
    DS4Controller();
    ~DS4Controller();

    // Start Bluetooth + discovery. Returns true when the stack accepted
    // the request (connection itself is asynchronous).
    bool begin();
    void end();

    // Drain queued HID reports and parse the newest state.
    // Call every loop() iteration. Also drives reconnect retries and
    // prints connection banners on state changes.
    void update();

    bool connected() const;

    // Last sticky error string, e.g. "HID connection failed".
    const char* lastErrorText() const;
    DS4::DS4Error lastError() const { return lastError_; }
    DS4::DS4Error beginError() const { return beginError_; }

    // Human-readable transport state for "Waiting..." loops, e.g.
    // "Scanning... hold SHARE+PS", "Connecting...", "Connected".
    const char* statusText() const;

    // Raw report diagnostics (Phase 4): ID + length of newest report.
    uint8_t lastReportId() const { return lastReportId_; }
    uint8_t lastReportLen() const { return lastReportLen_; }

    // ---- Sticks / triggers (0..255, centre 128) ----
    uint8_t leftStickX() const { return state_.leftX; }
    uint8_t leftStickY() const { return state_.leftY; }
    uint8_t rightStickX() const { return state_.rightX; }
    uint8_t rightStickY() const { return state_.rightY; }
    uint8_t l2() const { return state_.l2Analog; }
    uint8_t r2() const { return state_.r2Analog; }

    // ---- Buttons ----
    bool cross() const { return state_.cross; }
    bool circle() const { return state_.circle; }
    bool square() const { return state_.square; }
    bool triangle() const { return state_.triangle; }
    bool l1() const { return state_.l1; }
    bool r1() const { return state_.r1; }
    bool l3() const { return state_.l3; }
    bool r3() const { return state_.r3; }
    bool share() const { return state_.share; }
    bool options() const { return state_.options; }
    bool ps() const { return state_.ps; }
    bool touchpad() const { return state_.touchpadBtn; }

    // ---- D-pad ----
    DS4::DS4Dpad dpad() const { return state_.dpad; }
    bool dpadUp() const { return state_.dpadUp; }
    bool dpadDown() const { return state_.dpadDown; }
    bool dpadLeft() const { return state_.dpadLeft; }
    bool dpadRight() const { return state_.dpadRight; }

    // ---- Motion (raw int16) ----
    int16_t gyroX() const { return state_.gyroX; }
    int16_t gyroY() const { return state_.gyroY; }
    int16_t gyroZ() const { return state_.gyroZ; }
    int16_t accelX() const { return state_.accelX; }
    int16_t accelY() const { return state_.accelY; }
    int16_t accelZ() const { return state_.accelZ; }

    uint8_t batteryLevel() const { return state_.battery; }
    bool charging() const { return state_.charging; }
    uint32_t packetCount() const { return state_.sequence; }

    // ---- Touchpad contacts ----
    bool touch0Active() const { return state_.touch0.active; }
    uint8_t touch0Id() const { return state_.touch0.id; }
    uint16_t touch0X() const { return state_.touch0.x; }
    uint16_t touch0Y() const { return state_.touch0.y; }
    bool touch1Active() const { return state_.touch1.active; }
    uint8_t touch1Id() const { return state_.touch1.id; }
    uint16_t touch1X() const { return state_.touch1.x; }
    uint16_t touch1Y() const { return state_.touch1.y; }

    const DS4::DS4State& rawState() const { return state_; }

    // ---- Output (Phase 8; queued for the BT task, best-effort) ----
    bool setLED(uint8_t r, uint8_t g, uint8_t b);
    bool setRumble(uint8_t small, uint8_t large);

    // Test/bring-up helper: inject a raw report (bypasses Bluetooth).
    bool injectReport(const uint8_t* data, size_t len);

private:
    DS4::DS4Bluetooth bt_;
    DS4::DS4State state_;
    DS4::DS4Error lastError_ = DS4::DS4_OK;
    DS4::DS4Error beginError_ = DS4::DS4_OK;
    uint8_t lastReportId_ = 0;
    uint8_t lastReportLen_ = 0;
    bool verboseLog_ = false;

    DS4::BTState prevBtState_ = DS4::BTState::Idle;
    unsigned long lastRetryMs_ = 0;
    unsigned long scanRetryMs_ = 0;
    uint32_t lastDiscSeen_ = 0;
    bool fullModeSent_ = false;
    mutable char scanMsg_[96];

    void setError(DS4::DS4Error e) { lastError_ = e; }
#if defined(ARDUINO)
    void onBtStateChange(DS4::BTState st);
    void sendFullModeEnable();
#endif
};
