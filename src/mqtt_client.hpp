#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include <Arduino.h>   // ✅ REQUIRED
#include <stdint.h>

extern bool provisioningDone;

void mqtt_init();
void mqtt_loop();

void mqtt_publish(
  float pm1, float pm25, float pm4, float pm10,
  float voc, float nox,
  uint16_t co2,
  float temp, float hum
);

// Publishes a reset event with metadata
void mqtt_publish_reset_event(const char* device_id, const char* event, const char* reset_reason, const char* timestamp, const char* firmware_version);

// Get current MQTT connection state for debugging
String mqtt_get_state_info();

bool mqtt_is_connected();
void mqtt_force_disconnect();

// Test function for publish with retry
bool mqtt_publish_with_retry(
  float pm1, float pm25, float pm4, float pm10,
  float voc, float nox,
  uint16_t co2,
  float temp, float hum,
  int maxRetries
);

#endif
