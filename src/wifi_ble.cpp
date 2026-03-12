// src/wifi_ble.cpp
#include "wifi_ble.hpp"

#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

#define BLE_NAME "AQI_SETUP"

#define SERVICE_UUID              "8d53dc1d-1db7-4cd3-868b-8a527460aa84"
#define WIFI_CONFIG_CHAR_UUID     "8d53dc1d-1db7-4cd3-868b-8a527460aa85"
#define WIFI_STATUS_CHAR_UUID     "8d53dc1d-1db7-4cd3-868b-8a527460aa86"

// Preferences namespace for WiFi
static Preferences wifiPrefs;

// Exposed globals (used by main.cpp)
String ssid = "";
String pass = "";

// Internal state
static bool bleInitialized = false;
static bool bleAdvertisingActive = false;
static bool wifiConnectionAttempted = false;

static BLEServer* bleServer = nullptr;
static BLECharacteristic* configChar = nullptr;
static BLECharacteristic* statusChar = nullptr;
static BLEAdvertising* bleAdvertising = nullptr;

// Forward declaration (used by WiFiConfigCallback)
void connectWiFiNonBlocking();

/* -------------------- BLE Characteristic Callback -------------------- */
class WiFiConfigCallback: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string value = c->getValue();
    if (value.empty()) return;

    String data = String(value.c_str());
    data.trim();

    const int SSID_MAX_LEN = 32;
    const int PASS_MAX_LEN = 63;
    const int SSID_MIN_LEN = 1;

    int colonIndex = data.indexOf(':');
    if (colonIndex > 0 && data.length() <= (SSID_MAX_LEN + PASS_MAX_LEN + 1)) {
      String temp_ssid = data.substring(0, colonIndex);
      String temp_pass = data.substring(colonIndex + 1);

      if (temp_ssid.length() >= SSID_MIN_LEN && temp_ssid.length() <= SSID_MAX_LEN &&
          temp_pass.length() <= PASS_MAX_LEN) {
        // Save to globals and NVS immediately
        ssid = temp_ssid;
        pass = temp_pass;

        Serial.println("✅ WiFi Config received:");
        Serial.println("  SSID: " + ssid);
        Serial.println("  PASS length: " + String(pass.length()));

        // Persist credentials to Preferences
        wifiPrefs.begin("wifi", false);
        wifiPrefs.putString("ssid", ssid);
        wifiPrefs.putString("pass", pass);
        wifiPrefs.end();

        // Trigger WiFi connection immediately
        wifiConnectionAttempted = false;
        connectWiFiNonBlocking();

        // Notify client that we got credentials and are connecting
        if (statusChar) {
          String s = "Connecting...";
          statusChar->setValue(s.c_str());
          statusChar->notify();
        }

      } else {
        Serial.println("❌ SSID/PASS length invalid");
        if (statusChar) {
          String errorStatus = "Length invalid";
          statusChar->setValue(errorStatus.c_str());
          statusChar->notify();
        }
      }
    } else {
      Serial.println("❌ Invalid format. Use: SSID:PASSWORD");
      if (statusChar) {
        String invalidStatus = "Invalid format";
        statusChar->setValue(invalidStatus.c_str());
        statusChar->notify();
      }
    }
  }
};

/* -------------------- Internal helper (static) --------------------
   Notify BLE client about current WiFi status (internal name to avoid duplication)
*/
static void notifyWiFiStatus_internal() {
  if (!statusChar) return;

  String status = "Disconnected";
  wl_status_t wifiStatus = WiFi.status();

  if (wifiStatus == WL_CONNECTED) status = "Connected";
  else if (wifiStatus == WL_NO_SSID_AVAIL) status = "SSID not found";
  else if (wifiStatus == WL_CONNECT_FAILED) status = "Connect failed";
  else if (wifiStatus == WL_IDLE_STATUS) status = "Idle";
  else status = "Disconnected";

  statusChar->setValue(status.c_str());
  statusChar->notify();
  Serial.println("WiFi Status (notif): " + status);
}

/* -------------------- Public APIs -------------------- */

// Clear stored WiFi credentials from NVS
void clearWiFiCredentials() {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.clear();
  wifiPrefs.end();
  ssid = "";
  pass = "";
  wifiConnectionAttempted = false;
  Serial.println("WiFi credentials erased");
}

// Load saved credentials from Preferences into globals (ssid, pass)
void loadSavedWiFi() {
  wifiPrefs.begin("wifi", true); // read-only
  ssid = wifiPrefs.getString("ssid", "");
  pass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
}

// Reset connection attempt flag so connect can be retried
void resetWiFiConnectionAttempt() {
  wifiConnectionAttempted = false;
}

// Reset credentials in memory (not in NVS)
void resetWiFiCredentials() {
  ssid = "";
  pass = "";
  wifiConnectionAttempted = false;
}

// Initialize BLE stack & create service/characteristics once (idempotent)
void ble_wifi_init() {
  if (bleInitialized) {
    Serial.println("BLE already initialized");
    return;
  }

  BLEDevice::init(BLE_NAME);
  bleServer = BLEDevice::createServer();

  BLEService *service = bleServer->createService(SERVICE_UUID);

  configChar = service->createCharacteristic(
    WIFI_CONFIG_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  statusChar = service->createCharacteristic(
    WIFI_STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  // Add client config descriptor so mobile apps can subscribe to notifications
  statusChar->addDescriptor(new BLE2902());

  configChar->setCallbacks(new WiFiConfigCallback());

  // initial status
  String initStatus = "Waiting for credentials";
  statusChar->setValue(initStatus.c_str());

  service->start();

  bleAdvertising = BLEDevice::getAdvertising();
  bleAdvertising->addServiceUUID(SERVICE_UUID);

  bleInitialized = true;
  bleAdvertisingActive = false;

  Serial.println("BLE initialized (provisioning module ready)");
}

// Start advertising (safe). If BLE not initialized, it will be initialized first.
void ble_start_advertising() {
  if (!bleInitialized) ble_wifi_init();

  if (bleAdvertisingActive) {
    Serial.println("BLE advertising already active");
    return;
  }

  bleAdvertising->start();
  bleAdvertisingActive = true;
  Serial.println("BLE advertising started");

  // Send a fresh status
  notifyWiFiStatus_internal();
}

// Stop advertising (safe). Does not deinit BLE stack.
void ble_stop_advertising() {
  if (!bleInitialized || !bleAdvertisingActive) return;

  bleAdvertising->stop();
  bleAdvertisingActive = false;
  Serial.println("BLE advertising stopped");
}

// Called by main loop: if credentials exist and we haven't tried connecting this boot, start non-blocking connect.
void connectWiFiNonBlocking() {
  if (ssid.length() == 0) return;
  if (wifiConnectionAttempted) return;

  Serial.println("Initiating WiFi connect (non-blocking)...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(50);
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiConnectionAttempted = true;
}

// Public wrapper used by main to check connection & trigger connect if creds are present
bool ble_wifi_connected() {
  if (!wifiConnectionAttempted && ssid.length() > 0) {
    connectWiFiNonBlocking();
  }
  return WiFi.status() == WL_CONNECTED;
}

// Public function to send status notification (thin wrapper)
void updateWiFiStatus() {
  notifyWiFiStatus_internal();
}

// Send "Connected" status via BLE (used when WiFi successfully connects)
void sendWiFiConnectedToBLE() {
  if (!statusChar) return;

  String connectedMsg = "Connected";
  statusChar->setValue(connectedMsg.c_str());
  statusChar->notify();
  Serial.println("📱 BLE notified: WiFi Connected");
}
