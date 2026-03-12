#pragma once
#include <stdint.h>

void sensors_init();
void sensors_read(
  float &pm1, float &pm25, float &pm4, float &pm10,
  float &voc, float &nox,
  uint16_t &co2,
  float &temp, float &hum
);
