// BasicConnect: minimal DS4 bring-up.
// Phase 2..5 progression in one sketch:
//   - Phase 2: prints "DS4Arduino starting... / Bluetooth initialized / Scanning..."
//   - Phase 3: connection banners (DS4 found / Connecting... / DS4 connected)
//   - Phase 4: raw REPORT ID + LENGTH
//   - Phase 5: LX/LY/RX/RY/L2/R2
#include <DS4Arduino.h>

DS4Controller ds4;

void setup() {
    Serial.begin(115200);
    // Wait briefly for USB serial monitors to attach.
    for (int i = 0; i < 20 && !Serial; i++) delay(100);
    ds4.begin();
}

void loop() {
    ds4.update();

    if (!ds4.connected()) {
        static unsigned long last = 0;
        if (millis() - last > 2000) {
            last = millis();
            Serial.print("Waiting for DS4... ");
            Serial.println(ds4.statusText());
        }
        delay(50);
        return;
    }
    // Phase 4: raw report diagnostics (only once input actually flows).
    if (ds4.packetCount() == 0) {
        static unsigned long lastw = 0;
        if (millis() - lastw > 2000) {
            lastw = millis();
            Serial.println(ds4.statusText());
        }
        delay(100);
        return;
    }
    Serial.print("REPORT ID: 0x");
    Serial.print(ds4.lastReportId(), HEX);
    Serial.print(" LENGTH: ");
    Serial.println(ds4.lastReportLen());

    // Phase 5: parsed sticks + triggers.
    Serial.printf("LX=%3d LY=%3d RX=%3d RY=%3d L2=%3d R2=%3d\n",
                  ds4.leftStickX(), ds4.leftStickY(),
                  ds4.rightStickX(), ds4.rightStickY(),
                  ds4.l2(), ds4.r2());

    // Phase 6+: buttons / battery.
    if (ds4.cross() || ds4.circle() || ds4.square() || ds4.triangle()) {
        Serial.printf("BTN cross=%d circle=%d square=%d tri=%d dpad=%d batt=%d\n",
                      ds4.cross(), ds4.circle(), ds4.square(), ds4.triangle(),
                      (int)ds4.dpad(), ds4.batteryLevel());
    }

    delay(100);  // keep serial readable; BT queue absorbs jitter
}
