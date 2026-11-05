/*
 * M5Stack Heating Timer
 * M5Stack Core Metal + 4Relay Module + CHT8305 Temperature/Humidity Sensor
 * 
 * Gebaseerd op werkend Example.md - gebruikt M5Stack.h (niet M5Unified)
 * 
 * Knop 1: 1u verwarming
 * Knop 2: 2u verwarming
 * Knop 3: 4u verwarming
 * Timer actief: elke knop = cancel
 * Veiligheid: relais altijd uit bij opstart, timer herstart niet na stroomuitval.
 */

#include <M5Stack.h>
#include <Wire.h>
#include "Module_4RELAY.h"
#include "CHT8305.h"

// Free Fonts (zelfde als Example.md)
#define FF12  &FreeSans12pt7b
#define FF18  &FreeSans18pt7b
#define FF18B &FreeSansBold18pt7b
#define FF24  &FreeSans24pt7b

// --- Hardware ---
MODULE_4RELAY relay;
CHT8305 cht(0x40);
TFT_eSprite sprite(&M5.Lcd);

// --- Timer & state ---
uint32_t timerEndMs = 0;       // 0 = geen actieve timer
uint32_t lastTempUpdate = 0;
uint32_t lastDisplayUpdate = 0;
float lastTemp = 0.0f;
bool tempSensorOk = false;

// --- Constants (landscape: 320x240, knoppen onder scherm) ---
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
static constexpr uint32_t TEMP_UPDATE_MS = 2000;
static constexpr uint32_t DISPLAY_UPDATE_MS = 1000;

// Display layout (landscape 320x240)
#define SPRITE_H 165
#define TEMP_Y 35
#define COUNTDOWN_Y (SCREEN_HEIGHT / 2)  // 120 = midden display
#define COUNTDOWN_H 70
#define LABEL_Y 200
#define LABEL_X_1 53
#define LABEL_X_2 160
#define LABEL_X_3 267

// -----------------------------------------------------------------------------
void updateTimer() {
  if (timerEndMs == 0) return;
  if (millis() >= timerEndMs) {
    relay.setRelay(0, false);
    timerEndMs = 0;
  }
}

// -----------------------------------------------------------------------------
void handleButtons() {
  if (timerEndMs > 0) {
    if (M5.BtnA.wasReleased() || M5.BtnB.wasReleased() || M5.BtnC.wasReleased()) {
      relay.setRelay(0, false);
      timerEndMs = 0;
    }
  } else {
    uint32_t durationMs = 0;
    if (M5.BtnA.wasReleased()) durationMs = 60 * 60 * 1000UL;
    if (M5.BtnB.wasReleased()) durationMs = 2 * 60 * 60 * 1000UL;
    if (M5.BtnC.wasReleased()) durationMs = 4 * 60 * 60 * 1000UL;
    if (durationMs > 0) {
      timerEndMs = millis() + durationMs;
      relay.setRelay(0, true);
    }
  }
}

// -----------------------------------------------------------------------------
void updateTemperature() {
  if (millis() - lastTempUpdate >= TEMP_UPDATE_MS) {
    lastTempUpdate = millis();
    int err = cht.read();
    tempSensorOk = (err == CHT8305_OK);
    if (tempSensorOk) {
      lastTemp = cht.getTemperature();
    }
  }
}

// -----------------------------------------------------------------------------
void drawLabels(bool timerActive) {
  M5.Lcd.fillRect(0, SPRITE_H, SCREEN_WIDTH, SCREEN_HEIGHT - SPRITE_H, TFT_BLACK);
  M5.Lcd.setFreeFont(FF12);
  M5.Lcd.setTextColor(TFT_LIGHTGREY);
  M5.Lcd.setTextDatum(MC_DATUM);
  if (timerActive) {
    M5.Lcd.drawString("stop", SCREEN_WIDTH / 2, LABEL_Y);
  } else {
    M5.Lcd.drawString("1u", LABEL_X_1, LABEL_Y);
    M5.Lcd.drawString("2u", LABEL_X_2, LABEL_Y);
    M5.Lcd.drawString("4u", LABEL_X_3, LABEL_Y);
  }
}

// -----------------------------------------------------------------------------
void drawDisplay() {
  bool active = (timerEndMs > 0);
  uint16_t countdownColor = active ? TFT_RED : TFT_DARKGREEN;
  int cx = SCREEN_WIDTH / 2;
  int w = 240;

  // Temp + countdown in sprite (double buffering = geen flikkeren)
  sprite.fillSprite(TFT_BLACK);

  // Temperatuur (groot, boven)
  sprite.setFreeFont(FF18B);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextDatum(MC_DATUM);
  if (tempSensorOk) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f C", lastTemp);
    sprite.drawString(buf, cx, TEMP_Y);
  } else {
    sprite.drawString("-- C", cx, TEMP_Y);
  }

  // Countdown-gebied (rood/groen) - midden display, groter font
  sprite.fillRoundRect(cx - w/2, COUNTDOWN_Y - COUNTDOWN_H/2, w, COUNTDOWN_H, 8, countdownColor);
  sprite.setFreeFont(FF24);
  sprite.setTextColor(TFT_WHITE);
  if (active) {
    uint32_t remainingMs = timerEndMs - millis();
    uint32_t remainingSec = (remainingMs + 500) / 1000;
    uint32_t remainingMin = remainingSec / 60;
    remainingSec = remainingSec % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu:%02lu min", (unsigned long)remainingMin, (unsigned long)remainingSec);
    sprite.drawString(buf, cx, COUNTDOWN_Y);
  } else {
    sprite.drawString("OFF", cx, COUNTDOWN_Y);
  }

  sprite.pushSprite(0, 0);

  static bool lastLabelActive = true;  // != default, zodat eerste drawLabels() wordt aangeroepen
  if (active != lastLabelActive) {
    lastLabelActive = active;
    drawLabels(active);
  }
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("M5 Heating Timer init");

  M5.begin();
  M5.Power.begin();
  M5.Lcd.setRotation(1);  // Landscape: 320x240, knoppen onder scherm (1 ipv 0)
  M5.Lcd.fillScreen(TFT_BLACK);

  Wire.begin(21, 22);
  Wire.setClock(400000);

  // Sprite 320x170 - 2 params (16-bit); 4-bit faalt op sommige boards
  if (!sprite.createSprite(SCREEN_WIDTH, SPRITE_H)) {
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.setFreeFont(FF12);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Sprite creation failed!");
    while (1) delay(100);
  }
  sprite.setTextDatum(MC_DATUM);
  sprite.setSwapBytes(true);

  // Relaismodule: veilig UIT bij opstart
  if (!relay.begin(&Wire, MODULE_4RELAY_ADDR, 21, 22, 200000L)) {
    Serial.println("4RELAY INIT ERROR");
  } else {
    relay.setRelay(0, false);
    relay.setAllRelay(false);
    Serial.println("Relay init OK");
  }

  // CHT8305 sensor
  cht.begin();
  tempSensorOk = cht.isConnected();
  Serial.println(tempSensorOk ? "CHT8305 OK" : "CHT8305 not found");

  lastTempUpdate = millis();
  lastDisplayUpdate = millis();
  drawDisplay();
}

// -----------------------------------------------------------------------------
void loop() {
  delay(1);
  M5.update();

  updateTimer();
  handleButtons();
  updateTemperature();

  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = millis();
    drawDisplay();
  }
}
