#include <stdint.h>
#include "display.hpp"
#include "config.hpp"
#include "logo.hpp"

// Forward declarations to fix scope errors
extern void display_update_with_battery(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum, int battery_percent);
#define C_BG 0x1084
// ...existing code...
#include <Adafruit_ST7789.h>
extern Adafruit_ST7789 tft;
// ...existing code...
// ...existing code...
#include <stdint.h>
// ...existing code...
// Place these after all relevant declarations
// ...existing code...
// ...existing code...
// === END OF FILE ===
void display_update(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum) {
  display_update_with_battery(pm1, pm25, pm4, pm10, voc, nox, co2, temp, hum, -1);
}

void display_sleep() {
  tft.fillScreen(C_BG);
  // If supported by your TFT library, uncomment:
  // tft.setBrightness(0);
}
#include "display.hpp"
#include "config.hpp"
#include "logo.hpp"

#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// --- Fonts ---
// Make sure these are installed in your library
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

// Main Cards
#define CARD_Y        45
#define CARD_H        85
#define CARD_W        152 // (320 - 16 padding) / 2
// GAP is already defined in config.hpp as 8

// Grid
#define GRID_Y        145
#define GRID_H        90
#define GRID_ROW_H    40

DisplayMode currentDisplayMode = DISPLAY_MAIN;

/* ================= HELPERS ================= */

// Draw a smooth rounded panel
void drawPanel(int x, int y, int w, int h, uint16_t color) {
  tft.fillRoundRect(x, y, w, h, 8, color);
}

// Draw text centered in a specific box
void drawCentered(const char* txt, int x, int y, int w, int h, const GFXfont* font, uint16_t color, bool centerY = true) {
  tft.setFont(font);
  tft.setTextColor(color);
  
  int16_t x1, y1;
  uint16_t wt, ht;
  tft.getTextBounds(txt, 0, 0, &x1, &y1, &wt, &ht);
  
  int xPos = x + (w - wt) / 2;
  
  // Vertical centering logic for FreeFonts (which draw from baseline)
  int yPos = y + h - 6; // Default baseline offset
  if(centerY) {
    yPos = y + (h/2) + (ht/2) - 2; 
  }
  
  tft.setCursor(xPos, yPos);
  tft.print(txt);
}

// Right aligned text helper
void drawRightAligned(String txt, int x, int y, int w, const GFXfont* font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  
  int16_t x1, y1;
  uint16_t wt, ht;
  tft.getTextBounds(txt.c_str(), 0, 0, &x1, &y1, &wt, &ht);
  
  tft.setCursor(x + w - wt - 5, y);
  tft.print(txt);
}

// Draw a colored dot status indicator
void drawStatusDot(int x, int y, uint16_t color) {
  tft.fillCircle(x, y, 4, color);
}

/* ================= INIT ================= */

void display_init() {
  Serial.println("Init Display...");
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW); delay(50);
  digitalWrite(TFT_RST, HIGH); delay(50);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init(240, 320);
  tft.setRotation(1); // Landscape
  display_reinit_layout();
}

/* ================= LAYOUT ================= */

void display_reinit_layout() {
  currentDisplayMode = DISPLAY_MAIN;
  tft.fillScreen(C_BG);

  // --- Header ---
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(10, 28);
  tft.print("VAAIU MONITOR");

  // --- Grid Layout (labels only, values are drawn live) ---
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  // Clean table: 5 rows, 2 columns, all text centered, VOC row at bottom
  int gridCols = 2, gridRows = 5;
  int gridW = SCREEN_W - 20, gridH = SCREEN_H - 50;
  int cellW = gridW / gridCols;
  int cellH = gridH / gridRows;
  int startX = 10, startY = 40;
  // Draw outer border
  tft.drawRect(startX, startY, gridW, gridH, C_PANEL);
  // Draw vertical line
  tft.drawFastVLine(startX + cellW, startY, gridH, C_PANEL);
  // Draw horizontal lines
  for (int i = 1; i < gridRows; ++i)
    tft.drawFastHLine(startX, startY + i*cellH, gridW, C_PANEL);


  // --- Bottom Color Bar (as in your image) ---
  for (int i = 0; i < 10; ++i) {
    uint16_t color = tft.color565(180 - i*10, 220 - i*12, 220 - i*20);
    tft.fillRect(18 + i*28, 225, 28, 12, color);
  }
  // Removed bottom color bar to match the reference UI
  // (No color bar or palette strip at the bottom)
}

/* ================= UPDATE ================= */

// Helper to clear and redraw a number without flicker
void redrawValue(int x, int y, String val, bool isAlert, bool large) {
  int boxW = large ? 90 : 50;
  int boxH = large ? 30 : 20;
  int yOff = large ? -24 : -16;
  
  // 1. Clear area (Fill with background color)
  uint16_t bg = large ? C_PANEL : C_BG; 
  tft.fillRect(x, y + yOff, boxW, boxH, bg);

  // 2. Setup Font/Color
  if (large) tft.setFont(&FreeSansBold12pt7b);
  else tft.setFont(&FreeSansBold9pt7b);
  
  tft.setTextColor(isAlert ? C_ALERT : C_TEXT_MAIN);

  // 3. Draw
  tft.setCursor(x, y);
  tft.print(val);
}


// Draw battery percentage at top right
static void draw_battery_percent(int percent) {
  // Area: top right, inside header bar
  int x = SCREEN_W - 60;
  int y = 8;
  int w = 50;
  int h = 24;
  tft.fillRect(x, y, w, h, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(C_ACCENT);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + w - tw - 4, y + h - 8);
  tft.print(buf);
}

void display_update_with_battery(
  float pm1, float pm25, float pm4, float pm10,
  float voc, float nox,
  uint16_t co2,
  float temp, float hum,
  int battery_percent
) {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  // Draw battery percent first
  draw_battery_percent(battery_percent);

  // --- Live Grid Values with parameter names ---
  int gridCols = 2, gridRows = 5;
  int gridW = SCREEN_W - 20, gridH = SCREEN_H - 50;
  int cellW = gridW / gridCols;
  int cellH = gridH / gridRows;
  int startX = 10, startY = 40;
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  auto printCell = [&](int col, int row, const String& label, const String& val, uint16_t valColor = C_TEXT_MAIN) {
    int x = startX + col*cellW;
    int y = startY + row*cellH;
    tft.fillRect(x + 2, y + 2, cellW - 4, cellH - 4, C_BG);
    tft.setTextColor(C_TEXT_MUTED);
    tft.setCursor(x + 6, y + 20);
    tft.print(label);
    tft.setTextColor(valColor);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(val.c_str(), 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(x + cellW - w - 6, y + 20);
    tft.print(val);
    tft.setTextColor(C_TEXT_MAIN);
  };
  printCell(0, 0, "PM1.0", String(pm1, 0));
  printCell(1, 0, "NOX", String(nox, 0));
  printCell(0, 1, "PM2.5", String(pm25, 1), (pm25 > 35) ? ((pm25 > 150) ? C_ALERT : C_WARN) : C_GOOD);
  printCell(1, 1, "CO2", String(co2) + " ppm", (co2 > 1000) ? C_WARN : C_GOOD);
  printCell(0, 2, "PM4", String(pm4, 0));
  printCell(1, 2, "Temp", String(temp, 1) + "C");
  printCell(0, 3, "PM10", String(pm10, 0));
  printCell(1, 3, "Hum", String(hum, 0) + "%");
  int vocY = startY + 4*cellH;
  tft.fillRect(startX + 2, vocY + 2, gridW - 4, cellH - 4, C_BG);
  tft.setTextColor(C_TEXT_MUTED);
  tft.setCursor(startX + 6, vocY + 20);
  tft.print("VOC");
  tft.setTextColor(C_TEXT_MAIN);
  String vocVal = String(voc, 0) + " ppm";
  int16_t vx1, vy1; uint16_t vw, vh;
  tft.getTextBounds(vocVal.c_str(), 0, 0, &vx1, &vy1, &vw, &vh);
  tft.setCursor(startX + gridW - vw - 6, vocY + 20);
  tft.print(vocVal);
}

/* ================= SYSTEM SCREENS ================= */

void display_update_time() {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  struct tm tm;
  if (getLocalTime(&tm)) {
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &tm);
    
    // Clear top right corner
    tft.fillRect(240, 0, 80, 36, C_HEADER);
    
    // Clean table: 5 rows, 2 columns, VOC row below PM10 value, header centered only once
    int gridCols = 2, gridRows = 6;
    int gridW = SCREEN_W - 20, gridH = SCREEN_H - 50;
    int cellW = gridW / gridCols;
    int cellH = gridH / gridRows;
    int startX = 10, startY = 40;
    // Draw outer border
    tft.drawRect(startX, startY, gridW, gridH, C_PANEL);
    // Draw vertical line
    tft.drawFastVLine(startX + cellW, startY, gridH, C_PANEL);
    // Draw horizontal lines
    for (int i = 1; i < gridRows; ++i)
      tft.drawFastHLine(startX, startY + i*cellH, gridW, C_PANEL);
    // Draw header (centered only once)
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(C_ACCENT);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds("VAAIU MONITOR", 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(startX + (gridW - w) / 2, startY - 10);
    tft.print("VAAIU MONITOR");
    // Removed undefined drawRow call
  }
}

void display_wifi_setup_screen() {
  currentDisplayMode = DISPLAY_WIFI_SETUP;
  tft.fillScreen(C_BG);

  // Modern Alert Box
  int bx = 30, by = 40, bw = 260, bh = 160;
  drawPanel(bx, by, bw, bh, C_PANEL);
  // Icon placeholder (Orange square)
  tft.fillRoundRect(bx + 110, by - 20, 40, 40, 8, C_WARN);
  drawCentered("!", bx + 110, by - 20, 40, 40, &FreeSansBold12pt7b, C_BG);
  drawCentered("SETUP MODE", bx, by + 40, bw, 30, &FreeSansBold12pt7b, C_TEXT_MAIN);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  drawCentered("Connect to Hotspot:", bx, by + 80, bw, 20, &FreeSans9pt7b, C_TEXT_MUTED);
  drawCentered("AQI_SETUP", bx, by + 110, bw, 30, &FreeSansBold9pt7b, C_ACCENT);
}

void display_clear_all() {
  tft.fillScreen(C_BG);
}