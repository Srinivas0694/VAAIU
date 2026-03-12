// Ensure battery macros are defined before use
#include "config.hpp"
#include <driver/adc.h>
// Read battery voltage and convert to percent
static int read_battery_percent() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
  uint16_t raw = adc1_get_raw(ADC1_CHANNEL_7);
  float vbat = (raw / BATTERY_ADC_MAX) * BATTERY_VREF * BATTERY_VOLTAGE_DIVIDER_RATIO;
  float percent = (vbat - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN) * 100.0f;
  if (percent > 100.0f) percent = 100.0f;
  if (percent < 0.0f) percent = 0.0f;
  return (int)percent;
}
/* main.cpp — reorganized, robust, and production-friendly
 *
 * Uses the BLE provisioning module (ble_wifi_*) that is safe:
 *  - BLE initialized once per boot
 *  - advertising started/stopped instead of init/deinit
 *  - WiFi connect initiated non-blocking when credentials arrive
 *
 * Assumes other modules exist (sensors, display, mqtt).
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include "config.hpp"
// Ensure firmware version macro is defined for reset event publishing
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.3"
#endif
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "wifi_ble.hpp"
#include "config.hpp"
#include "sensors.hpp"
#include "display.hpp"
#include "mqtt_client.hpp"

// ------------------------ CONFIG ------------------------
#define WATCHDOG_TIMEOUT_SEC    30
#define TIME_SYNC_TIMEOUT_MS    15000UL

// Deep sleep cycle: 3 minutes active, 2 minutes asleep (total 5‑minute repeating cycle)
static const unsigned long ACTIVE_DURATION_MS       = 3UL * 60UL * 1000UL;   // 3 minutes
static const uint64_t      DEEP_SLEEP_DURATION_US   = 2ULL * 60ULL * 1000000ULL; // 2 minutes

// keep track across deep sleep boots (RTC memory)
RTC_DATA_ATTR static uint32_t bootCount = 0;

// ------------------------ STATE & TIMERS ------------------------
// Timing intervals
static const unsigned long PUBLISH_INTERVAL_MS   = 60000UL; // 1 minute
static const unsigned long SENSOR_INTERVAL_MS    = 60000UL; // 1 minute
static const unsigned long TIME_UPDATE_MS        = 5000UL;  // 5 seconds
static const unsigned long WIFI_RETRY_INTERVAL_MS= 60000UL; // 1 minute
static const unsigned long SETUP_TOGGLE_COOLDOWN_MS = 3000UL; // 3s cooldown
static const unsigned long BLE_PROVISION_TIMEOUT_MS = 120000UL; // 2 minutes

// ------------------------ TFT ------------------------
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST); // from config.hpp

// ------------------------ STATE & TIMERS ------------------------
static unsigned long lastPublishTime = 0;
static unsigned long lastSensorReadTime = 0;
static unsigned long activeStart = 0;  // timestamp when active cycle began
static unsigned long lastTimeUpdate = 0;
static unsigned long lastWiFiRetryTime = 0;
static unsigned long lastSetupToggleTime = 0;
static unsigned long bleProvStartTime = 0;

// WiFi stored flag
static bool hasStoredWiFiCredentials = false;

// Sensor values (persist between reads)
static float last_pm1 = 0, last_pm25 = 0, last_pm4 = 0, last_pm10 = 0;
static float last_voc = 0, last_nox = 0, last_temp = 0, last_hum = 0;
static uint16_t last_co2 = 0;

// ------------------------ BUTTONS & FLAGS ------------------------
static volatile bool bootButtonISRFlag = false;
static unsigned long lastBootHandled = 0;
static const unsigned long BOOT_DEBOUNCE_MS = 200UL;

static volatile bool setupButtonISRFlag = false;
static unsigned long lastSetupHandled = 0;
static const unsigned long SETUP_DEBOUNCE_MS = 200UL;

static bool inSettingsMode = false;
static bool wifiConnected = false;
static bool mqttInitialized = false;
static bool bleAdvertisingActive = false;
static bool showCpuPage = false;

extern String ssid;
extern String pass;

// ------------------------ ISRs ------------------------
void IRAM_ATTR bootISR() {
  bootButtonISRFlag = true;
}

void IRAM_ATTR setupISR() {
  setupButtonISRFlag = true;
}

// ------------------------ FORWARDS ------------------------
static void setupWatchdog();
static void setupButtons();
static void updateWiFiConnectedState();
static void handleButtons();
static void enterSetupMode();
static void exitSetupMode();
static void finalizeProvisioning();
static void tryTimeSyncBlockingWithTimeout();
static void periodicTasks();
static void doSensorReadAndDisplay();
static void doPublishIfNeeded();
static void safeDelayWithWDT(unsigned long ms);

// ------------------------ SETUP ------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  bootCount++;
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.println("\n=== AQI SYSTEM STARTED ===");
  Serial.printf("Boot count: %u\n", bootCount);
  if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Woke from deep sleep timer");
  } else if (wakeReason != ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.printf("Wakeup reason: %d\n", wakeReason);
  }

  setupWatchdog();
  setupButtons();

  // WiFi event logging for debugging
  WiFi.onEvent([](WiFiEvent_t event) {
    Serial.printf("[WiFi-event] %d\n", event);
  });

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("I2C initialized");

  Serial.println("Initializing display...");
  display_init();
  Serial.println("Display initialized");

  Serial.println("Initializing sensors...");
  sensors_init();
  Serial.println("Sensors initialized");

  // Read initial sensor values once
  sensors_read(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum
  );

  // Load saved WiFi credentials from Preferences (wifi_ble module populates ssid/pass)
  loadSavedWiFi(); 
  if (ssid.length() > 0) {
    Serial.println("Saved WiFi credentials found; attempting connection...");
    hasStoredWiFiCredentials = true;
    WiFi.begin(ssid.c_str(), pass.c_str());
    lastWiFiRetryTime = millis();
  } else {
    Serial.println("No WiFi credentials stored - press SETUP button to configure WiFi");
    hasStoredWiFiCredentials = false;
  }

  // Display initial sensor values and time
  int battery_percent = read_battery_percent();
  display_update_with_battery(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum,
    battery_percent
  );
  display_update_time();

  Serial.println("✅ System initialized successfully");

  // begin active cycle timer
  activeStart = millis();

  // Always initialize MQTT after WiFi connects (for non-BLE use)
  if (WiFi.status() == WL_CONNECTED) {
    mqtt_init();
    mqttInitialized = true;
    Serial.println("✅ MQTT initialized (non-BLE mode)");
  }
}

// ------------------------ LOOP ------------------------
void loop() {
  esp_task_wdt_reset();

  updateWiFiConnectedState();

  handleButtons();

  // If in settings mode and BLE module reports connected -> finalize provisioning
  if (inSettingsMode && ble_wifi_connected() && !wifiConnected) {
    finalizeProvisioning();
  }

  periodicTasks();

  if (mqttInitialized) {
    mqtt_loop();
  }

  // check if active period is over

  if (millis() - activeStart >= ACTIVE_DURATION_MS) {
    Serial.println("Active period expired, entering deep sleep");
    // turn off display/backlight
    display_sleep();

    // Publish reset event with reason 'power_off' before sleeping
    if (mqttInitialized && wifiConnected) {
      char isoTime[32];
      time_t nowTime = time(nullptr);
      struct tm* tm_info = gmtime(&nowTime);
      strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", tm_info);
      mqtt_publish_reset_event("ESP32_01", "reset", "power_off", isoTime, FIRMWARE_VERSION);
      delay(100); // allow MQTT to send
    }

    // give serial a moment to flush
    delay(50);

    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US);
    Serial.printf("Going to deep sleep for %llu seconds\n", DEEP_SLEEP_DURATION_US / 1000000ULL);
    esp_deep_sleep_start(); // does not return
  }

  delay(1);
}

// ------------------------ HELPERS ------------------------
static void setupWatchdog() {
  esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.printf("Watchdog enabled (%ds)\n", WATCHDOG_TIMEOUT_SEC);
}

static void setupButtons() {
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON), bootISR, FALLING);
  Serial.println("BOOT_BUTTON attached");

  // NOTE: use a safe GPIO for SETUP_BUTTON (not GPIO34/35/36/39). Prefer 25/26/27/32/33
  pinMode(SETUP_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SETUP_BUTTON), setupISR, FALLING);
  Serial.println("SETUP_BUTTON attached");
}

static void updateWiFiConnectedState() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("✅ WiFi connected");

      // If in provisioning mode, notify BLE and switch to sensor page
      if (inSettingsMode) {
        sendWiFiConnectedToBLE();
        showCpuPage = false;  // Switch to sensor page
        display_reinit_layout();
        display_update(
          last_pm1, last_pm25, last_pm4, last_pm10,
          last_voc, last_nox, last_co2,
          last_temp, last_hum
        );
        display_update_time();
      }
    }
  } else {
    if (wifiConnected) {
      wifiConnected = false;
      Serial.println("WiFi disconnected");
    }
  }
}

static void handleButtons() {
  unsigned long now = millis();

  // Boot button
  if (bootButtonISRFlag) {
    bootButtonISRFlag = false;
    if (now - lastBootHandled > BOOT_DEBOUNCE_MS) {
      lastBootHandled = now;
      if (inSettingsMode) {
        Serial.println("Boot button pressed - Exiting Settings Mode");
        exitSetupMode();
      } else {
        showCpuPage = !showCpuPage;
        Serial.printf("Boot button pressed - Switching to %s\n", showCpuPage ? "CPU Page" : "Sensor Page");
        display_reinit_layout();
        display_update(
          last_pm1, last_pm25, last_pm4, last_pm10,
          last_voc, last_nox, last_co2,
          last_temp, last_hum
        );
        display_update_time();

        // Publish reset event to MQTT
        if (mqttInitialized && wifiConnected) {
          // ISO8601 timestamp
          char isoTime[32];
          time_t nowTime = time(nullptr);
          struct tm* tm_info = gmtime(&nowTime);
          strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", tm_info);
          mqtt_publish_reset_event("ESP32_01", "reset", "power_on", isoTime, FIRMWARE_VERSION);
        }
      }
    }
  }

  // Setup button (enter/exit provisioning) with cooldown to avoid thrash
  if (setupButtonISRFlag) {
    setupButtonISRFlag = false;
    if (now - lastSetupHandled > SETUP_DEBOUNCE_MS) {
      lastSetupHandled = now;

      if (now - lastSetupToggleTime < SETUP_TOGGLE_COOLDOWN_MS) {
        Serial.println("Setup toggle ignored (cooldown)");
        return;
      }
      lastSetupToggleTime = now;

      if (!inSettingsMode) {
        Serial.println("Setup button pressed - Entering WiFi Setup Mode");
        enterSetupMode();
      } else {
        Serial.println("Setup button pressed - Exiting WiFi Setup Mode");
        exitSetupMode();
      }
    }
  }
}

static void enterSetupMode() {
  inSettingsMode = true;
  Serial.println("⏹️  Stopping all sensor and MQTT processes");
  display_wifi_setup_screen();

  // Publish reset event with reason 'setup_mode'
  if (mqttInitialized && wifiConnected) {
    char isoTime[32];
    time_t nowTime = time(nullptr);
    struct tm* tm_info = gmtime(&nowTime);
    strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    mqtt_publish_reset_event("ESP32_01", "reset", "setup_mode", isoTime, FIRMWARE_VERSION);
    delay(100); // allow MQTT to send
  }

  // reset previous attempts so provisioning starts fresh
  resetWiFiCredentials();
  resetWiFiConnectionAttempt();

  // initialize BLE module (safe even if already initialized)
  ble_wifi_init();

  // start advertising (safe)
  ble_start_advertising();
  bleAdvertisingActive = true;
  bleProvStartTime = millis();
}

static void exitSetupMode() {
  inSettingsMode = false;
  Serial.println("✅ Resuming sensor and MQTT processes");

  // Reset timers to resume operations immediately
  lastSensorReadTime = 0;
  lastPublishTime = 0;
  lastTimeUpdate = 0;

  display_reinit_layout();
  display_update(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum
  );
  display_update_time();

  // stop advertising (do not deinit BLE)
  ble_stop_advertising();
  bleAdvertisingActive = false;
  Serial.println("BLE advertising stopped (exitSetupMode)");
}

static void finalizeProvisioning() {
  Serial.println("✅ BLE reported WiFi connected — finalizing provisioning");

  wifiConnected = true;
  inSettingsMode = false;

  // stop advertising (safe)
  if (bleAdvertisingActive) {
    ble_stop_advertising();
    bleAdvertisingActive = false;
  }

  // Reset timers to resume operations immediately
  lastSensorReadTime = 0;
  lastPublishTime = 0;
  lastTimeUpdate = 0;

  // sync time (blocking up to timeout)
  tryTimeSyncBlockingWithTimeout();

  // init MQTT
  mqtt_init();
  mqttInitialized = true;
  Serial.println("✅ MQTT initialized - ready to publish");

  // Read latest sensor data before publishing
  sensors_read(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum
  );

  // Publish immediately after WiFi & MQTT are ready
  Serial.println("📡 Publishing first set of data...");
  doPublishIfNeeded();

  // refresh display
  display_reinit_layout();
  display_update(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum
  );
  display_update_time();
}

static void tryTimeSyncBlockingWithTimeout() {
  Serial.print("Syncing time");
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

  unsigned long start = millis();
  time_t now = 0;
  while ((now = time(nullptr)) < 1700000000) {
    if (millis() - start > TIME_SYNC_TIMEOUT_MS) {
      Serial.println("\n⚠️ Time sync timeout — proceeding without correct RTC");
      break;
    }
    delay(200);
    esp_task_wdt_reset();
    Serial.print(".");
  }

  if (now >= 1700000000) Serial.println("\n✅ Time synced");
}

static void periodicTasks() {
  unsigned long now = millis();

  // Skip all periodic tasks during setup mode
  if (inSettingsMode) {
    return;
  }

  // TIME UPDATE
  if (now - lastTimeUpdate >= TIME_UPDATE_MS) {
    lastTimeUpdate = now;
    display_update_time();
  }

  // SENSOR POLLING
  if (now - lastSensorReadTime >= SENSOR_INTERVAL_MS) {
    lastSensorReadTime = now;
    doSensorReadAndDisplay();
  }

  // WIFI retry
  if (hasStoredWiFiCredentials && !wifiConnected && (now - lastWiFiRetryTime >= WIFI_RETRY_INTERVAL_MS)) {
    lastWiFiRetryTime = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi retry - attempting reconnect");
      WiFi.reconnect();
    }
  }

  // BLE provisioning timeout
  if (inSettingsMode && bleAdvertisingActive) {
    if (now - bleProvStartTime > BLE_PROVISION_TIMEOUT_MS) {
      Serial.println("BLE provisioning timed out — exiting setup mode");
      exitSetupMode();
    }
  }

  // PUBLISH
  if (mqttInitialized && wifiConnected && (now - lastPublishTime >= PUBLISH_INTERVAL_MS)) {
    lastPublishTime = now;
    doPublishIfNeeded();
  }
}

static void doSensorReadAndDisplay() {
  // Skip if in settings mode (should not reach here due to periodicTasks check, but safety first)
  if (inSettingsMode) {
    return;
  }

  sensors_read(
    last_pm1, last_pm25, last_pm4, last_pm10,
    last_voc, last_nox, last_co2,
    last_temp, last_hum
  );

  if (!showCpuPage) {
    int battery_percent = read_battery_percent();
    display_update_with_battery(
      last_pm1, last_pm25, last_pm4, last_pm10,
      last_voc, last_nox, last_co2,
      last_temp, last_hum,
      battery_percent
    );
  }

  Serial.printf("✅ sen5x PM2.5: %.2f\n", last_pm25);
  Serial.printf("✅ scd4x CO2: %u\n", last_co2);
  Serial.printf("✅ bme680 T:%.2f°C H:%.2f\n", last_temp, last_hum);
}

static void doPublishIfNeeded() {
  // Skip publishing if in setup mode or WiFi not connected
  if (inSettingsMode || !wifiConnected) {
    return;
  }

  Serial.println("========== AQI DATA ==========");
  Serial.printf("PM2.5 : %.2f\n", last_pm25);
  Serial.printf("CO2   : %u\n", last_co2);
  Serial.printf("Temp  : %.2f\n", last_temp);
  Serial.printf("Hum   : %.2f\n", last_hum);
  Serial.println("==============================");

  if (mqttInitialized) {
    mqtt_publish(
      last_pm1, last_pm25, last_pm4, last_pm10,
      last_voc, last_nox, last_co2,
      last_temp, last_hum
    );
  }
}

static void safeDelayWithWDT(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    delay(10);
    esp_task_wdt_reset();
  }
}
