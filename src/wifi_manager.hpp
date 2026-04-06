#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

class WiFiManager_Custom {
public:
    WiFiManager_Custom();

    void begin(const char* hostname);
    void update(uint8_t btnPin, uint8_t ledPin, const char* apName, const char* apPass);

    bool isConnected();
    String getSSID();
    String getPassword();
    void reset();

private:
    Preferences prefs;
    WebServer* server;
    DNSServer* dnsServer;
    
    bool lastState;
    unsigned long configPortalStartTime;
    bool configPortalActive;
    
    static WiFiManager_Custom* inst;

    void startConfigPortal(const char* apName, const char* apPass);
    void stopConfigPortal();
    void handleRoot();
    void handleConfigSave();
    void handleStatus();
    
    static void handleRootStatic();
    static void handleConfigSaveStatic();
    static void handleStatusStatic();
};
