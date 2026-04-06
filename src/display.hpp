#pragma once
#include <stdint.h>
#include <Arduino.h>

enum DisplayMode {
  DISPLAY_MAIN,
  DISPLAY_WIFI_SETUP,
  DISPLAY_CPU_STATUS
};

extern DisplayMode currentDisplayMode;

void display_init();
void display_update(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum);
void display_update_with_battery(float pm1, float pm25, float pm4, float pm10, float voc, float nox, uint16_t co2, float temp, float hum, int battery_percent);
void display_update_time();
void display_cpu_status();
void display_wifi_setup_screen();
void display_reinit_layout();
void display_clear_all();

// power management
void display_wake();       // re-enable backlight and redraw layout
