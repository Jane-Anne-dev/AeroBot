// ============================================================
//  Plant Monitor Face  -  SH1106 OLED (128 x 64, 1.3")
//  Board : Seeed XIAO ESP32 series (C3 / S3 / C6), 12-bit ADC
//  Libs  : Adafruit SH110X, Adafruit GFX, Adafruit MPU6050,
//          Adafruit Unified Sensor
//
//  Mood button cycle (starts in AUTO at boot, never returns to it):
//     Tap 1 = Happy     Tap 2 = Wink      Tap 3 = Love
//     Tap 4 = Sleepy    Tap 5 = Scuba Cat (animated loop)
//     Tap 6 = back to Happy ... and so on forever
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ---------------- DISPLAY ----------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C      // try 0x3D if the screen stays blank
#define COLOR_ON      SH110X_WHITE
#define COLOR_OFF     SH110X_BLACK

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MPU6050 mpu;

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
const float TILT_GAIN   = 0.6f;     // pixels per m/s^2 (10 m/s^2 -> 6 px)

// ---------------- EXPRESSIONS ----------------
enum Expression : uint8_t {
    EXPR_AUTO   = 0,   // boot state: sensor-driven face (not part of the 5-cycle)
    EXPR_HAPPY  = 1,
    EXPR_WINK   = 2,
    EXPR_LOVE   = 3,
    EXPR_SLEEPY = 4,
    EXPR_SCUBA  = 5
};
const uint8_t EXPR_COUNT = 5;                   // number of cycling expressions
const char *const MOOD_NAMES[] = {"Auto", "Happy", "Wink", "Love", "Sleepy", "ScubaCat"};
const uint32_t TRANSITION_MS = 120;             // quick "blink" between expressions

// ---------------- STATE ----------------
float accelX = 0, accelY = 0;       // raw accelerometer
float baseAccelX = 0, baseAccelY = 0;
float eyeX = 0, eyeY = 0;           // smoothed pupil offset
float moistureSmooth = 0;
int   rawMoisture = 0;
bool  criticalDry = false;
bool  mpuOk = false;

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
void oledPower(bool on) {
    display.oled_command(on ? SH110X_DISPLAYON : SH110X_DISPLAYOFF);
}

int dryLevel(int raw) {
    return HIGHER_IS_DRIER ? raw : (ADC_MAX - raw);
}

void nextExpression(uint32_t now) {
    // 0 (Auto) -> 1 -> 2 -> 3 -> 4 -> 5 -> 1 -> 2 ...
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
    }

    float tx = constrain((accelX - baseAccelX) * TILT_GAIN, -MAX_PUPIL_X, MAX_PUPIL_X);
    float ty = constrain((accelY - baseAccelY) * TILT_GAIN, -MAX_PUPIL_Y, MAX_PUPIL_Y);
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
            display.clearDisplay();
            display.display();
        }
        oledPower(isScreenAwake);   // real panel off = real power saving
    }
    if (buttonPressed(buttons[3], now)) {
        baseAccelX = accelX;
        baseAccelY = accelY;
    }
}

// ---------------- DRAWING PRIMITIVES ----------------

// Half-disc smile (flat edge on top, curved edge below). Occupies y: cy .. cy+r
void drawSmile(int cx, int cy, int r) {
    display.fillCircle(cx, cy, r, COLOR_ON);
    display.fillRect(cx - r, cy - r, 2 * r + 1, r, COLOR_OFF);
}

// Happy "^" eye, 2px thick
void drawCaretEye(int cx, int cy) {
    for (int d = 0; d < 2; d++) {
        display.drawLine(cx - 9, cy + 5 + d, cx,     cy - 4 + d, COLOR_ON);
        display.drawLine(cx,     cy - 4 + d, cx + 9, cy + 5 + d, COLOR_ON);
    }
}

// Sleepy closed eye, shaped like a shallow cup
void drawClosedEye(int cx, int cy) {
    display.drawLine(cx - 10, cy,     cx - 6,  cy + 4, COLOR_ON);
    display.drawLine(cx - 6,  cy + 4, cx + 6,  cy + 4, COLOR_ON);
    display.drawLine(cx + 6,  cy + 4, cx + 10, cy,     COLOR_ON);
}

// Heart with radius r (2 circles + triangle)
void drawHeart(int cx, int cy, int r) {
    display.fillCircle(cx - r, cy, r, COLOR_ON);
    display.fillCircle(cx + r, cy, r, COLOR_ON);
    display.fillTriangle(cx - 2 * r + 1, cy + 2, cx + 2 * r - 1, cy + 2, cx, cy + 2 * r + 2, COLOR_ON);
}

// Eye outline with tracking pupil
void drawTrackingEye(int cx, int cy) {
    int ox = (int)roundf(eyeX);
    int oy = (int)roundf(eyeY);
    display.drawCircle(cx, cy, 12, COLOR_ON);
    display.fillCircle(cx + ox, cy + oy, 5, COLOR_ON);
}

// ---------------- EXPRESSIONS ----------------

// Boot / default: reacts to the plant sensor
void drawAutoFace() {
    drawTrackingEye(40, 28);
    drawTrackingEye(88, 28);
    if (dryLevel((int)moistureSmooth) >= THIRSTY_LEVEL) {
        display.drawLine(48, 52, 80, 52, COLOR_ON);              // thirsty: flat
    } else {
        display.fillRoundRect(54, 46, 20, 10, 4, COLOR_ON);      // happy
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
    display.drawLine(30, 28, 50, 28, COLOR_ON);
    display.drawLine(30, 29, 50, 29, COLOR_ON);
    drawTrackingEye(88, 28);
    drawSmile(64, 45, 8);
    if ((t % 600) < 300) {                                       // sparkle
        display.drawLine(112, 10, 112, 18, COLOR_ON);
        display.drawLine(108, 14, 116, 14, COLOR_ON);
    }
}

// 3: Love - heart eyes that beat, small smile
void drawLove(uint32_t t) {
    int r = ((t % 900) < 160) ? 6 : 5;                           // heartbeat
    drawHeart(40, 22, r);
    drawHeart(88, 22, r);
    drawSmile(64, 44, 7);
}

// 4: Sleepy - closed eyes, breathing mouth, floating Zzz
void drawSleepy(uint32_t t) {
    drawClosedEye(40, 28);
    drawClosedEye(88, 28);

    float breath = 0.5f * (1.0f + sinf(2.0f * PI * (float)(t % 3000) / 3000.0f));
    display.drawCircle(64, 50, 2 + (int)roundf(breath * 2.0f), COLOR_ON);

    display.setTextColor(COLOR_ON);
    for (int i = 0; i < 3; i++) {
        float p = (float)((t + i * 800) % 2400) / 2400.0f;
        if (p > 0.95f) continue;
        display.setTextSize(p < 0.5f ? 1 : 2);
        display.setCursor(100 + (int)(p * 14), 26 - (int)(p * 20));
        display.print(p < 0.5f ? 'z' : 'Z');
    }
    display.setTextSize(1);
}

// 5: Scuba Cat - diving mask, wide stare, regulator + hose,
//    gentle breathing bob, periodic blink, looping rising bubbles
void drawScubaCat(uint32_t t) {
    const uint32_t BREATH_MS = 2400;
    float ph = (float)(t % BREATH_MS) / (float)BREATH_MS;
    int y = (int)roundf(1.5f * sinf(2.0f * PI * ph));            // -1, 0, +1 bob

    // Ears + head
    display.drawTriangle(23, 26 + y, 25, 6 + y, 40, 17 + y, COLOR_ON);
    display.drawTriangle(75, 26 + y, 73, 6 + y, 58, 17 + y, COLOR_ON);
    display.drawRoundRect(20, 16 + y, 58, 44, 18, COLOR_ON);

    // Diving mask: frame, strap, two lenses
    display.drawRoundRect(26, 27 + y, 46, 18, 6, COLOR_ON);
    display.drawLine(21, 36 + y, 26, 36 + y, COLOR_ON);
    display.drawLine(72, 36 + y, 77, 36 + y, COLOR_ON);
    display.drawCircle(38, 36 + y, 6, COLOR_ON);
    display.drawCircle(60, 36 + y, 6, COLOR_ON);

    // Wide stare with a blink every 3.6 s
    if ((t % 3600) < 120) {
        display.drawLine(33, 36 + y, 43, 36 + y, COLOR_ON);
        display.drawLine(55, 36 + y, 65, 36 + y, COLOR_ON);
    } else {
        display.fillCircle(38, 36 + y, 3, COLOR_ON);
        display.fillCircle(60, 36 + y, 3, COLOR_ON);
        display.drawPixel(37, 35 + y, COLOR_OFF);                // eye glint
        display.drawPixel(59, 35 + y, COLOR_OFF);
    }

    // Nose, whiskers
    display.fillTriangle(46, 47 + y, 52, 47 + y, 49, 50 + y, COLOR_ON);
    display.drawLine(20, 46 + y, 10, 44 + y, COLOR_ON);
    display.drawLine(20, 50 + y, 10, 52 + y, COLOR_ON);
    display.drawLine(78, 46 + y, 86, 44 + y, COLOR_ON);
    display.drawLine(78, 50 + y, 86, 52 + y, COLOR_ON);

    // Regulator mouthpiece + hose to the exhaust port
    display.fillRoundRect(43, 52 + y, 12, 6, 3, COLOR_ON);
    display.drawLine(55, 55 + y, 70, 60, COLOR_ON);
    display.drawLine(70, 60, 98, 57, COLOR_ON);
    display.fillRect(98, 55, 5, 5, COLOR_ON);

    // Bubbles: 5 staggered, rise + wobble + grow, seamless loop
    for (int i = 0; i < 5; i++) {
        float p = (float)((t + i * 480) % 2400) / 2400.0f;
        int by = 52 - (int)(p * 48.0f);
        int bx = 100 + (int)(sinf(p * 4.0f * PI + i * 1.3f) * 4.0f) + (int)(p * 8.0f);
        int br = 1 + (int)(p * 3.0f);
        display.drawCircle(bx, by, br, COLOR_ON);
    }
}

// Short "blink" shown for a moment whenever the expression changes
void drawTransitionBlink() {
    display.drawLine(30, 28, 50, 28, COLOR_ON);
    display.drawLine(78, 28, 98, 28, COLOR_ON);
    display.drawLine(56, 50, 72, 50, COLOR_ON);
}

void drawFace(uint32_t now) {
    if (expressionStartMs != 0 && (now - expressionStartMs) < TRANSITION_MS) {
        drawTransitionBlink();
        return;
    }
    uint32_t t = now - expressionStartMs;   // animation time since last tap

    switch (currentExpression) {
        case EXPR_HAPPY:  drawHappy(t);    break;
        case EXPR_WINK:   drawWink(t);     break;
        case EXPR_LOVE:   drawLove(t);     break;
        case EXPR_SLEEPY: drawSleepy(t);   break;
        case EXPR_SCUBA:  drawScubaCat(t); break;
        case EXPR_AUTO:
        default:          drawAutoFace();  break;
    }
}

// ---------------- OTHER SCREENS ----------------
void drawDataPage() {
    int level = dryLevel((int)moistureSmooth);
    const char *state = criticalDry ? "DRY!" : (level >= THIRSTY_LEVEL ? "Thirsty" : "OK");

    display.setTextSize(1);
    display.setTextColor(COLOR_ON);
    display.setCursor(0, 0);
    display.println(F("--- PLANT MONITOR ---"));
    display.print(F("Moisture: ")); display.println(rawMoisture);
    display.print(F("State: "));    display.println(state);
    display.print(F("Accel X: "));  display.println(accelX);
    display.print(F("Accel Y: "));  display.println(accelY);
    display.setCursor(0, 56);
    display.print(F("Mood: ")); display.print(MOOD_NAMES[currentExpression]);
    if (currentExpression != EXPR_AUTO) {
        display.print(' ');
        display.print(currentExpression);
        display.print('/');
        display.print(EXPR_COUNT);
    }
}

void drawWarning(uint32_t now) {
    // Double-line warning triangle (centered)
    display.drawTriangle(64, 2, 38, 44, 90, 44, COLOR_ON);
    display.drawTriangle(64, 6, 43, 42, 85, 42, COLOR_ON);

    // Exclamation mark (2-3 px wide so it is visible)
    display.fillRect(63, 16, 3, 15, COLOR_ON);
    display.fillRect(63, 34, 3, 3, COLOR_ON);

    // Blinking text, centered: 9 chars * 6 px = 54 px
    if ((now % 1000) < 500) {
        display.setTextSize(1);
        display.setTextColor(COLOR_ON);
        display.setCursor(37, 53);
        display.print(F("WATER ME!"));
    }
}

void drawFrame(uint32_t now) {
    display.clearDisplay();
    if (showSensorDataPage)  drawDataPage();
    else if (criticalDry)    drawWarning(now);   // dry warning overrides every face
    else                     drawFace(now);
    display.display();
}

// ---------------- SETUP / LOOP ----------------
void setup() {
    Serial.begin(115200);

    initButton(buttons[0], BUTTON_1);
    initButton(buttons[1], BUTTON_2);
    initButton(buttons[2], BUTTON_3);
    initButton(buttons[3], BUTTON_4);

#if defined(ARDUINO_ARCH_ESP32)
    analogReadResolution(12);
    Wire.begin(XIAO_SDA, XIAO_SCL);
#else
    Wire.begin();   // non-ESP32 boards: wire to the board's default I2C pins
#endif
    Wire.setClock(400000);

    if (!display.begin(OLED_ADDR, true)) {   // true = reset controller
        Serial.println(F("SH1106 not found! Check wiring / address."));
        for (;;) delay(1000);       // delay keeps the watchdog happy
    }
    display.clearDisplay();
    display.display();

    mpuOk = mpu.begin(0x68, &Wire);
    if (mpuOk) {
        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    } else {
        Serial.println(F("MPU6050 not found - eyes will stay centered."));
        display.setTextSize(1);
        display.setTextColor(COLOR_ON);
        display.setCursor(0, 0);
        display.println(F("MPU6050 not found"));
        display.println(F("Eyes fixed."));
        display.display();
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
 