#pragma once
#include <Arduino.h>

void ble_wifi_init();            // initialize BLE stack & service (idempotent)
void ble_start_advertising();    // start advertising (safe)
void ble_stop_advertising();     // stop advertising (safe)
bool ble_wifi_connected();       // returns true when WiFi status is WL_CONNECTED (also triggers connect when credentials received)
void clearWiFiCredentials();     // erase saved credentials in NVS
void updateWiFiStatus();         // update/notify statusChar (if connected)
void sendWiFiConnectedToBLE();   // send "Connected" status via BLE
void loadSavedWiFi();            // populate extern ssid/pass from Preferences
void resetWiFiConnectionAttempt();
void resetWiFiCredentials();

// Expose the saved creds as extern (main uses loadSavedWiFi() to populate)
extern String ssid;
extern String pass;
