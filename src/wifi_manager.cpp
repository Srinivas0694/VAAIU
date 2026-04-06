// src/wifi_manager.cpp - WiFiManager wrapper for ESP32
#include "wifi_manager.hpp"

WiFiManager_Custom::WiFiManager_Custom()
    : manager(),
      staServer(nullptr),
      lastState(HIGH),
      portalActive(false),
      lastProcessTime(0)
{
}

void WiFiManager_Custom::begin(const char* hostname) {
    Serial.println("[WiFiManager] Initializing...");

    // Configure WiFiManager for responsive captive portal
    manager.setHostname(hostname);
    manager.setConfigPortalBlocking(false);  // Non-blocking mode
    manager.setConfigPortalTimeout(300);     // 5 minute timeout instead of infinite
    manager.setTimeout(30);                  // 30 second connection timeout
    manager.setSaveConnect(true);            // Save WiFi on connect
    manager.setEnableConfigPortal(true);     // Enable portal
    manager.setBreakAfterConfig(false);      // Don't exit after config
    manager.setAPClientCheck(false);         // Don't require AP client to keep portal open
    manager.setWebPortalClientCheck(true);   // Keep portal open when web clients are connected
    manager.setCaptivePortalEnable(true);    // Enable captive portal

    // Try to auto-connect first, fallback to portal if needed
    if (!manager.autoConnect("AQI_SETUP", "password123")) {
        Serial.println("[WiFiManager] Auto-connect failed, starting config portal");
        manager.startConfigPortal("AQI_SETUP", "password123");
    } else {
        Serial.println("[WiFiManager] Auto-connected successfully");
        // Start STA web server for remote portal access
        startSTAServer();
    }

    Serial.println("[WiFiManager] Initialization complete");
}

void WiFiManager_Custom::update(uint8_t btnPin, uint8_t ledPin, const char* apName, const char* apPass) {
    pinMode(btnPin, INPUT_PULLUP);
    pinMode(ledPin, OUTPUT);

    bool current = digitalRead(btnPin);
    if (lastState == HIGH && current == LOW) {
        Serial.println("[WiFiManager] Button pressed - starting config portal");
        if (manager.getConfigPortalActive()) {
            manager.stopConfigPortal();
            delay(200); // Give time to stop
        }
        manager.startConfigPortal(apName, apPass);
    }
    lastState = current;

    // Process WiFiManager periodically but more frequently for better responsiveness
    unsigned long currentTime = millis();
    if (currentTime - lastProcessTime >= 20) { // Process every 20ms for better responsiveness
        lastProcessTime = currentTime;

        // Check if portal should be active
        bool shouldBeActive = manager.getConfigPortalActive() || manager.getWebPortalActive();

        if (shouldBeActive) {
            portalActive = true;
            manager.process();  // Handle HTTP requests
        } else {
            portalActive = false;
            // Only start portal if button was pressed or if we need to maintain connectivity
            // Don't auto-start portal to avoid conflicts
        }

        // Handle STA web server requests when connected to WiFi
        if (staServer && WiFi.status() == WL_CONNECTED) {
            staServer->handleClient();
        }
    }

    digitalWrite(ledPin, portalActive ? HIGH : LOW);
}

void WiFiManager_Custom::startPortal(const char* apName, const char* apPass) {
    if (!manager.getConfigPortalActive()) {
        Serial.println("[WiFiManager] Starting config portal on demand");
        manager.startConfigPortal(apName, apPass);
    }
}

bool WiFiManager_Custom::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager_Custom::getSSID() {
    return manager.getWiFiSSID(true);
}

String WiFiManager_Custom::getPassword() {
    return manager.getWiFiPass(true);
}

void WiFiManager_Custom::reset() {
    Serial.println("[WiFiManager] Resetting WiFi settings...");
    manager.resetSettings();
}

void WiFiManager_Custom::startSTAServer() {
    if (staServer) {
        delete staServer;
    }

    staServer = new WebServer(80);
    Serial.println("[WiFiManager] Starting STA web server on port 80");

    // Add endpoint for triggering portal remotely
    staServer->on("/trigger-portal", HTTP_GET, std::bind(&WiFiManager_Custom::handlePortalTrigger, this));
    staServer->on("/trigger-portal", HTTP_POST, std::bind(&WiFiManager_Custom::handlePortalTrigger, this));

    // Add a simple status endpoint
    staServer->on("/status", HTTP_GET, [this]() {
        String response = "{";
        response += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
        response += "\"portal_active\":" + String(portalActive ? "true" : "false") + ",";
        response += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        response += "\"ssid\":\"" + WiFi.SSID() + "\"";
        response += "}";
        staServer->send(200, "application/json", response);
    });

    // Add root endpoint with usage instructions
    staServer->on("/", HTTP_GET, [this]() {
        String html = "<html><head><title>AQI Device Remote Control</title></head><body>";
        html += "<h1>AQI Device Remote Control</h1>";
        html += "<p>Device IP: " + WiFi.localIP().toString() + "</p>";
        html += "<p>WiFi SSID: " + WiFi.SSID() + "</p>";
        html += "<p>Portal Status: " + String(portalActive ? "Active" : "Inactive") + "</p>";
        html += "<h2>Endpoints:</h2>";
        html += "<ul>";
        html += "<li><a href='/trigger-portal'>/trigger-portal</a> - Trigger WiFi configuration portal</li>";
        html += "<li><a href='/status'>/status</a> - Get device status (JSON)</li>";
        html += "</ul>";
        html += "<p><strong>Note:</strong> Portal will be accessible at AQI_SETUP WiFi network when triggered.</p>";
        html += "</body></html>";
        staServer->send(200, "text/html", html);
    });

    staServer->begin();
    Serial.println("[WiFiManager] STA web server started successfully");
}

void WiFiManager_Custom::handlePortalTrigger() {
    Serial.println("[WiFiManager] Remote portal trigger requested");

    // Start the configuration portal
    if (!manager.getConfigPortalActive()) {
        manager.startConfigPortal("AQI_SETUP", "password123");
        Serial.println("[WiFiManager] Config portal started via remote request");
    } else {
        Serial.println("[WiFiManager] Config portal already active");
    }

    // Send response
    String response = "{";
    response += "\"success\":true,";
    response += "\"message\":\"WiFi configuration portal triggered\",";
    response += "\"portal_active\":" + String(manager.getConfigPortalActive() ? "true" : "false") + ",";
    response += "\"connect_to\":\"AQI_SETUP\",";
    response += "\"password\":\"password123\"";
    response += "}";

    staServer->send(200, "application/json", response);
}
