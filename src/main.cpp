// Ensure battery macros are defined before use
#include "config.hpp"
#include <driver/adc.h>
// Read battery voltage and convert to percent
static int read_battery_percent() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_12);
  uint16_t raw = adc1_get_raw(ADC1_CHANNEL_7);
  float vbat = (raw / BATTERY_ADC_MAX) * BATTERY_VREF * BATTERY_VOLTAGE_DIVIDER_RATIO;
  float percent = (vbat - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN) * 100.0f;
  if (percent > 100.0f) percent = 100.0f;
  if (percent < 0.0f) percent = 0.0f;
  return (int)percent;
}
/* main.cpp — reorganized, robust, and production-friendly
 *
 * Uses WiFiManager for WiFi provisioning.
 * WiFiManager creates an access point for configuration when needed.
 *
 * Assumes other modules exist (sensors, display, mqtt).
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "config.hpp"
// Ensure firmware version macro is defined for reset event publishing
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.3"
#endif
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "wifi_manager.hpp"
#include "config.hpp"
#include "sensors.hpp"
#include "display.hpp"
#include "mqtt_client.hpp"

// ------------------------ CONFIG ------------------------
#define WATCHDOG_TIMEOUT_SEC    30
#define TIME_SYNC_TIMEOUT_MS    15000UL

// ------------------------ STATE & TIMERS ------------------------
// Timing intervals
static const unsigned long PUBLISH_INTERVAL_MS   = 60000UL; // 1 minute
static const unsigned long SENSOR_INTERVAL_MS    = 60000UL; // 1 minute
static const unsigned long TIME_UPDATE_MS        = 5000UL;  // 5 seconds
static const unsigned long SETUP_TOGGLE_COOLDOWN_MS = 3000UL; // 3s cooldown

// ------------------------ TFT ------------------------
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST); // from config.hpp

// ------------------------ WiFiManager Instance ------------------------
static WiFiManager_Custom wifiManager;

// ------------------------ STATE & TIMERS ------------------------
static unsigned long lastPublishTime = 0;
static unsigned long lastSensorReadTime = 0;
static unsigned long lastTimeUpdate = 0;
static unsigned long lastSetupToggleTime = 0;

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
static bool showCpuPage = false;

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
static void handleSerialCommands();
static void test_wifi_disconnect_during_publish();
static void test_mqtt_disconnect_during_cert();
static void test_network_timeout();
static void run_error_handling_tests();
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
  Serial.println("\n=== AQI SYSTEM STARTED ===");

  setupWatchdog();
  setupButtons();

  // WiFi event logging for debugging
  WiFi.onEvent([](WiFiEvent_t event) {
    Serial.printf("[WiFi-event] %d\n", event);
  });

  // Initialize WiFi FIRST - establish connection before other components
  wifiManager.begin("AQI_Device");
  Serial.println("WiFi initialization complete");

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("I2C initialized");

  Serial.println("Initializing display...");
  display_init();
  Serial.println("Display initialized");

  Serial.println("Initializing sensors...");
  sensors_init();
  Serial.println("Sensors initialized");

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

  // MQTT will be initialized in loop() when WiFi connects
}

// ------------------------ LOOP ------------------------
void loop() {
  esp_task_wdt_reset();

  updateWiFiConnectedState();

  // Initialize MQTT when WiFi becomes connected
  if (wifiConnected && !mqttInitialized) {
    mqtt_init();
    mqttInitialized = true;
    Serial.println("✅ MQTT initialized");
  }

  handleButtons();

  // If in settings mode and WiFi connected -> finalize provisioning
  if (inSettingsMode && wifiManager.isConnected() && !wifiConnected) {
    finalizeProvisioning();
  }

  // Check setup button for WiFi configuration portal
  static bool lastSetupButtonState = HIGH;
  bool currentSetupButtonState = digitalRead(SETUP_BUTTON);
  if (lastSetupButtonState == HIGH && currentSetupButtonState == LOW) {
    Serial.println("Setup button pressed - starting WiFi portal");
    wifiManager.startPortal("AQI_SETUP", "password123");
    digitalWrite(WIFI_CONFIG_LED, HIGH); // Turn on LED when portal starts
  }
  lastSetupButtonState = currentSetupButtonState;

  // Update WiFiManager (handles portal processing)
  wifiManager.update(SETUP_BUTTON, WIFI_CONFIG_LED, "AQI_SETUP", "password123");

  // Small delay to allow other tasks to run
  safeDelayWithWDT(10);

    periodicTasks();

  if (mqttInitialized) {
    mqtt_loop();
  }

  handleSerialCommands();

  // Reduced delay since WiFiManager processing is now optimized
  delay(20);
}

// Handle serial commands for testing
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "test wifi") {
      test_wifi_disconnect_during_publish();
    } else if (command == "test mqtt") {
      test_mqtt_disconnect_during_cert();
    } else if (command == "test timeout") {
      test_network_timeout();
    } else if (command == "test all") {
      run_error_handling_tests();
    } else if (command == "reset wifi") {
      Serial.println("Resetting WiFi settings...");
      wifiManager.reset();
    } else if (command == "status") {
      Serial.println("=== SYSTEM STATUS ===");
      Serial.println(mqtt_get_state_info());
      Serial.print("WiFi Connected: ");
      Serial.println(wifiConnected ? "YES" : "NO");
      if (wifiConnected) {
        Serial.print("WiFi SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("WiFi IP: ");
        Serial.println(WiFi.localIP());
      }
      Serial.println("=====================");
    } else if (command.length() > 0) {
      Serial.println("Available commands:");
      Serial.println("  test wifi    - Test WiFi disconnect during publish");
      Serial.println("  test mqtt    - Test MQTT disconnect during cert creation");
      Serial.println("  test timeout - Test network timeout handling");
      Serial.println("  test all     - Run all error handling tests");
      Serial.println("  reset wifi   - Reset WiFiManager settings and restart");
      Serial.println("  status       - Show current system status");
    }
  }
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

      // If in provisioning mode, switch to sensor page
      if (inSettingsMode) {
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
}

static void finalizeProvisioning() {
  Serial.println("✅ WiFi connected — finalizing provisioning");

  wifiConnected = true;
  inSettingsMode = false;

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

  // PUBLISH
  if (mqttInitialized && wifiConnected && (now - lastPublishTime >= PUBLISH_INTERVAL_MS)) {
    lastPublishTime = now;
    doPublishIfNeeded();
    
    // Debug: Print current states every publish cycle
    Serial.println("=== SYSTEM STATE ===");
    Serial.println(mqtt_get_state_info());
    Serial.print("WiFi Connected: ");
    Serial.println(wifiConnected ? "YES" : "NO");
    Serial.println("====================");
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

// ================= TEST CASES FOR ERROR HANDLING =================
// These functions simulate various failure scenarios for testing

// Test Case 1: Simulate WiFi disconnection during MQTT publishing
void test_wifi_disconnect_during_publish() {
  Serial.println("🧪 TEST: Simulating WiFi disconnect during publish");
  
  // Force disconnect WiFi
  WiFi.disconnect();
  delay(1000);
  
  // Try to publish (should fail and retry)
  if (mqttInitialized) {
    mqtt_publish_with_retry(
      last_pm1, last_pm25, last_pm4, last_pm10,
      last_voc, last_nox, last_co2,
      last_temp, last_hum, 2
    );
  }
  
  Serial.println("🧪 TEST: WiFi disconnect test completed");
}

// Test Case 2: Simulate MQTT connection loss during certificate creation
void test_mqtt_disconnect_during_cert() {
  Serial.println("🧪 TEST: Simulating MQTT disconnect during certificate creation");
  
  if (!provisioningDone && mqtt_is_connected()) {
    // Force disconnect MQTT
    mqtt_force_disconnect();
    delay(2000);
    
    // The mqtt_loop should handle reconnection and retry certificate requests
    Serial.println("🧪 TEST: MQTT disconnect during cert test - check logs for retry behavior");
  } else {
    Serial.println("🧪 TEST: Cannot run test - device already provisioned or MQTT not connected");
  }
}

// Test Case 3: Test provisioning rejection handling
void test_provisioning_rejection() {
  Serial.println("🧪 TEST: Simulating provisioning rejection (cannot actually simulate AWS response)");
  Serial.println("🧪 TEST: This would need to be tested with actual AWS IoT configuration issues");
}

// Test Case 4: Test network timeout scenarios
void test_network_timeout() {
  Serial.println("🧪 TEST: Testing network timeout handling");
  
  // Disconnect WiFi and try operations
  WiFi.disconnect();
  delay(2000);
  
  // Try MQTT operations (should fail gracefully)
  if (mqttInitialized) {
    mqtt_publish_with_retry(
      last_pm1, last_pm25, last_pm4, last_pm10,
      last_voc, last_nox, last_co2,
      last_temp, last_hum, 1
    );
  }
  
  Serial.println("🧪 TEST: Network timeout test completed");
}

// Test Case 5: Test invalid certificate handling
void test_invalid_certificate() {
  Serial.println("🧪 TEST: Testing invalid certificate handling");
  Serial.println("🧪 TEST: This would occur if AWS returns malformed certificates");
  Serial.println("🧪 TEST: Check MQTT callback logs for certificate validation");
}

// Test Case 6: Test sensor read failures
void test_sensor_read_failure() {
  Serial.println("🧪 TEST: Testing sensor read failure handling");
  
  // This would need to be implemented in sensors.cpp
  // For now, just log that sensors should handle read failures gracefully
  Serial.println("🧪 TEST: Sensor read failures should be handled in sensors.cpp");
  Serial.println("🧪 TEST: Check sensor logs for timeout/retry behavior");
}

// Run all test cases (call this from setup for testing)
void run_error_handling_tests() {
  Serial.println("🚀 Running error handling test cases...");
  
  delay(5000); // Wait for system to stabilize
  
  test_wifi_disconnect_during_publish();
  delay(10000);
  
  test_mqtt_disconnect_during_cert();
  delay(10000);
  
  test_network_timeout();
  delay(10000);
  
  test_invalid_certificate();
  delay(5000);
  
  test_sensor_read_failure();
  
  Serial.println("✅ All error handling tests completed");
}
