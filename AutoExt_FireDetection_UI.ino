#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "logos.h"

// ==========================================================
// AUTO-EXT
// Vehicle Fire Detection & Suppression
//
// OLED: 128 x 64
//
// SHORT PRESS:
//   Change View + Beep
//
// LONG PRESS:
//   Alarm ON/OFF
//
// WARNING:
//   Slow buzzer
//
// FIRE:
//   Fast buzzer + flashing display
//
// LOGOS:
//   Main AutoExt logo  = 128x64
//   Front bonnet logo  = 24x16
//   Rear bonnet logo   = 24x16
// ==========================================================

// ==========================================================
// OLED
// ==========================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==========================================================
// LOGO DIMENSIONS
// ==========================================================
#define AUTOEXT_LOGO_WIDTH   128
#define AUTOEXT_LOGO_HEIGHT   64

#define BONNET_LOGO_WIDTH     24
#define BONNET_LOGO_HEIGHT    16

// ==========================================================
// PIN DEFINITIONS
// ==========================================================
#define MQ2_FRONT_PIN      33
#define FLAME_FRONT_PIN    27

#define MQ2_REAR_PIN       32
#define FLAME_REAR_PIN     26

#define BUTTON_PIN         14
#define BUZZER_PIN         25

// ==========================================================
// SMOKE SETTINGS
// ==========================================================
const int SMOKE_THRESHOLD_HIGH = 1500;
const int SMOKE_HYSTERESIS = 200;
const int SMOKE_THRESHOLD_LOW = SMOKE_THRESHOLD_HIGH - SMOKE_HYSTERESIS;
const int ADC_MAX = 4095;

// ==========================================================
// FLAME SENSOR
// Most modules output LOW when flame is detected
// ==========================================================
#define FLAME_DETECTED LOW

// ==========================================================
// ZONE STRUCTURE
// ==========================================================
struct Zone {
  const char* name;
  uint8_t mq2Pin;
  uint8_t flamePin;
  int smokeRaw;
  bool smokeDetected;
  bool flameDetected;
  bool fireActive;
  bool warningActive;
};

// ==========================================================
// ZONES
// ==========================================================
Zone zones[2] = {
  { "FRONT", MQ2_FRONT_PIN, FLAME_FRONT_PIN, 0, false, false, false, false },
  { "REAR",  MQ2_REAR_PIN,  FLAME_REAR_PIN,  0, false, false, false, false }
};

// ==========================================================
// VIEW MODES
// ==========================================================
enum ViewMode {
  VIEW_OVERVIEW = 0,
  VIEW_DETAIL_FRONT = 1,
  VIEW_DETAIL_REAR = 2
};

ViewMode currentView = VIEW_OVERVIEW;

// ==========================================================
// BUTTON VARIABLES
// ==========================================================
uint8_t lastRawReading = HIGH;
uint8_t buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long pressStartTime = 0;
bool longPressFired = false;
const unsigned long LONG_PRESS_MS = 700;

// ==========================================================
// BUZZER VARIABLES
// ==========================================================
bool buzzerMuted = false;
bool buzzerOn = false;
unsigned long lastBuzzerToggle = 0;

// ==========================================================
// ALARM MESSAGE
// ==========================================================
bool showAlarmMessage = false;
unsigned long alarmMessageTime = 0;
const unsigned long ALARM_MESSAGE_MS = 1200;

// ==========================================================
// WARM-UP
// ==========================================================
unsigned long bootTime = 0;
const unsigned long WARMUP_MS = 30000;   // 30 seconds

// ==========================================================
// FUNCTION DECLARATIONS
// ==========================================================
void updateZone(Zone &z);
void drawHeader();
void drawFlame(int x, int y, int s, bool active, uint16_t color);
void drawBonnetLogo(int x, int y, uint8_t zone, uint16_t color);
void drawSmokeBar(int x, int y, int w, int h, int value, int maxVal, int threshold, uint16_t color);
void drawOverview();
void drawDetail(uint8_t idx);
void drawStatusBanner(int y, bool fire, bool warning);
void drawBootScreen();
void warmUpSensors();

// ==========================================================
// SETUP
// ==========================================================
void setup() {
  Serial.begin(115200);

  // I2C
  Wire.begin(21, 22);

  // FLAME SENSORS – use pull-up (most modules are open-collector)
  pinMode(FLAME_FRONT_PIN, INPUT_PULLUP);
  pinMode(FLAME_REAR_PIN,  INPUT_PULLUP);

  // BUTTON
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // BUZZER
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ADC
  analogReadResolution(12);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
    while (true) { delay(100); }
  }

  // BOOT SCREEN
  drawBootScreen();

  // MQ2 WARM-UP
  warmUpSensors();

  // Record boot time after warm-up
  bootTime = millis();
}

// ==========================================================
// MAIN LOOP
// ==========================================================
void loop() {
  unsigned long now = millis();

  // UPDATE SENSORS
  for (uint8_t i = 0; i < 2; i++) {
    updateZone(zones[i]);
  }

  // BUTTON
  uint8_t rawReading = digitalRead(BUTTON_PIN);

  if (rawReading != lastRawReading) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > debounceDelay) {
    if (rawReading != buttonState) {
      buttonState = rawReading;

      if (buttonState == LOW) {
        pressStartTime = now;
        longPressFired = false;
      } else {
        if (!longPressFired) {
          // CHANGE VIEW
          currentView = (ViewMode)((currentView + 1) % 3);

          // SHORT PRESS BEEP
          digitalWrite(BUZZER_PIN, HIGH);
          delay(50);
          digitalWrite(BUZZER_PIN, LOW);
        }
      }
    }
  }
  lastRawReading = rawReading;

  // LONG PRESS
  if (buttonState == LOW && !longPressFired && (now - pressStartTime > LONG_PRESS_MS)) {
    buzzerMuted = !buzzerMuted;
    longPressFired = true;

    showAlarmMessage = true;
    alarmMessageTime = now;

    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // GLOBAL FIRE / WARNING
  bool anyFire = zones[0].fireActive || zones[1].fireActive;
  bool anyWarning = zones[0].warningActive || zones[1].warningActive;

  // ALARM BUZZER – always updated, even during alarm message
  if (!buzzerMuted && (anyFire || anyWarning)) {
    unsigned long period = anyFire ? 120 : 450;
    if (now - lastBuzzerToggle >= period) {
      lastBuzzerToggle = now;
      buzzerOn = !buzzerOn;
      digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }

  // OLED
  display.clearDisplay();
  drawHeader();

  // TEMPORARY ALARM MESSAGE
  if (showAlarmMessage) {
    if (now - alarmMessageTime > ALARM_MESSAGE_MS) {
      showAlarmMessage = false;
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(2);

      const char* msg = buzzerMuted ? "MUTED" : "ALARM ON";
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 25);
      display.print(msg);

      display.setTextSize(1);
      const char* hint = "HOLD = TOGGLE";
      display.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 50);
      display.print(hint);

      display.display();
      delay(20);
      // Do NOT return – buzzer already updated, and we skip normal view below
    }
  }

  // NORMAL VIEW (only if no alarm message)
  if (!showAlarmMessage) {
    switch (currentView) {
      case VIEW_OVERVIEW:    drawOverview();      break;
      case VIEW_DETAIL_FRONT: drawDetail(0);      break;
      case VIEW_DETAIL_REAR:  drawDetail(1);      break;
    }
  }

  display.display();

  // SERIAL MONITOR
  Serial.print("FRONT MQ2: ");
  Serial.print(zones[0].smokeRaw);
  Serial.print(" | FRONT FLAME: ");
  Serial.print(zones[0].flameDetected ? "YES" : "NO");
  Serial.print(" || REAR MQ2: ");
  Serial.print(zones[1].smokeRaw);
  Serial.print(" | REAR FLAME: ");
  Serial.println(zones[1].flameDetected ? "YES" : "NO");

  delay(50);
}

// ==========================================================
// UPDATE ZONE
// ==========================================================
void updateZone(Zone &z) {
  // MQ2
  z.smokeRaw = analogRead(z.mq2Pin);

  // SMOKE HYSTERESIS
  if (!z.smokeDetected && z.smokeRaw > SMOKE_THRESHOLD_HIGH) {
    z.smokeDetected = true;
  } else if (z.smokeDetected && z.smokeRaw < SMOKE_THRESHOLD_LOW) {
    z.smokeDetected = false;
  }

  // FLAME
  z.flameDetected = (digitalRead(z.flamePin) == FLAME_DETECTED);

  // FIRE
  z.fireActive = z.smokeDetected && z.flameDetected;

  // WARNING
  z.warningActive = z.smokeDetected || z.flameDetected;
}

// ==========================================================
// BONNET LOGO
// ==========================================================
void drawBonnetLogo(int x, int y, uint8_t zone, uint16_t color) {
  if (zone == 0) {
    display.drawBitmap(x, y, FRONT_BONNET_LOGO, BONNET_LOGO_WIDTH, BONNET_LOGO_HEIGHT, color);
  } else {
    display.drawBitmap(x, y, REAR_BONNET_LOGO, BONNET_LOGO_WIDTH, BONNET_LOGO_HEIGHT, color);
  }
}

// ==========================================================
// FLAME ICON
// ==========================================================
void drawFlame(int x, int y, int s, bool active, uint16_t color) {
  int cx = x + s / 2;
  int by = y + s - s / 3;
  int r = max(2, s / 3);

  if (active) {
    display.fillCircle(cx, by, r, color);
    display.fillTriangle(cx - r, by - r / 2, cx + r, by - r / 2, cx, y, color);
  } else {
    display.drawCircle(cx, by, r, color);
    display.drawTriangle(cx - r, by - r / 2, cx + r, by - r / 2, cx, y, color);
  }
}

// ==========================================================
// SMOKE BAR
// ==========================================================
void drawSmokeBar(int x, int y, int w, int h, int value, int maxVal, int threshold, uint16_t color) {
  display.drawRect(x, y, w, h, color);

  int usableW = w - 2;
  int filled = map(constrain(value, 0, maxVal), 0, maxVal, 0, usableW);

  if (filled > 0) {
    display.fillRect(x + 1, y + 1, filled, h - 2, color);
  }

  // Threshold line – dotted so it remains visible on filled or empty bar
  int threshX = x + 1 + map(constrain(threshold, 0, maxVal), 0, maxVal, 0, usableW);
  for (int i = y; i < y + h; i += 2) {
    display.drawPixel(threshX, i, color);
  }
}

// ==========================================================
// STATUS BANNER
// ==========================================================
void drawStatusBanner(int y, bool fire, bool warning) {
  int h = 64 - y;
  const char* txt = fire ? "!! FIRE !!" : (warning ? "WARNING" : "SAFE");
  bool flashOn = fire ? ((millis() / 180) % 2 == 0) : true;
  uint16_t fg = SSD1306_WHITE;

  if (fire && flashOn) {
    display.fillRect(0, y, SCREEN_WIDTH, h, SSD1306_WHITE);
    fg = SSD1306_BLACK;
  } else if (warning) {
    display.drawRect(0, y, SCREEN_WIDTH, h, SSD1306_WHITE);
  }

  display.setTextColor(fg);
  int16_t x1, y1;
  uint16_t w, th;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &th);
  display.setCursor((SCREEN_WIDTH - w) / 2, y + (h - th) / 2);
  display.print(txt);
  display.setTextColor(SSD1306_WHITE);
}

// ==========================================================
// HEADER
// ==========================================================
void drawHeader() {
  display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);

  display.setCursor(2, 1);
  display.print("AUTO-EXT");

  display.setCursor(52, 1);
  display.print(buzzerMuted ? "MUTE" : "ALRM");

  unsigned long secs = millis() / 1000;
  unsigned int mm = (secs / 60) % 100;
  unsigned int ss = secs % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", mm, ss);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w - 2, 1);
  display.print(buf);

  display.setTextColor(SSD1306_WHITE);
}

// ==========================================================
// OVERVIEW
// ==========================================================
void drawOverview() {
  display.drawFastVLine(64, 11, 52, SSD1306_WHITE);

  int cols[2][2] = { { 2, 58 }, { 68, 58 } };

  for (uint8_t i = 0; i < 2; i++) {
    Zone &z = zones[i];
    int xOff = cols[i][0];
    int colW = cols[i][1];
    int colCenterX = xOff + colW / 2;

    bool flashOn = ((millis() / 250) % 2 == 0);
    bool invertCol = z.fireActive && flashOn;
    uint16_t fg = SSD1306_WHITE;

    if (invertCol) {
      display.fillRect(xOff - 1, 11, colW + 2, 52, SSD1306_WHITE);
      fg = SSD1306_BLACK;
    }

    display.setTextColor(fg);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(z.name, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(colCenterX - w / 2, 12);
    display.print(z.name);

    drawBonnetLogo(colCenterX - BONNET_LOGO_WIDTH / 2, 22, i, fg);

    drawSmokeBar(xOff + 4, 40, colW - 8, 8, z.smokeRaw, ADC_MAX, SMOKE_THRESHOLD_HIGH, fg);

    const char* st = z.fireActive ? "FIRE!" : (z.warningActive ? "WARN" : "SAFE");
    display.getTextBounds(st, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(colCenterX - w / 2, 52);
    display.print(st);
  }
  display.setTextColor(SSD1306_WHITE);
}

// ==========================================================
// DETAIL SCREEN
// ==========================================================
void drawDetail(uint8_t idx) {
  Zone &z = zones[idx];
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(2, 12);
  display.print(z.name);
  display.print(" - DETAIL");

  drawFlame(4, 21, 24, z.flameDetected, SSD1306_WHITE);

  int rx = 40;
  int pct = map(constrain(z.smokeRaw, 0, ADC_MAX), 0, ADC_MAX, 0, 100);
  display.setCursor(rx, 21);
  display.print("Smoke: ");
  display.print(pct);
  display.print("%");

  display.setCursor(rx, 31);
  display.print("Raw: ");
  display.print(z.smokeRaw);

  display.setCursor(rx, 41);
  display.print("Flame: ");
  display.print(z.flameDetected ? "YES" : "NO");

  drawStatusBanner(50, z.fireActive, z.warningActive);
}

// ==========================================================
// BOOT SCREEN
// ==========================================================
void drawBootScreen() {
  display.clearDisplay();
  display.drawBitmap(0, 0, AUTOEXT_LOGO, AUTOEXT_LOGO_WIDTH, AUTOEXT_LOGO_HEIGHT, SSD1306_WHITE);
  display.display();
  delay(1500);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* loadingText = "INITIALIZING AUTO-EXT";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(loadingText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 20);
  display.print(loadingText);

  int barX = 14, barY = 38, barW = 100, barH = 7;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  display.display();

  for (int i = 0; i <= barW - 2; i += 2) {
    display.fillRect(barX + 1, barY + 1, i, barH - 2, SSD1306_WHITE);
    display.display();
    delay(15);
  }
  delay(300);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* line1 = "BTN : CHANGE VIEW";
  const char* line2 = "HOLD: ALARM ON/OFF";
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 22);
  display.print(line1);
  display.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 38);
  display.print(line2);
  display.display();
  delay(1200);
}

// ==========================================================
// WARM-UP SENSORS (MQ2)
// ==========================================================
void warmUpSensors() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* title = "MQ2 WARMING UP";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 20);
  display.print(title);

  display.setCursor((SCREEN_WIDTH - w) / 2, 40);
  display.print("PLEASE WAIT");

  display.display();

  // 30-second countdown (simple delay, no fancy bar)
  for (int i = 30; i > 0; i--) {
    display.fillRect(0, 50, SCREEN_WIDTH, 14, SSD1306_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%2d s", i);
    display.setTextSize(1);
    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 52);
    display.print(buf);
    display.display();
    delay(1000);
  }

  display.clearDisplay();
  display.setCursor(30, 30);
  display.print("READY");
  display.display();
  delay(500);
}