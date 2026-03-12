
#pragma once

// ================= BATTERY =================
#define BATTERY_ADC_PIN 35  // Example: GPIO35 (VP, input only)
#define BATTERY_VOLTAGE_DIVIDER_RATIO 2.0f // Adjust if using a resistor divider
#define BATTERY_ADC_MAX 4095.0f
#define BATTERY_VREF 3.3f
#define BATTERY_VOLTAGE_MIN 3.2f // 0% (adjust for your battery)
#define BATTERY_VOLTAGE_MAX 4.2f // 100% (adjust for your battery)

// ================= I2C =================
#define I2C_SDA 21
#define I2C_SCL 22

// ================= GPIO =================
#define BOOT_BUTTON 0  // ESP32 Boot button GPIO for toggling pages
#define SETUP_BUTTON 32  // Exit button GPIO

// ================= TFT =================
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   27
#define TFT_RST  4
#define TFT_BL   13  // Backlight pin

// ============== SCREEN ==============
#define SCREEN_W 320
#define SCREEN_H 240

// ============== LAYOUT ==============

// Outer margin
#define MARGIN   10
#define GAP      8

// Title
#define TITLE_X  MARGIN
#define TITLE_Y  MARGIN

// PARAMETER TABLE (LEFT)
#define PARAM_X  MARGIN
#define PARAM_Y  (TITLE_Y + 20)
#define PARAM_W  180
#define PARAM_H  (SCREEN_H - PARAM_Y - MARGIN)

// WIFI TABLE (TOP RIGHT)
#define WIFI_X   (PARAM_X + PARAM_W + GAP)
#define WIFI_Y   PARAM_Y
#define WIFI_W   (SCREEN_W - WIFI_X - MARGIN)
#define WIFI_H   45

// DATE/TIME TABLE (BOTTOM RIGHT)
#define DT_X     WIFI_X
#define DT_Y     (WIFI_Y + WIFI_H + GAP)
#define DT_W     WIFI_W
#define DT_H     (PARAM_H - WIFI_H - GAP)

// PARAM TEXT
#define LABEL_X  (PARAM_X + 10)
#define VALUE_X  (PARAM_X + 95)
#define LINE_H   14
