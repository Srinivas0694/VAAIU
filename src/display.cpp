#include "display.hpp"
#include "config.hpp"

#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
 
extern Adafruit_ST7789 tft;

/* --- Modern Color Palette (Dark Theme) --- */
#define C_BG          0x1084  // Deep Slate Blue (Background)
#define C_PANEL       0x2126  // Dark Grey/Blue (Panels)
#define C_HEADER      0x2969  // Slightly Lighter Header
#define C_TEXT_MAIN   0xFFFF  // White
#define C_TEXT_MUTED  0x9CD3  // Soft Blue-Grey
#define C_ACCENT      0x4D3F  // Highlight Blue
#define C_GOOD        0x2E09  // Soft Green
#define C_WARN        0xE4E0  // Soft Orange
#define C_ALERT       0xE986  // Soft Red

/* --- Layout Constants --- */
#define SCREEN_W      320
#define SCREEN_H      240

DisplayMode currentDisplayMode = DISPLAY_MAIN;

/* ================= HELPERS ================= */

void drawPanel(int x, int y, int w, int h, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 8, color);
}

void drawCentered(const char* txt, int x, int y, int w, int h, const GFXfont* font, uint16_t color, bool centerY = true) {
  tft.setFont(font);
  tft.setTextColor(color);

  int16_t x1, y1;
  uint16_t wt, ht;
  tft.getTextBounds(txt, 0, 0, &x1, &y1, &wt, &ht);
  int xPos = x + (w - wt) / 2;
  int yPos = y + h - 6;
  if (centerY) {
    yPos = y + (h / 2) + (ht / 2) - 2;
  }

  tft.setCursor(xPos, yPos);
  tft.print(txt);
}

static void draw_battery_percent(int percent) {
  int x = SCREEN_W - 65;
  int y = 8;
  int w = 55;
  int h = 24;
  tft.fillRect(x, y, w, h, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_ACCENT);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + w - tw - 4, y + h - 8);
  tft.print(buf);
}

static void drawValueCell(int col, int row, const String& label, const String& value, uint16_t valueColor = C_TEXT_MAIN) {
  int gridCols = 2;
  int gridRows = 5;
  int gridW = SCREEN_W - 20;
  int gridH = SCREEN_H - 50;
  int cellW = gridW / gridCols;
  int cellH = gridH / gridRows;
  int startX = 10;
  int startY = 40;
  int x = startX + col * cellW;
  int y = startY + row * cellH;

  tft.fillRect(x + 2, y + 2, cellW - 4, cellH - 4, C_BG);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  tft.setCursor(x + 8, y + 18);
  tft.print(label);

  tft.setTextColor(valueColor);
  int16_t x1, y1;
  uint16_t wt, ht;
  tft.getTextBounds(value.c_str(), 0, 0, &x1, &y1, &wt, &ht);
  tft.setCursor(x + cellW - wt - 8, y + 18);
  tft.print(value);
}

static void drawFullWidthCell(const String& label, const String& value, uint16_t valueColor = C_TEXT_MAIN) {
  int gridW = SCREEN_W - 20;
  int gridRows = 5;
  int gridH = SCREEN_H - 50;
  int cellH = gridH / gridRows;
  int startX = 10;
  int startY = 40;
  int x = startX;
  int y = startY + 4 * cellH;
  int w = gridW;

  tft.fillRect(x + 2, y + 2, w - 4, cellH - 4, C_BG);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  tft.setCursor(x + 8, y + 18);
  tft.print(label);

  tft.setTextColor(valueColor);
  int16_t x1, y1;
  uint16_t wt, ht;
  tft.getTextBounds(value.c_str(), 0, 0, &x1, &y1, &wt, &ht);
  tft.setCursor(x + w - wt - 8, y + 18);
  tft.print(value);
}

/* ================= INIT ================= */

void display_init() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init(240, 320);
  tft.setRotation(1);
  display_reinit_layout();
}

/* ================= LAYOUT ================= */

void display_reinit_layout() {
  currentDisplayMode = DISPLAY_MAIN;
  tft.fillScreen(C_BG);

  tft.fillRect(0, 0, SCREEN_W, 40, C_HEADER);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(10, 28);
  tft.print("VAAIU MONITOR");

  int gridCols = 2;
  int gridRows = 5;
  int gridW = SCREEN_W - 20;
  int gridH = SCREEN_H - 50;
  int cellW = gridW / gridCols;
  int cellH = gridH / gridRows;
  int startX = 10;
  int startY = 40;

  tft.drawRect(startX, startY, gridW, gridH, C_PANEL);
  tft.drawFastVLine(startX + cellW, startY, gridH, C_PANEL);
  for (int i = 1; i < gridRows; ++i) {
    tft.drawFastHLine(startX, startY + i * cellH, gridW, C_PANEL);
  }
}

void display_update(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum) {
  display_update_with_battery(pm1, pm25, pm4, pm10, voc, nox, co2, temp, hum, -1);
}

void display_update_with_battery(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum, int battery_percent) {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  draw_battery_percent(battery_percent);

  drawValueCell(0, 0, "PM1.0", String(pm1, 0));
  drawValueCell(1, 0, "NOX", String(nox, 0));
  drawValueCell(0, 1, "PM2.5", String(pm25, 1), (pm25 > 150) ? C_ALERT : (pm25 > 35 ? C_WARN : C_GOOD));
  drawValueCell(1, 1, "CO2", String(co2) + " ppm", (co2 > 1000) ? C_WARN : C_GOOD);
  drawValueCell(0, 2, "PM4", String(pm4, 0));
  drawValueCell(1, 2, "Temp", String(temp, 1) + "C");
  drawValueCell(0, 3, "PM10", String(pm10, 0));
  drawValueCell(1, 3, "Hum", String(hum, 0) + "%");
  drawFullWidthCell("VOC", String(voc, 0) + " ppm");
}

void display_update_time() {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char timeStr[6];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

  tft.fillRect(200, 0, 120, 40, C_HEADER);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  tft.setCursor(208, 26);
  tft.print(timeStr);
}

void display_cpu_status() {
  currentDisplayMode = DISPLAY_CPU_STATUS;
  tft.fillScreen(C_BG);

  tft.fillRect(0, 0, SCREEN_W, 40, C_HEADER);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(10, 28);
  tft.print("CPU STATUS");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  tft.setCursor(40, 90);
  tft.printf("Free Heap: %d bytes", ESP.getFreeHeap());
  tft.setCursor(40, 120);
  tft.printf("CPU Freq: %d MHz", ESP.getCpuFreqMHz());
  tft.setCursor(40, 150);
  tft.printf("Uptime: %lu sec", millis() / 1000);
}

void display_wifi_setup_screen() {
  currentDisplayMode = DISPLAY_WIFI_SETUP;
  tft.fillScreen(C_BG);

  tft.fillRect(0, 0, SCREEN_W, 40, C_HEADER);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(10, 28);
  tft.print("SETUP MODE");

  int bx = 30;
  int by = 60;
  int bw = 260;
  int bh = 140;
  drawPanel(bx, by, bw, bh, C_PANEL);
  tft.fillRoundRect(bx + 110, by - 20, 40, 40, 8, C_WARN);
  drawCentered("!", bx + 110, by - 20, 40, 40, &FreeSansBold12pt7b, C_BG);
  drawCentered("WIFI SETUP", bx, by + 40, bw, 30, &FreeSansBold9pt7b, C_TEXT_MAIN);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  drawCentered("Connect to Hotspot:", bx, by + 80, bw, 20, &FreeSans9pt7b, C_TEXT_MUTED);
  drawCentered("AQI_SETUP", bx, by + 110, bw, 30, &FreeSansBold9pt7b, C_ACCENT);
}

void display_clear_all() {
  tft.fillScreen(C_BG);
}

void display_wake() {
  digitalWrite(TFT_BL, HIGH);
  display_reinit_layout();
}
