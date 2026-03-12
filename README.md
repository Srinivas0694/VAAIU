# AQI_TFT_MQTT

This project runs on an ESP32, reads air‑quality sensors, displays values on a TFT, and publishes to MQTT.

## Power-saving cycle

The firmware now implements an automatic deep-sleep cycle to reduce power consumption. After booting the device stays fully active for **3 minutes** (display on, sensors polled, Wi‑Fi/MQTT running) and then enters deep sleep for **2 minutes**. Upon wake the system reinitializes and repeats the cycle.

Durations are controlled by `ACTIVE_DURATION_MS` and `DEEP_SLEEP_DURATION_US` constants in `src/main.cpp` and can be adjusted as needed.
