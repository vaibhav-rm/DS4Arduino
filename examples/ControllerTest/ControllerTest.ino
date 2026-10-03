// ControllerTest: exercise every DS4 input and verify the link.
//
// Human output: button edges, D-pad changes, periodic battery/IMU.
// Machine output (for docs/live.html over WebSerial, 20 Hz while connected):
//
//   $DS4,SEQ,LX,LY,RX,RY,L2,R2,B0,B1,B2,BATT,CHG,GX,GY,GZ,AX,AY,AZ,T0A,T0X,T0Y,T1A,T1X,T1Y*CS
//
//   SEQ   parsed-report counter (missed packets visible as jumps)
//   LX..R2 0..255 sticks/triggers (centre 128)
//   B0/B1/B2 raw button bytes: B0 = hat(lo nibble)+square/cross/circle/triangle,
//     B1 = L1/R1/L2/R2/Share/Options/L3/R3, B2 = PS/touchpad/counter
//   BATT 0..10, CHG 0/1, GX..AZ raw signed IMU
//   T0A/T1A touch active 0/1, T0X/T0Y/T1X/T1Y 0..1920 / 0..942
//   CS = XOR of all bytes between '$' and '*' as 2 uppercase hex digits
//
// Lines starting with '#' are comments for humans (ignored by the web page).
//
// How to test: pair (SHARE+PS), then press every button one by one, move
// both sticks through their full range, squeeze L2/R2, swipe the touchpad,
// tilt the controller.
//
// Output-path demo: each press of TRIANGLE steps the light bar through
// off/red/green/blue (printed as "# led=R,G,B"). Delete the triEdge block
// below if you never want the sketch to touch the LED.
#include <DS4Arduino.h>

DS4Controller ds4;

static uint8_t xorsum(const char* s) {
    uint8_t c = 0;
    while (*s) c ^= (uint8_t)*s++;
    return c;
}

struct BtnCache {
    bool cross, circle, square, triangle, l1, r1, l3, r3;
    bool share, options, ps, touchpad;
    int dpad;
    bool valid = false;
};
static BtnCache lastBtn;
static uint8_t ledStep = 0;

static void printChanged(const char* name, bool now, bool was) {
    if (now && !was) {
        Serial.print("# press ");
        Serial.println(name);
    }
}

void setup() {
    Serial.begin(115200);
    for (int i = 0; i < 20 && !Serial; i++) delay(100);
    Serial.println("# DS4Arduino ControllerTest");
    Serial.println("# $DS4 machine lines at 20 Hz; web viewer: docs/live.html");
    ds4.begin();
}

void loop() {
    ds4.update();

    if (!ds4.connected()) {
        static unsigned long last = 0;
        if (millis() - last > 2000) {
            last = millis();
            Serial.print("# waiting: ");
            Serial.println(ds4.statusText());
        }
        lastBtn.valid = false;
        delay(50);
        return;
    }

    // --- human-readable edges ---
    if (!lastBtn.valid) {
        Serial.println("# connected, test every input now");
        lastBtn.valid = true;
    } else {
        bool triEdge = ds4.triangle() && !lastBtn.triangle;
        printChanged("cross", ds4.cross(), lastBtn.cross);
        printChanged("circle", ds4.circle(), lastBtn.circle);
        printChanged("square", ds4.square(), lastBtn.square);
        printChanged("triangle", ds4.triangle(), lastBtn.triangle);
        printChanged("l1", ds4.l1(), lastBtn.l1);
        printChanged("r1", ds4.r1(), lastBtn.r1);
        printChanged("l3", ds4.l3(), lastBtn.l3);
        printChanged("r3", ds4.r3(), lastBtn.r3);
        printChanged("share", ds4.share(), lastBtn.share);
        printChanged("options", ds4.options(), lastBtn.options);
        printChanged("ps", ds4.ps(), lastBtn.ps);
        printChanged("touchpad", ds4.touchpad(), lastBtn.touchpad);
        if ((int)ds4.dpad() != lastBtn.dpad) {
            Serial.print("# dpad=");
            Serial.println((int)ds4.dpad());
        }
        if (triEdge) {  // output-path demo: step the light bar color
            ledStep = (uint8_t)((ledStep + 1) % 4);
            uint8_t r = 0, g = 0, b = 0;
            if (ledStep == 1) r = 255;
            if (ledStep == 2) g = 255;
            if (ledStep == 3) b = 255;
            ds4.setLED(r, g, b);
            Serial.print("# led=");
            Serial.print(r);
            Serial.print(",");
            Serial.print(g);
            Serial.print(",");
            Serial.println(b);
        }
    }
    lastBtn.cross = ds4.cross();
    lastBtn.circle = ds4.circle();
    lastBtn.square = ds4.square();
    lastBtn.triangle = ds4.triangle();
    lastBtn.l1 = ds4.l1();
    lastBtn.r1 = ds4.r1();
    lastBtn.l3 = ds4.l3();
    lastBtn.r3 = ds4.r3();
    lastBtn.share = ds4.share();
    lastBtn.options = ds4.options();
    lastBtn.ps = ds4.ps();
    lastBtn.touchpad = ds4.touchpad();
    lastBtn.dpad = (int)ds4.dpad();

    // --- machine-readable line, 20 Hz ---
    static unsigned long lastm = 0;
    if (millis() - lastm >= 50) {
        lastm = millis();
        const DS4::DS4State& s = ds4.rawState();
        // Exact raw button bytes (B0/B1/B2) for decoding on the web page:
        uint8_t b0 = (uint8_t)(((uint8_t)s.dpad & 0x0F) | (s.square ? 0x10 : 0) |
                               (s.cross ? 0x20 : 0) | (s.circle ? 0x40 : 0) |
                               (s.triangle ? 0x80 : 0));
        uint8_t b1 = (uint8_t)((s.l1 ? 0x01 : 0) | (s.r1 ? 0x02 : 0) | (s.l2Btn ? 0x04 : 0) |
                               (s.r2Btn ? 0x08 : 0) | (s.share ? 0x10 : 0) |
                               (s.options ? 0x20 : 0) | (s.l3 ? 0x40 : 0) | (s.r3 ? 0x80 : 0));
        uint8_t b2 = (uint8_t)((s.ps ? 0x01 : 0) | (s.touchpadBtn ? 0x02 : 0) |
                               ((s.packetCounter & 0x3F) << 2));
        char line[160];
        snprintf(line, sizeof(line),
                 "$DS4,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                 (unsigned long)s.sequence, s.leftX, s.leftY, s.rightX, s.rightY,
                 s.l2Analog, s.r2Analog, b0, b1, b2, s.battery, s.charging ? 1 : 0, s.gyroX,
                 s.gyroY, s.gyroZ, s.accelX, s.accelY, s.accelZ, s.touch0.active ? 1 : 0,
                 s.touch0.x, s.touch0.y, s.touch1.active ? 1 : 0, s.touch1.x, s.touch1.y);
        // Checksum over everything between '$' and '*'.
        uint8_t cs = xorsum(line + 1);
        char out[170];
        snprintf(out, sizeof(out), "%s*%02X", line, cs);
        Serial.println(out);
    }
}
