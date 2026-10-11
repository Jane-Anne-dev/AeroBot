// ============================================================
//  AeroBot firmware - U8g2 version, 20 animated faces
//  Display : SH1106 OLED (128 x 64, 1.3", I2C)
//  Board   : Seeed XIAO ESP32 series C3, 12-bit ADC
//  Libs    : U8g2 (olikraus), Adafruit MPU6050,
//            Adafruit Unified Sensor, Adafruit BusIO
//
//  Mood button cycle (starts in AUTO at boot, never returns to it):
//     Tap  1 Happy         Tap  2 Wink          Tap  3 Love
//     Tap  4 Sleepy        Tap  5 Cry           Tap  6 Distressed
//     Tap  7 Glare         Tap  8 Mad           Tap  9 Glasses
//     Tap 10 Sleep         Tap 11 Blink upper   Tap 12 Tired
//     Tap 13 Night         Tap 14 Upper lids    Tap 15 Lower lids
//     Tap 16 Blank         Tap 17 "HELLO"       Tap 18 "BYE"
//     Tap 19 "What?"       Tap 20 Scuba Cat (sways side to side)
//     Tap 21 = back to Happy ... and so on forever
//
//  MPU6050: EVERY face reacts to motion
//     - tilt  : the whole face slides with the tilt + pupils track it
//     - shake : the face shudders for a moment when the robot is shaken
//
//  Using an SSD1306 instead? Replace the display constructor with:
//     U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------------- SERIAL DEBUG (guarded) ----------------
// On XIAO ESP32 boards D6 is the UART0 TX pin. If "USB CDC On Boot" is Disabled,
// Serial uses UART0 and takes over D6, which is your RECALIBRATE button.
// So Serial is only used when USB CDC On Boot = Enabled (Tools menu), where it is
// routed over USB and D6 stays free for the button.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
  #define DEBUG_BEGIN()      Serial.begin(115200)
  #define DEBUG_PRINTLN(x)   Serial.println(x)
#else
  #define DEBUG_BEGIN()
  #define DEBUG_PRINTLN(x)
#endif

// ---------------- DISPLAY ----------------
#define OLED_ADDR 0x3C          // try 0x3D if the screen is not found

// _F_ = full frame buffer (smoothest animation, uses ~1 KB RAM)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_MPU6050 mpu;

// Fonts used
#define FONT_SMALL u8g2_font_6x10_tr        // 6x10, ascent 8
#define FONT_BIG   u8g2_font_9x15_tr        // 9x15, ascent 12 (big "Z")
#define FONT_WORD  u8g2_font_logisoso28_tr  // big word faces

// ---------------- PINS ----------------
const int BUTTON_1 = D1;            // SW2: cycle mood
const int BUTTON_2 = D2;            // SW3: face / data page
const int BUTTON_3 = D5;            // SW4: screen on / off
const int BUTTON_4 = D6;            // SW5: recalibrate eyes
const int PLANT_SENSOR_ANALOG = D0; // soil moisture analog out

const int XIAO_SDA = D4;
const int XIAO_SCL = D3;

// ---------------- MOISTURE SETTINGS ----------------
const int  ADC_MAX = 4095;          // 12-bit ADC
// Capacitive sensors: HIGHER value = DRIER. Set false for sensors where LOW = dry.
const bool HIGHER_IS_DRIER = true;
const int  THIRSTY_LEVEL   = 2500;  // face turns "sassy" (flat mouth)
const int  CRITICAL_LEVEL  = 2900;  // warning sign + "WATER ME!"
const int  HYSTERESIS      = 100;   // stops the warning flickering on/off

// ---------------- EYE SETTINGS ----------------
const float MAX_PUPIL_X = 6.0f;     // eye radius 12 - pupil radius 5 - 1px margin
const float MAX_PUPIL_Y = 6.0f;
const float TILT_GAIN   = 2.0f;     // pixels per m/s^2 (full deflection at ~17 deg tilt)
const float TILT_DEADBAND = 0.15f;  // m/s^2 ignored to hide sensor noise

// Whole-face reaction to the MPU6050
const float FACE_SHIFT_X_PER_PUPIL = 1.3f;   // face slides up to ~8 px sideways
const float FACE_SHIFT_Y_PER_PUPIL = 0.7f;   // ... and ~4 px up/down
const float SHAKE_ACCEL_DELTA = 5.0f;        // |accel| differs from 1 g by this -> shake
const float SHAKE_GYRO_RADS   = 5.0f;        // or spinning faster than this (rad/s)
const uint32_t SHAKE_REACT_MS = 800;         // how long the shudder lasts

const int EX_L  = 36;               // left eye center x
const int EX_R  = 92;               // right eye center x
const int EYE_Y = 30;               // default eye center y

// ---------------- EXPRESSIONS ----------------
enum Expression : uint8_t {
    EXPR_AUTO        = 0,   // boot state: sensor-driven face (not part of the cycle)
    EXPR_HAPPY       = 1,
    EXPR_WINK        = 2,
    EXPR_LOVE        = 3,
    EXPR_SLEEPY      = 4,
    EXPR_CRY         = 5,
    EXPR_DISTRESSED  = 6,
    EXPR_GLARE       = 7,
    EXPR_MAD         = 8,
    EXPR_GLASSES     = 9,
    EXPR_SLEEP       = 10,
    EXPR_BLINK_UPPER = 11,
    EXPR_TIRED       = 12,
    EXPR_NIGHT       = 13,
    EXPR_UPPER_LIDS  = 14,
    EXPR_LOWER_LIDS  = 15,
    EXPR_BLANK       = 16,
    EXPR_HELLO       = 17,
    EXPR_BYE         = 18,
    EXPR_WHAT        = 19,
    EXPR_SCUBA       = 20   // always last
};
const uint8_t EXPR_COUNT = 20;                  // number of cycling expressions
const char *const MOOD_NAMES[] = {
    "Auto", "Happy", "Wink", "Love", "Sleepy",
    "Cry", "Distress", "Glare", "Mad", "Glasses",
    "Sleep", "BlinkUp", "Tired", "Night", "UpLids",
    "LowLids", "Blank", "Hello", "Bye", "What",
    "ScubaCat"
};
const uint32_t TRANSITION_MS = 120;             // quick "blink" between expressions

// ---------------- STATE ----------------
float accelX = 0, accelY = 0;       // raw accelerometer
float baseAccelX = 0, baseAccelY = 0;
float eyeX = 0, eyeY = 0;           // smoothed pupil offset
float moistureSmooth = 0;
int   rawMoisture = 0;
bool  criticalDry = false;
bool  mpuOk = false;
uint32_t shakeUntilMs = 0;          // face shudders until this time
uint32_t calibratedAtMs = 0;        // shows the CALIBRATED notice for a moment

uint8_t  currentExpression = EXPR_AUTO;
uint32_t expressionStartMs = 0;     // 0 = no change yet; animations restart on each tap
bool showSensorDataPage = false;
bool isScreenAwake = true;

uint32_t lastSensorMs = 0;
uint32_t lastFrameMs  = 0;

// ---------------- BUTTONS (non-blocking debounce) ----------------
struct Button {
    uint8_t  pin;
    bool     lastReading;
    bool     stableState;
    uint32_t lastChangeMs;
};
Button buttons[4];
const uint32_t DEBOUNCE_MS = 30;

void initButton(Button &b, uint8_t pin) {
    b.pin = pin;
    pinMode(pin, INPUT_PULLUP);
    b.lastReading = b.stableState = HIGH;
    b.lastChangeMs = 0;
}

// returns true once per press
bool buttonPressed(Button &b, uint32_t now) {
    bool reading = digitalRead(b.pin);
    if (reading != b.lastReading) {
        b.lastReading = reading;
        b.lastChangeMs = now;
    }
    if ((now - b.lastChangeMs) > DEBOUNCE_MS && reading != b.stableState) {
        b.stableState = reading;
        if (b.stableState == LOW) return true;
    }
    return false;
}

// ---------------- HELPERS ----------------
int dryLevel(int raw) {
    return HIGHER_IS_DRIER ? raw : (ADC_MAX - raw);
}

// pupil offset from the tilt sensor, scaled to +-maxPx
int tiltX(int maxPx) { return (int)roundf(eyeX * maxPx / MAX_PUPIL_X); }
int tiltY(int maxPx) { return (int)roundf(eyeY * maxPx / MAX_PUPIL_Y); }

// 0..1..0 triangle pulse used for blinking: one blink of length dur every period
float blinkAmount(uint32_t t, uint32_t period, uint32_t dur) {
    uint32_t q = t % period;
    if (q >= dur) return 0.0f;
    float h = dur / 2.0f;
    return (q < h) ? (q / h) : ((dur - q) / h);
}

float deadband(float v, float d) {
    return (fabsf(v) < d) ? 0.0f : v;
}

// Slide everything already drawn in the frame buffer by (dx, dy) pixels.
// Pixels pushed off the edge disappear; the vacated edge is filled with black.
// (SH1106 / SSD1306 page layout: byte = 8 vertical pixels, bit0 = top.)
void shiftBuffer(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    uint8_t *buf = u8g2.getBufferPtr();
    const int W = 128;
    const int PAGES = 8;

    if (dy != 0) {                                  // vertical: treat each column as 64 bits
        for (int x = 0; x < W; x++) {
            uint64_t col = 0;
            for (int p = 0; p < PAGES; p++) col |= (uint64_t)buf[p * W + x] << (8 * p);
            col = (dy > 0) ? (col << dy) : (col >> (-dy));
            for (int p = 0; p < PAGES; p++) buf[p * W + x] = (uint8_t)(col >> (8 * p));
        }
    }
    if (dx != 0) {                                  // horizontal: move bytes inside each page row
        for (int p = 0; p < PAGES; p++) {
            uint8_t *row = buf + p * W;
            if (dx > 0) { memmove(row + dx, row, W - dx); memset(row, 0, dx); }
            else        { int m = -dx; memmove(row, row + m, W - m); memset(row + W - m, 0, m); }
        }
    }
}

// Applies the MPU6050 reaction to whatever face was just drawn
void applyMotionShift(uint32_t now) {
    int sx = (int)roundf(eyeX * FACE_SHIFT_X_PER_PUPIL);   // tilt -> face slides
    int sy = (int)roundf(eyeY * FACE_SHIFT_Y_PER_PUPIL);
    if (now < shakeUntilMs) {                               // shaken -> shudder
        sx += ((now / 45) & 1) ? 6 : -6;
        sy += ((now / 70) & 1) ? 2 : -2;
    }
    sx = constrain(sx, -12, 12);
    sy = constrain(sy, -6, 6);
    shiftBuffer(sx, sy);
}

void nextExpression(uint32_t now) {
    // 0 (Auto) -> 1 -> ... -> 20 (Scuba) -> 1 -> 2 ...
    currentExpression = (currentExpression >= EXPR_COUNT) ? EXPR_HAPPY : currentExpression + 1;
    expressionStartMs = now ? now : 1;      // restart animation clock (never 0)
}

void readSensors() {
    // Moisture (smoothed to remove ADC noise)
    rawMoisture = analogRead(PLANT_SENSOR_ANALOG);
    moistureSmooth += ((float)rawMoisture - moistureSmooth) * 0.2f;

    int level = dryLevel((int)moistureSmooth);
    if (!criticalDry && level >= CRITICAL_LEVEL) criticalDry = true;
    else if (criticalDry && level < (CRITICAL_LEVEL - HYSTERESIS)) criticalDry = false;

    // Motion
    if (mpuOk) {
        sensors_event_t a, g, t;
        mpu.getEvent(&a, &g, &t);
        accelX = a.acceleration.x;
        accelY = a.acceleration.y;

        // Shake detection: total acceleration far from 1 g, or fast rotation
        float az      = a.acceleration.z;
        float accMag  = sqrtf(accelX * accelX + accelY * accelY + az * az);
        float gyroMag = sqrtf(g.gyro.x * g.gyro.x + g.gyro.y * g.gyro.y + g.gyro.z * g.gyro.z);
        if (fabsf(accMag - 9.81f) > SHAKE_ACCEL_DELTA || gyroMag > SHAKE_GYRO_RADS) {
            shakeUntilMs = millis() + SHAKE_REACT_MS;
        }
    }

    float tx = constrain(deadband(accelX - baseAccelX, TILT_DEADBAND) * TILT_GAIN, -MAX_PUPIL_X, MAX_PUPIL_X);
    float ty = constrain(deadband(accelY - baseAccelY, TILT_DEADBAND) * TILT_GAIN, -MAX_PUPIL_Y, MAX_PUPIL_Y);
    eyeX += (tx - eyeX) * 0.35f;    // smooth movement
    eyeY += (ty - eyeY) * 0.35f;
}

void handleButtons(uint32_t now) {
    if (buttonPressed(buttons[0], now)) {
        nextExpression(now);
    }
    if (buttonPressed(buttons[1], now)) {
        showSensorDataPage = !showSensorDataPage;
    }
    if (buttonPressed(buttons[2], now)) {
        isScreenAwake = !isScreenAwake;
        if (!isScreenAwake) {
            u8g2.clearBuffer();
            u8g2.sendBuffer();
        }
        u8g2.setPowerSave(isScreenAwake ? 0 : 1);   // real panel off = real power saving
    }
    if (buttonPressed(buttons[3], now)) {
        baseAccelX = accelX;                // current pose becomes "straight ahead"
        baseAccelY = accelY;
        eyeX = 0;                           // snap pupils to center right away
        eyeY = 0;
        calibratedAtMs = now ? now : 1;     // triggers the on-screen notice
    }
}

// ---------------- DRAWING PRIMITIVES ----------------
// NOTE: U8g2's drawTriangle() is FILLED, so outlines are made from lines.
// NOTE: U8g2 coordinates are unsigned - keep everything on-screen (>= 0).

void drawTriOutline(int x0, int y0, int x1, int y1, int x2, int y2) {
    u8g2.drawLine(x0, y0, x1, y1);
    u8g2.drawLine(x1, y1, x2, y2);
    u8g2.drawLine(x2, y2, x0, y0);
}

// Half-disc smile (flat edge on top, curved edge below). Occupies y: cy .. cy+r
void drawSmile(int cx, int cy, int r) {
    u8g2.drawDisc(cx, cy, r);
    u8g2.setDrawColor(0);                                   // erase the top half
    u8g2.drawBox(cx - r, cy - r, 2 * r + 1, r);
    u8g2.setDrawColor(1);
}

// Happy "^" eye, 2px thick
void drawCaretEye(int cx, int cy) {
    for (int d = 0; d < 2; d++) {
        u8g2.drawLine(cx - 9, cy + 5 + d, cx,     cy - 4 + d);
        u8g2.drawLine(cx,     cy - 4 + d, cx + 9, cy + 5 + d);
    }
}

// Sleepy closed eye, shaped like a shallow cup
void drawClosedEye(int cx, int cy) {
    u8g2.drawLine(cx - 10, cy,     cx - 6,  cy + 4);
    u8g2.drawLine(cx - 6,  cy + 4, cx + 6,  cy + 4);
    u8g2.drawLine(cx + 6,  cy + 4, cx + 10, cy);
}

// Heart with radius r (2 discs + filled triangle)
void drawHeart(int cx, int cy, int r) {
    u8g2.drawDisc(cx - r, cy, r);
    u8g2.drawDisc(cx + r, cy, r);
    u8g2.drawTriangle(cx - 2 * r + 1, cy + 2, cx + 2 * r - 1, cy + 2, cx, cy + 2 * r + 2);
}

// Eye outline with tracking pupil
void drawTrackingEye(int cx, int cy) {
    int ox = (int)roundf(eyeX);
    int oy = (int)roundf(eyeY);
    u8g2.drawCircle(cx, cy, 12);
    u8g2.drawDisc(cx + ox, cy + oy, 5);
}

// "«" double chevron (motion marks on the screen edges)
void drawChevrons(int x, int y) {
    for (int i = 0; i < 2; i++) {
        int xo = x + i * 5;
        u8g2.drawLine(xo + 3, y - 3, xo,     y);
        u8g2.drawLine(xo,     y,     xo + 3, y + 3);
    }
}

// ---- solid white eyes + black overlays (used by the new faces) ----

// White oval eye
void whiteEye(int cx, int cy, int rx, int ry) {
    if (ry < 1) ry = 1;
    u8g2.drawFilledEllipse(cx, cy, rx, ry, U8G2_DRAW_ALL);
}

// Black pupil on a white eye
void blackPupil(int x, int y, int r) {
    u8g2.setDrawColor(0);
    u8g2.drawDisc(x, y, r);
    u8g2.setDrawColor(1);
}

// Flat upper eyelid: black block covering the top `amt` px of the eye
void lidTop(int cx, int cy, int rx, int ry, int amt) {
    if (amt <= 0) return;
    u8g2.setDrawColor(0);
    u8g2.drawBox(cx - rx - 1, cy - ry - 1, 2 * rx + 3, amt + 1);
    u8g2.setDrawColor(1);
}

// Flat lower eyelid: black block covering the bottom `amt` px of the eye
void lidBottom(int cx, int cy, int rx, int ry, int amt) {
    if (amt <= 0) return;
    u8g2.setDrawColor(0);
    u8g2.drawBox(cx - rx - 1, cy + ry - amt + 1, 2 * rx + 3, amt + 2);
    u8g2.setDrawColor(1);
}

// Slanted upper lid. The lid is lower on the right side when lowRight = true.
// Angry:  left eye lowRight=true,  right eye lowRight=false
// Sad:    left eye lowRight=false, right eye lowRight=true
// brow = draws a white eyebrow line just above the lid
void lidSlant(int cx, int cy, int rx, int ry, int drop, bool lowRight, bool brow) {
    int top = cy - ry - 1;
    int L = cx - rx - 1;
    int R = cx + rx + 1;
    u8g2.setDrawColor(0);
    if (lowRight) u8g2.drawTriangle(L, top, R, top, R, top + drop);
    else          u8g2.drawTriangle(L, top, R, top, L, top + drop);
    u8g2.setDrawColor(1);
    if (brow) {
        int yl = lowRight ? top - 3 : top - 3 + drop;
        int yr = lowRight ? top - 3 + drop : top - 3;
        u8g2.drawLine(L, yl,     R, yr);
        u8g2.drawLine(L, yl + 1, R, yr + 1);
    }
}

// ---------------- ORIGINAL EXPRESSIONS ----------------

// Boot / default: reacts to the plant sensor
void drawAutoFace() {
    drawTrackingEye(40, 28);
    drawTrackingEye(88, 28);
    if (dryLevel((int)moistureSmooth) >= THIRSTY_LEVEL) {
        u8g2.drawLine(48, 52, 80, 52);                          // thirsty: flat
    } else {
        u8g2.drawRBox(54, 46, 20, 10, 4);                       // happy
    }
}

// 1: Happy - "^ ^" eyes, big open smile, little bounce
void drawHappy(uint32_t t) {
    int b = ((t % 800) < 400) ? 0 : 1;
    drawCaretEye(40, 26 + b);
    drawCaretEye(88, 26 + b);
    drawSmile(64, 42 + b, 11);
}

// 2: Wink - left eye winks, right eye tracks, twinkling sparkle
void drawWink(uint32_t t) {
    u8g2.drawLine(30, 28, 50, 28);
    u8g2.drawLine(30, 29, 50, 29);
    drawTrackingEye(88, 28);
    drawSmile(64, 45, 8);
    if ((t % 600) < 300) {                                      // sparkle
        u8g2.drawLine(112, 10, 112, 18);
        u8g2.drawLine(108, 14, 116, 14);
    }
}

// 3: Love - heart eyes that beat, small smile
void drawLove(uint32_t t) {
    int r = ((t % 900) < 160) ? 6 : 5;                          // heartbeat
    drawHeart(40, 22, r);
    drawHeart(88, 22, r);
    drawSmile(64, 44, 7);
}

// 4: Sleepy - closed eyes, breathing mouth, floating Zzz
void drawSleepy(uint32_t t) {
    drawClosedEye(40, 28);
    drawClosedEye(88, 28);

    float breath = 0.5f * (1.0f + sinf(2.0f * PI * (float)(t % 3000) / 3000.0f));
    u8g2.drawCircle(64, 50, 2 + (int)roundf(breath * 2.0f));

    for (int i = 0; i < 3; i++) {
        float p = (float)((t + i * 800) % 2400) / 2400.0f;
        if (p > 0.95f) continue;
        int x    = 100 + (int)(p * 14);
        int yTop = 26 - (int)(p * 20);
        if (p < 0.5f) {
            u8g2.setFont(FONT_SMALL);
            u8g2.drawStr(x, yTop + 8, "z");                     // baseline = top + ascent
        } else {
            u8g2.setFont(FONT_BIG);
            u8g2.drawStr(x, yTop + 12, "Z");
        }
    }
    u8g2.setFont(FONT_SMALL);
}

// ---------------- NEW EXPRESSIONS (from the reference sheet) ----------------

// 5: EYES_CRY - sad lids, watery pupils, tears falling, wobbling frown, little sobs
void drawCry(uint32_t t) {
    int y = EYE_Y + (((t % 700) < 120) ? 1 : 0);                // sob bounce
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, y, 14, 15);
        blackPupil(cx + tiltX(3), y - 1 + tiltY(3), 7);
        u8g2.drawDisc(cx - 3 + tiltX(3), y - 4 + tiltY(3), 1);                        // watery glint
        lidSlant(cx, y, 14, 15, 7, e == 1, false);
        for (int i = 0; i < 2; i++) {                           // two tears per eye
            float p = (float)((t + i * 600 + e * 300) % 1200) / 1200.0f;
            int tx = cx + (i ? 6 : -6);
            int ty = y + 16 + (int)(p * (60 - (y + 16)));
            if (ty < 61) {
                u8g2.drawDisc(tx, ty, 2);
                u8g2.drawTriangle(tx - 2, ty - 1, tx + 2, ty - 1, tx, ty - 5);
            }
        }
    }
    int w = (int)((t / 150) & 1);                               // quivering frown
    u8g2.drawLine(54, 58, 64, 53 + w);
    u8g2.drawLine(64, 53 + w, 74, 58);
}

// 6: EYES_DISTRESSED - trembling worried eyes, shaky pupils, sweat drop, zigzag mouth
void drawDistressed(uint32_t t) {
    int jx = (int)((t / 90) & 1);
    for (int e = 0; e < 2; e++) {
        int cx = (e ? EX_R : EX_L) + jx;
        whiteEye(cx, EYE_Y, 14, 15);
        int px = (int)((t / 70) % 3) - 1;
        int py = (int)((t / 110) % 3) - 1;
        blackPupil(cx + px + tiltX(5), EYE_Y + py + tiltY(5), 3);
        lidSlant(cx, EYE_Y, 14, 15, 9, e == 1, true);
    }
    float p = (float)(t % 1600) / 1600.0f;                      // sweat drop slides down
    if (p < 0.9f) {
        int sy = 14 + (int)(p * 34);
        u8g2.drawDisc(114, sy + 4, 3);
        u8g2.drawTriangle(111, sy + 3, 117, sy + 3, 114, sy - 3);
    }
    int px0 = 50, py0 = 54 + (((0 + (t / 150)) & 1) ? 2 : 0);   // zigzag mouth
    for (int i = 1; i <= 5; i++) {
        int px1 = 50 + i * 6;
        int py1 = 54 + (((i + (t / 150)) & 1) ? 2 : 0);
        u8g2.drawLine(px0, py0, px1, py1);
        px0 = px1; py0 = py1;
    }
}

// 7: EYES_GLARE - heavy angry lids, pupils slowly sliding, squint pulses
void drawGlare(uint32_t t) {
    int slide = (int)roundf(3.0f * sinf(2.0f * PI * (float)(t % 3000) / 3000.0f));
    int drop  = 12 + (((t % 2000) > 1700) ? 3 : 0);             // occasional squint
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, 32, 14, 14);
        blackPupil(cx + slide + tiltX(3), 34 + tiltY(3), 4);
        lidSlant(cx, 32, 14, 14, drop, e == 0, true);
    }
    u8g2.drawLine(52, 55, 76, 55);
}

// 8: EYES_MAD - shaking rage: steep brows, tiny pupils, gritted teeth, pulsing anger mark
void drawMad(uint32_t t) {
    int sx = ((t / 60) & 1) ? 1 : 0;                            // shake
    for (int e = 0; e < 2; e++) {
        int cx = (e ? EX_R : EX_L) + sx;
        whiteEye(cx, 32, 14, 13);
        blackPupil(cx + tiltX(5), 35 + tiltY(3), 3);
        lidSlant(cx, 32, 14, 13, 14, e == 0, true);
    }
    // gritted teeth
    u8g2.drawFrame(48 + sx, 50, 32, 10);
    u8g2.drawVLine(56 + sx, 50, 10);
    u8g2.drawVLine(64 + sx, 50, 10);
    u8g2.drawVLine(72 + sx, 50, 10);
    u8g2.drawHLine(48 + sx, 55, 32);
    // pulsing anger mark (corner brackets)
    int s = 4 + (int)((t / 200) & 1);
    int ax = 116, ay = 12;
    u8g2.drawLine(ax - s, ay - 1, ax - 1, ay - 1); u8g2.drawLine(ax - 1, ay - 1, ax - 1, ay - s);
    u8g2.drawLine(ax + s, ay - 1, ax + 1, ay - 1); u8g2.drawLine(ax + 1, ay - 1, ax + 1, ay - s);
    u8g2.drawLine(ax - s, ay + 1, ax - 1, ay + 1); u8g2.drawLine(ax - 1, ay + 1, ax - 1, ay + s);
    u8g2.drawLine(ax + s, ay + 1, ax + 1, ay + 1); u8g2.drawLine(ax + 1, ay + 1, ax + 1, ay + s);
}

// 9: EYES_GLASSES - round glasses, tracking pupils, glint sweeping across the lenses, blinks
void drawGlasses(uint32_t t) {
    float closeAmt = blinkAmount(t, 3200, 160);
    int   ry = (int)(10.0f * (1.0f - closeAmt));
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        u8g2.drawCircle(cx, EYE_Y, 17);                         // thick frame
        u8g2.drawCircle(cx, EYE_Y, 16);
        whiteEye(cx, EYE_Y, 9, ry);
        if (ry > 4) blackPupil(cx + tiltX(4), EYE_Y + tiltY(4), 3);
        uint32_t q = t % 2400;                                  // glint sweep (first 700 ms)
        if (q < 700) {
            int dx = -7 + (int)(14 * q / 700);
            u8g2.drawLine(cx + dx - 3, EYE_Y + 7, cx + dx + 3, EYE_Y - 7);
            u8g2.drawLine(cx + dx - 2, EYE_Y + 7, cx + dx + 4, EYE_Y - 7);
        }
    }
    u8g2.drawLine(53, 26, 75, 26);                              // bridge
    u8g2.drawLine(53, 27, 75, 27);
    u8g2.drawLine(19, 28, 6, 24);                               // temples
    u8g2.drawLine(109, 28, 122, 24);
    drawSmile(64, 52, 6);
}

// 10: EYES_SLEEP - closed eyes, head nodding, snore bubble that grows and pops
void drawSleepFace(uint32_t t) {
    int ny = (int)roundf(2.5f * sinf(2.0f * PI * (float)(t % 4000) / 4000.0f));  // nod
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        u8g2.drawEllipse(cx, 28 + ny, 13, 7, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        u8g2.drawEllipse(cx, 28 + ny, 13, 6, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
    }
    u8g2.drawFilledEllipse(62, 50 + ny, 3, 2, U8G2_DRAW_ALL);   // open mouth
    uint32_t q = t % 3200;
    if (q < 2600) {                                             // bubble inflates
        int r = 1 + (int)(q * 9 / 2600);
        u8g2.drawCircle(66 + r, 48 + ny, r);
    } else if (q < 2780) {                                      // pop
        u8g2.drawLine(78, 40 + ny, 81, 37 + ny);
        u8g2.drawLine(80, 48 + ny, 84, 48 + ny);
        u8g2.drawLine(78, 56 + ny, 81, 59 + ny);
        u8g2.drawLine(74, 38 + ny, 74, 35 + ny);
    }
}

// 11: BLINK_UPPER - big eyes with upper lids that close in quick double blinks
void drawBlinkUpper(uint32_t t) {
    float c = max(blinkAmount(t, 2600, 300), blinkAmount(t + 500, 2600, 300));
    int amt = (int)(c * 34.0f);                                 // eye height = 2*16 + 2
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, EYE_Y, 14, 16);
        blackPupil(cx + tiltX(5), EYE_Y + 2 + tiltY(4), 6);
        lidTop(cx, EYE_Y, 14, 16, amt);
        if (amt > 0) u8g2.drawHLine(cx - 14, EYE_Y - 16 + amt, 29);   // lid edge line
    }
}

// 12: EYES_TIRED - eyelids sink and sag, jerk awake, then a big yawn
void drawTired(uint32_t t) {
    uint32_t q = t % 5000;
    bool yawn = (q >= 3700 && q < 4600);
    int lid;
    if (q < 3500)      lid = 6 + (int)(q * 14 / 3500);          // slowly drooping
    else if (q < 3700) lid = 20 - (int)((q - 3500) * 16 / 200); // snaps awake
    else if (yawn)     lid = 24;                                // eyes squeezed in the yawn
    else               lid = 4 + (int)((q - 4600) * 2 / 400);
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, 32, 14, 15);
        blackPupil(cx + tiltX(5), 38 + tiltY(3), 5);
        lidTop(cx, 32, 14, 15, lid);
        int yy = 32 - 15 - 1 + lid;                             // outer-corner sag
        int L = cx - 15, R = cx + 15;
        u8g2.setDrawColor(0);
        if (e == 0) u8g2.drawTriangle(L, yy, R, yy, L, yy + 4);
        else        u8g2.drawTriangle(L, yy, R, yy, R, yy + 4);
        u8g2.setDrawColor(1);
    }
    if (yawn) {
        float k = sinf(PI * (float)(q - 3700) / 900.0f);
        u8g2.drawFilledEllipse(64, 53, 5, 2 + (int)(7.0f * k), U8G2_DRAW_ALL);
    } else {
        u8g2.drawLine(58, 54, 70, 54);
    }
}

// 13: EYES_NIGHT - wide night-watch eyes scanning side to side, moon and twinkling stars
void drawNight(uint32_t t) {
    int scan = (int)roundf(5.0f * sinf(2.0f * PI * (float)(t % 3600) / 3600.0f));
    float closeAmt = blinkAmount(t, 3000, 160);
    int   ry = (int)(13.0f * (1.0f - closeAmt));
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, 32, 12, ry);
        if (ry > 5) blackPupil(cx + scan + tiltX(3), 32 + tiltY(3), 4);
    }
    u8g2.drawDisc(114, 10, 7);                                  // crescent moon
    u8g2.setDrawColor(0);
    u8g2.drawDisc(117, 8, 6);
    u8g2.setDrawColor(1);
    const int sx[3] = {14, 6, 118};                             // twinkling stars
    const int sy[3] = {10, 44, 50};
    for (int k = 0; k < 3; k++) {
        if (((t + k * 300) % 1200) < 700) {
            u8g2.drawHLine(sx[k] - 2, sy[k], 5);
            u8g2.drawVLine(sx[k], sy[k] - 2, 5);
        }
    }
    u8g2.drawLine(60, 54, 68, 54);
}

// 14: UPPER_LIDS - unimpressed half-lidded eyes that do a slow eye-roll
void drawUpperLids(uint32_t t) {
    uint32_t q = t % 3600;
    int px = 0, py = 3;                                         // resting: looking down-center
    if (q < 1400) {
        float th = PI / 2.0f + 2.0f * PI * (float)q / 1400.0f;  // one full roll, ends at rest
        px = (int)roundf(5.0f * cosf(th));
        py = (int)roundf(3.0f * sinf(th));
    }
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, 32, 14, 15);
        blackPupil(cx + px + tiltX(3), 33 + py + tiltY(2), 5);
        lidTop(cx, 32, 14, 15, 14);
    }
    u8g2.drawLine(54, 54, 74, 54);
}

// 15: LOWER_LIDS - smug squint, lower lids rising and falling, eyes looking up
void drawLowerLids(uint32_t t) {
    int amt = 10 + (int)roundf(6.0f * sinf(2.0f * PI * (float)(t % 2400) / 2400.0f));
    for (int e = 0; e < 2; e++) {
        int cx = e ? EX_R : EX_L;
        whiteEye(cx, EYE_Y, 14, 16);
        blackPupil(cx + tiltX(4), 26 + tiltY(3), 6);
        lidBottom(cx, EYE_Y, 14, 16, amt);
    }
    drawSmile(64, 50, 7);
}

// 16: EYES_BLANK - blank white oval stare, drifting around, occasional blink
void drawBlank(uint32_t t) {
    int sx = (int)roundf(4.0f * sinf(2.0f * PI * (float)(t % 5000) / 5000.0f));
    float closeAmt = blinkAmount(t, 3400, 200);
    int   ry = (int)(17.0f * (1.0f - closeAmt));
    whiteEye(EX_L + sx, EYE_Y, 11, ry);
    whiteEye(EX_R + sx, EYE_Y, 11, ry);
}

// ---- word faces ----
// mode 0 = HELLO (typed in, then wave), 1 = BYE (sway), 2 = WHAT (shake + bouncing "?")
void drawAnimatedWord(const char *s, uint32_t t, uint8_t mode) {
    u8g2.setFont(FONT_WORD);
    int n = strlen(s);
    int widths[8];
    int total = 0;
    char buf[2] = {0, 0};
    for (int i = 0; i < n; i++) {
        buf[0] = s[i];
        widths[i] = u8g2.getStrWidth(buf);
        total += widths[i];
    }
    int x = (128 - total) / 2;
    int shown = n;
    if (mode == 0) {                                            // typewriter reveal
        shown = (int)(t / 180) + 1;
        if (shown > n) shown = n;
    }
    for (int i = 0; i < shown; i++) {
        int dx = 0, dy = 0;
        float ph = 2.0f * PI * (float)(t % 1600) / 1600.0f - i * 0.8f;
        if (mode == 0) {
            if (t > (uint32_t)(180 * n + 300)) {                // wave once fully typed
                float w = sinf(ph);
                if (w > 0) dy = -(int)roundf(6.0f * w);
            }
        } else if (mode == 1) {
            dx = (int)roundf(3.0f * sinf(ph));                  // waving goodbye
        } else {
            if ((t % 2400) < 250) dx = ((t / 50) & 1) ? 2 : -2; // confused shake
            if (i == n - 1)
                dy = -(int)(7.0f * fabsf(sinf(2.0f * PI * (float)(t % 1000) / 1000.0f)));
        }
        buf[0] = s[i];
        u8g2.drawStr(x + dx, 50 + dy, buf);
        x += widths[i];
    }
    u8g2.setFont(FONT_SMALL);
}

// ---------------- SCUBA CAT (kept from the previous code, always last) ----------------
// Diving mask, wide stare, regulator + hose coil.
// The whole cat sways side to side (eyes look where it is heading),
// with a tiny breathing bob, periodic blink, and chevron marks on both sides.
void drawScubaCat(uint32_t t) {
    const uint32_t SWAY_MS = 3000;                                // one full left-right-left
    float ph   = (float)(t % SWAY_MS) / (float)SWAY_MS;
    int   cx   = 64 + (int)roundf(16.0f * sinf(2.0f * PI * ph));  // cat center: 48 .. 80
    int   look = (int)roundf(cosf(2.0f * PI * ph));               // -1 left, 0, +1 right
    int   y    = ((t % 1200) < 600) ? 0 : 1;                      // breathing bob

    // Side chevrons (fixed on screen edges)
    drawChevrons(8, 26);
    drawChevrons(8, 42);
    drawChevrons(116, 26);
    drawChevrons(116, 42);

    // Ears + head
    drawTriOutline(cx - 22, 22 + y, cx - 20, 4 + y, cx - 8, 14 + y);
    drawTriOutline(cx + 22, 22 + y, cx + 20, 4 + y, cx + 8, 14 + y);
    u8g2.drawRFrame(cx - 23, 12 + y, 46, 40, 16);

    // Diving mask: frame, strap, lenses
    u8g2.drawRFrame(cx - 18, 22 + y, 36, 16, 6);
    u8g2.drawLine(cx - 23, 30 + y, cx - 18, 30 + y);
    u8g2.drawLine(cx + 18, 30 + y, cx + 23, 30 + y);
    u8g2.drawCircle(cx - 9, 30 + y, 5);
    u8g2.drawCircle(cx + 9, 30 + y, 5);

    // Wide stare (blink every 3.6 s)
    if ((t % 3600) < 120) {
        u8g2.drawLine(cx - 13, 30 + y, cx - 5,  30 + y);
        u8g2.drawLine(cx + 5,  30 + y, cx + 13, 30 + y);
    } else {
        u8g2.drawDisc(cx - 9 + look + tiltX(1), 30 + y + tiltY(1), 3);
        u8g2.drawDisc(cx + 9 + look + tiltX(1), 30 + y + tiltY(1), 3);
    }

    // Nose + whiskers
    u8g2.drawTriangle(cx - 3, 40 + y, cx + 3, 40 + y, cx, 43 + y);   // filled
    u8g2.drawLine(cx - 23, 41 + y, cx - 30, 39 + y);
    u8g2.drawLine(cx - 23, 45 + y, cx - 30, 47 + y);
    u8g2.drawLine(cx + 23, 41 + y, cx + 30, 39 + y);
    u8g2.drawLine(cx + 23, 45 + y, cx + 30, 47 + y);

    // Regulator + hose coil
    u8g2.drawRBox(cx - 6, 46 + y, 12, 6, 2);
    u8g2.drawLine(cx + 6, 48 + y, cx + 9, 48 + y);
    u8g2.drawCircle(cx + 13, 49 + y, 4);
}

// Short "blink" shown for a moment whenever the expression changes
void drawTransitionBlink() {
    u8g2.drawLine(30, 28, 50, 28);
    u8g2.drawLine(78, 28, 98, 28);
    u8g2.drawLine(56, 50, 72, 50);
}

void drawFace(uint32_t now) {
    if (expressionStartMs != 0 && (now - expressionStartMs) < TRANSITION_MS) {
        drawTransitionBlink();
        return;
    }
    uint32_t t = now - expressionStartMs;   // animation time since last tap

    switch (currentExpression) {
        case EXPR_HAPPY:       drawHappy(t);                 break;
        case EXPR_WINK:        drawWink(t);                  break;
        case EXPR_LOVE:        drawLove(t);                  break;
        case EXPR_SLEEPY:      drawSleepy(t);                break;
        case EXPR_CRY:         drawCry(t);                   break;
        case EXPR_DISTRESSED:  drawDistressed(t);            break;
        case EXPR_GLARE:       drawGlare(t);                 break;
        case EXPR_MAD:         drawMad(t);                   break;
        case EXPR_GLASSES:     drawGlasses(t);               break;
        case EXPR_SLEEP:       drawSleepFace(t);             break;
        case EXPR_BLINK_UPPER: drawBlinkUpper(t);            break;
        case EXPR_TIRED:       drawTired(t);                 break;
        case EXPR_NIGHT:       drawNight(t);                 break;
        case EXPR_UPPER_LIDS:  drawUpperLids(t);             break;
        case EXPR_LOWER_LIDS:  drawLowerLids(t);             break;
        case EXPR_BLANK:       drawBlank(t);                 break;
        case EXPR_HELLO:       drawAnimatedWord("HELLO", t, 0);  break;
        case EXPR_BYE:         drawAnimatedWord("BYE", t, 1);    break;
        case EXPR_WHAT:        drawAnimatedWord("What?", t, 2);  break;
        case EXPR_SCUBA:       drawScubaCat(t);              break;
        case EXPR_AUTO:
        default:               drawAutoFace();               break;
    }
}

// ---------------- OTHER SCREENS ----------------
void drawDataPage() {
    char buf[32];
    int level = dryLevel((int)moistureSmooth);
    const char *state = criticalDry ? "DRY!" : (level >= THIRSTY_LEVEL ? "Thirsty" : "OK");

    u8g2.setFont(FONT_SMALL);                       // 10 px line spacing, baseline = 8 + row*10
    u8g2.drawStr(0, 8, "--- PLANT MONITOR ---");

    snprintf(buf, sizeof(buf), "Moisture: %d", rawMoisture);
    u8g2.drawStr(0, 18, buf);

    // Button test: 4 squares = buttons 1..4, filled while that button is held down
    for (int i = 0; i < 4; i++) {
        int bx = 94 + i * 9;
        if (buttons[i].lastReading == LOW) u8g2.drawBox(bx, 11, 7, 7);
        else                               u8g2.drawFrame(bx, 11, 7, 7);
    }

    snprintf(buf, sizeof(buf), "State: %s", state);
    u8g2.drawStr(0, 28, buf);

    if (mpuOk) snprintf(buf, sizeof(buf), "Accel X: %.2f", accelX);
    else       snprintf(buf, sizeof(buf), "Accel X: NO MPU");
    u8g2.drawStr(0, 38, buf);

    if (mpuOk) snprintf(buf, sizeof(buf), "Accel Y: %.2f", accelY);
    else       snprintf(buf, sizeof(buf), "Accel Y: NO MPU");
    u8g2.drawStr(0, 48, buf);

    if (currentExpression == EXPR_AUTO) {
        snprintf(buf, sizeof(buf), "Mood: %s", MOOD_NAMES[currentExpression]);
    } else {
        snprintf(buf, sizeof(buf), "Mood: %s %d/%d", MOOD_NAMES[currentExpression],
                 currentExpression, EXPR_COUNT);
    }
    u8g2.drawStr(0, 58, buf);
}

void drawWarning(uint32_t now) {
    // Double-line warning triangle (centered)
    drawTriOutline(64, 2, 38, 44, 90, 44);
    drawTriOutline(64, 6, 43, 42, 85, 42);

    // Exclamation mark (3 px wide so it is visible)
    u8g2.drawBox(63, 16, 3, 15);
    u8g2.drawBox(63, 34, 3, 3);

    // Blinking text, centered
    if ((now % 1000) < 500) {
        u8g2.setFont(FONT_SMALL);
        const char *msg = "WATER ME!";
        int w = u8g2.getStrWidth(msg);
        u8g2.drawStr((128 - w) / 2, 61, msg);
    }
}

// Brief on-screen confirmation after pressing the recalibrate button
void drawCalibrationNotice(uint32_t now) {
    if (calibratedAtMs == 0 || (now - calibratedAtMs) > 800) return;
    const char *msg = mpuOk ? "CALIBRATED" : "NO MPU!";
    u8g2.setFont(FONT_SMALL);
    int w = u8g2.getStrWidth(msg);
    int x = (128 - w) / 2;
    u8g2.setDrawColor(0);
    u8g2.drawBox(x - 4, 51, w + 8, 13);
    u8g2.setDrawColor(1);
    u8g2.drawFrame(x - 4, 51, w + 8, 13);
    u8g2.drawStr(x, 61, msg);
}

void drawFrame(uint32_t now) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    if (showSensorDataPage)  drawDataPage();
    else if (criticalDry)    drawWarning(now);   // dry warning overrides every face
    else {
        drawFace(now);
        applyMotionShift(now);               // tilt / shake reaction for EVERY face
    }
    drawCalibrationNotice(now);
    u8g2.sendBuffer();
}

// ---------------- SETUP / LOOP ----------------
void setup() {
    DEBUG_BEGIN();

    initButton(buttons[0], BUTTON_1);
    initButton(buttons[1], BUTTON_2);
    initButton(buttons[2], BUTTON_3);
    initButton(buttons[3], BUTTON_4);

    // Start I2C on the custom pins BEFORE u8g2.begin(), so U8g2 reuses this bus
#if defined(ARDUINO_ARCH_ESP32)
    analogReadResolution(12);
    Wire.begin(XIAO_SDA, XIAO_SCL);
#else
    Wire.begin();   // non-ESP32 boards: wire to the board's default I2C pins
#endif
    Wire.setClock(400000);

    // U8g2's begin() cannot tell if a display is attached, so probe the address first
    Wire.beginTransmission(OLED_ADDR);
    if (Wire.endTransmission() != 0) {
        DEBUG_PRINTLN(F("OLED not found! Check wiring / address (0x3C or 0x3D)."));
        for (;;) delay(1000);       // delay keeps the watchdog happy
    }

    u8g2.setI2CAddress(OLED_ADDR << 1);     // U8g2 uses the 8-bit address
    u8g2.begin();
    u8g2.setBusClock(400000);
    u8g2.setFontMode(1);                    // transparent text
    u8g2.setDrawColor(1);
    u8g2.setFont(FONT_SMALL);
    u8g2.clearBuffer();
    u8g2.sendBuffer();

    // Try both MPU6050 addresses (0x68, or 0x69 if AD0 is tied high), a few times
    for (int attempt = 0; attempt < 3 && !mpuOk; attempt++) {
        mpuOk = mpu.begin(0x68, &Wire) || mpu.begin(0x69, &Wire);
        if (!mpuOk) delay(100);
    }
    DEBUG_PRINTLN(mpuOk ? F("MPU6050 OK") : F("MPU6050 NOT FOUND"));
    if (mpuOk) {
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    } else {
        DEBUG_PRINTLN(F("MPU6050 not found - eyes will stay centered."));
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "MPU6050 not found");
        u8g2.drawStr(0, 22, "Eyes fixed.");
        u8g2.sendBuffer();
        delay(2000);
    }

    moistureSmooth = analogRead(PLANT_SENSOR_ANALOG);   // seed the filter
    readSensors();
    baseAccelX = accelX;                                // auto-center at boot
    baseAccelY = accelY;
}

void loop() {
    uint32_t now = millis();

    handleButtons(now);

    if (now - lastSensorMs >= 20) {
        lastSensorMs = now;
        readSensors();
    }

    if (isScreenAwake && (now - lastFrameMs >= 33)) {   // ~30 FPS
        lastFrameMs = now;
        drawFrame(now);
    }
}
