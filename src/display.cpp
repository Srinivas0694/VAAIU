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
  int16_t x1_tb, y1_tb;
  uint16_t wt, ht;
  tft.getTextBounds(txt, 0, 0, &x1_tb, &y1_tb, &wt, &ht);
  tft.setCursor(x + w - wt - 4, y + ht - 6);
  tft.print(txt);
}

// Draw battery percentage indicator
static void draw_battery_percent(int percent) {
  if (percent < 0) return; // Skip if no battery data

  int x = 280, y = 10, w = 30, h = 15;
  tft.drawRoundRect(x, y, w, h, 2, C_TEXT_MUTED);

  // Battery level
  int fillW = (percent * (w - 4)) / 100;
  uint16_t color = (percent > 20) ? C_GOOD : (percent > 10) ? C_WARN : C_ALERT;
  tft.fillRect(x + 2, y + 2, fillW, h - 4, color);

  // Text
  char buf[5];
  sprintf(buf, "%d%%", percent);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  tft.setCursor(x - 35, y + h - 2);
  tft.print(buf);
}

/* ================= INIT ================= */

void display_init() {
  tft.init(240, 320); // ST7789 320x240 in portrait
  tft.setRotation(1); // Landscape orientation

  // Backlight control
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Turn on backlight

  tft.fillScreen(C_BG);

  // Draw logo initially - simple text logo
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(80, 120);
  tft.print("AQI MONITOR");
  tft.setCursor(110, 140);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  tft.print("v1.0.3");
  delay(2000);

  currentDisplayMode = DISPLAY_MAIN;
}

/* ================= MAIN DISPLAY ================= */

void display_update(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum) {
  display_update_with_battery(pm1, pm25, pm4, pm10, voc, nox, co2, temp, hum, -1);
}

void display_update_with_battery(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum, int battery_percent) {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  tft.fillScreen(C_BG);

  // Header
  drawPanel(0, 0, SCREEN_W, 40, C_HEADER);
  drawCentered("AQI MONITOR", 0, 0, SCREEN_W, 40, &FreeSansBold12pt7b, C_TEXT_MAIN);

  // Battery indicator
  draw_battery_percent(battery_percent);

  // Main cards
  int gap = 8;
  int card1_x = gap;
  int card2_x = gap + CARD_W + gap;

  // PM2.5 Card
  drawPanel(card1_x, CARD_Y, CARD_W, CARD_H, C_PANEL);
  drawCentered("PM2.5", card1_x, CARD_Y, CARD_W, 25, &FreeSansBold9pt7b, C_TEXT_MAIN, false);
  char pm25_str[16];
  sprintf(pm25_str, "%.1f", pm25);
  drawCentered(pm25_str, card1_x, CARD_Y + 25, CARD_W, 35, &FreeSansBold12pt7b, C_ACCENT);
  drawCentered("μg/m³", card1_x, CARD_Y + 60, CARD_W, 20, &FreeSans9pt7b, C_TEXT_MUTED, false);

  // CO2 Card
  drawPanel(card2_x, CARD_Y, CARD_W, CARD_H, C_PANEL);
  drawCentered("CO2", card2_x, CARD_Y, CARD_W, 25, &FreeSansBold9pt7b, C_TEXT_MAIN, false);
  char co2_str[16];
  sprintf(co2_str, "%u", co2);
  drawCentered(co2_str, card2_x, CARD_Y + 25, CARD_W, 35, &FreeSansBold12pt7b, C_ACCENT);
  drawCentered("ppm", card2_x, CARD_Y + 60, CARD_W, 20, &FreeSans9pt7b, C_TEXT_MUTED, false);

  // Data grid
  int grid_x = gap;
  int grid_w = SCREEN_W - 2 * gap;

  // Temperature
  tft.fillRect(grid_x, GRID_Y, grid_w, GRID_ROW_H, C_PANEL);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  tft.setCursor(grid_x + 8, GRID_Y + 28);
  tft.print("Temperature");
  char temp_str[16];
  sprintf(temp_str, "%.1f°C", temp);
  drawRightAligned(temp_str, grid_x, GRID_Y, grid_w, &FreeSans9pt7b, C_ACCENT);

  // Humidity
  tft.fillRect(grid_x, GRID_Y + GRID_ROW_H, grid_w, GRID_ROW_H, C_PANEL);
  tft.setCursor(grid_x + 8, GRID_Y + GRID_ROW_H + 28);
  tft.print("Humidity");
  char hum_str[16];
  sprintf(hum_str, "%.1f%%", hum);
  drawRightAligned(hum_str, grid_x, GRID_Y + GRID_ROW_H, grid_w, &FreeSans9pt7b, C_ACCENT);

  // VOC
  tft.fillRect(grid_x, GRID_Y + 2 * GRID_ROW_H, grid_w, GRID_ROW_H, C_PANEL);
  tft.setCursor(grid_x + 8, GRID_Y + 2 * GRID_ROW_H + 28);
  tft.print("VOC Index");
  char voc_str[16];
  sprintf(voc_str, "%.1f", voc);
  drawRightAligned(voc_str, grid_x, GRID_Y + 2 * GRID_ROW_H, grid_w, &FreeSans9pt7b, C_ACCENT);
}

void display_update_time() {
  if (currentDisplayMode != DISPLAY_MAIN) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);

  // Draw time in header
  tft.fillRect(200, 5, 70, 30, C_HEADER);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MAIN);
  tft.setCursor(205, 28);
  tft.print(timeStr);
}

/* ================= OTHER MODES ================= */

void display_cpu_status() {
  currentDisplayMode = DISPLAY_CPU_STATUS;
  tft.fillScreen(C_BG);

  drawPanel(20, 20, SCREEN_W - 40, SCREEN_H - 40, C_PANEL);
  drawCentered("CPU STATUS", 0, 40, SCREEN_W, 40, &FreeSansBold12pt7b, C_TEXT_MAIN);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT_MUTED);
  tft.setCursor(40, 100);
  tft.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  tft.setCursor(40, 130);
  tft.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
  tft.setCursor(40, 160);
  tft.printf("Uptime: %lu seconds\n", millis() / 1000);
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

void display_reinit_layout() {
  currentDisplayMode = DISPLAY_MAIN;
  tft.fillScreen(C_BG);
}

void display_clear_all() {
  tft.fillScreen(C_BG);
}

void display_wake() {
  digitalWrite(TFT_BL, HIGH); // Turn on backlight
  display_reinit_layout();
}

// Draw a colored dot status indicator
void drawStatusDot(int x, int y, uint16_t color) {
  tft.fillCircle(x, y, 4, color);
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






/* ================= SYSTEM SCREENS ================= */