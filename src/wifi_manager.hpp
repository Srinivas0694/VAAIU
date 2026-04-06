#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>

class WiFiManager_Custom {
public:
    WiFiManager_Custom();

    void begin(const char* hostname);
    void update(uint8_t btnPin, uint8_t ledPin, const char* apName, const char* apPass);
    void startPortal(const char* apName, const char* apPass); // New method to start portal on demand

    bool isConnected();
    String getSSID();
    String getPassword();
    void reset();

private:
    WiFiManager manager;
    WebServer* staServer;  // Web server for STA mode (remote access)
    bool lastState;
    bool portalActive;
    unsigned long lastProcessTime;
    static const unsigned long PROCESS_INTERVAL_MS = 20; // Process every 20ms for better responsiveness

    void startConfigPortal(const char* apName, const char* apPass);
    void handlePortalTrigger(); // Handle remote portal trigger requests
    void startSTAServer(); // Start web server for remote access
};
