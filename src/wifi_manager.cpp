// src/wifi_manager.cpp - Lightweight custom WiFi configuration for ESP32
#include "wifi_manager.hpp"

WiFiManager_Custom* WiFiManager_Custom::inst = nullptr;

WiFiManager_Custom::WiFiManager_Custom() :
    server(nullptr),
    dnsServer(nullptr),
    lastState(HIGH),
    configPortalStartTime(0),
    configPortalActive(false)
{
    inst = this;
}

void WiFiManager_Custom::begin(const char* hostname) {
    Serial.println("[WiFiManager] Initializing...");

    WiFi.setHostname(hostname);

    // Load saved credentials
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    prefs.end();

    // Try to connect to saved WiFi if available
    if (ssid.length() > 0) {
        Serial.print("[WiFiManager] Connecting to saved WiFi: ");
        Serial.println(ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

            if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WiFiManager] Connected!");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            return;
        }
        Serial.println("\n[WiFiManager] Connection failed - starting config portal");
    } else {
        Serial.println("[WiFiManager] No saved credentials - starting config portal");
    }

    // If no saved WiFi or connection failed, start config portal immediately
    startConfigPortal("AQI_SETUP", "password123");
}

void WiFiManager_Custom::update(uint8_t btnPin, uint8_t ledPin, const char* apName, const char* apPass) {
    pinMode(btnPin, INPUT_PULLUP);
    pinMode(ledPin, OUTPUT);

    bool current = digitalRead(btnPin);

    // Button pressed (HIGH to LOW transition) - restart portal if needed
    if (lastState == HIGH && current == LOW) {
        digitalWrite(ledPin, HIGH);
        Serial.println("[WiFiManager] Button pressed - restarting config portal");
        if (configPortalActive) {
            stopConfigPortal();
        }
        startConfigPortal(apName, apPass);
    }

    lastState = current;

    // Handle config portal timeout (60 seconds)
    if (configPortalActive) {
        if (millis() - configPortalStartTime > 60000) {
            Serial.println("[WiFiManager] Config portal timeout");
            stopConfigPortal();
        }

        // Process DNS and HTTP requests
        if (dnsServer) {
            dnsServer->processNextRequest();
        }
        if (server) {
            server->handleClient();
            yield();  // Let WiFi stack run
        }
    }

    digitalWrite(ledPin, configPortalActive ? HIGH : LOW);
}

void WiFiManager_Custom::startConfigPortal(const char* apName, const char* apPass) {
    if (configPortalActive) {
        Serial.println("[WiFiManager] Config portal already active");
        return;
    }

    configPortalActive = true;
    configPortalStartTime = millis();

    // Stop any existing servers
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    // Switch to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    delay(100); // Give WiFi time to switch modes

    bool apStarted = WiFi.softAP(apName, apPass);
    if (!apStarted) {
        Serial.println("[WiFiManager] ERROR: Failed to start AP!");
        configPortalActive = false;
        return;
    }

    Serial.println("[WiFiManager] Access Point started successfully");
    Serial.print("AP SSID: ");
    Serial.println(apName);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("AP MAC: ");
    Serial.println(WiFi.softAPmacAddress());

    // Start DNS Server - redirect all requests to AP IP
    dnsServer = new DNSServer();
    dnsServer->start(53, "*", WiFi.softAPIP());
    Serial.println("[WiFiManager] DNS server started (captive portal enabled)");

    // Start web server
    server = new WebServer(80);

    server->on("/", [this]() { handleRoot(); });
    server->on("/save", HTTP_POST, [this]() { handleConfigSave(); });
    server->on("/status", [this]() { handleStatus(); });
    server->on("/exit", [this]() {
        server->send(200, "text/plain", "Exiting...");
        stopConfigPortal();
    });
    
    // Catch-all for captive portal
    server->onNotFound([this]() { handleRoot(); });

    server->begin();
    Serial.println("[WiFiManager] Web server started on port 80");
}

void WiFiManager_Custom::stopConfigPortal() {
    if (!configPortalActive) return;

    configPortalActive = false;

    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }

    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }

    // Switch back to STA mode
    WiFi.mode(WIFI_STA);
    Serial.println("[WiFiManager] Config portal stopped");
}

void WiFiManager_Custom::handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Configuration</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 400px;
            margin: 50px auto;
            padding: 20px;
            background: #f5f5f5;
        }
        .container {
            background: white;
            border-radius: 8px;
            padding: 30px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            text-align: center;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #555;
            font-weight: bold;
        }
        input {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
            font-size: 14px;
        }
        input:focus {
            outline: none;
            border-color: #4CAF50;
            box-shadow: 0 0 5px rgba(76,175,80,0.3);
        }
        .button-group {
            display: flex;
            gap: 10px;
            margin-top: 30px;
        }
        button {
            flex: 1;
            padding: 12px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
            font-weight: bold;
            transition: background 0.3s;
        }
        .save-btn {
            background: #4CAF50;
            color: white;
        }
        .save-btn:hover {
            background: #45a049;
        }
        .exit-btn {
            background: #f44336;
            color: white;
        }
        .exit-btn:hover {
            background: #da190b;
        }
        .status {
            margin-top: 20px;
            padding: 10px;
            border-radius: 4px;
            text-align: center;
            font-weight: bold;
            display: none;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
            display: block;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
            display: block;
        }
        .timer {
            text-align: center;
            color: #ff9800;
            margin-top: 15px;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 WiFi Configuration</h1>
        
        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">WiFi SSID:</label>
                <input type="text" id="ssid" name="ssid" placeholder="Network Name" required>
            </div>
            
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password" placeholder="WiFi Password" required>
            </div>
            
            <div class="button-group">
                <button type="button" class="save-btn" onclick="saveConfig()">Save</button>
                <button type="button" class="exit-btn" onclick="exitPortal()">Exit</button>
            </div>
            
            <div id="status" class="status"></div>
        </form>
    </div>

    <script>
        function saveConfig() {
            const ssid = document.getElementById('ssid').value.trim();
            const password = document.getElementById('password').value.trim();

            if (!ssid) {
                showStatus('Please enter SSID', false);
                return;
            }

            const formData = new FormData();
            formData.append('ssid', ssid);
            formData.append('password', password);

            fetch('/save', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(data => {
                showStatus('✔ Saved! Device will reconnect...', true);
                setTimeout(() => {
                    window.location.href = '/status';
                }, 2000);
            })
            .catch(err => {
                showStatus('✗ Save failed: ' + err, false);
            });
        }

        function exitPortal() {
            fetch('/exit')
                .then(() => {
                    showStatus('Exiting...', true);
                    setTimeout(() => {
                        window.close();
                    }, 1000);
                });
        }

        function showStatus(message, isSuccess) {
            const elem = document.getElementById('status');
            elem.textContent = message;
            elem.className = 'status ' + (isSuccess ? 'success' : 'error');
        }
    </script>
</body>
</html>
    )rawliteral";

    server->send(200, "text/html", html);
}

void WiFiManager_Custom::handleConfigSave() {
    if (server->hasArg("ssid")) {
        String ssid = server->arg("ssid");
        String password = server->arg("password");

        Serial.println("[WiFiManager] Saving credentials...");
        Serial.print("SSID: ");
        Serial.println(ssid);

        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("password", password);
        prefs.end();

        server->send(200, "text/plain", "Saved");

        // Give it time to respond, then try to connect
        delay(500);
        WiFi.begin(ssid.c_str(), password.c_str());
    } else {
        server->send(400, "text/plain", "Missing SSID");
    }
}

void WiFiManager_Custom::handleStatus() {
    String status = "WiFi: ";
    if (WiFi.status() == WL_CONNECTED) {
        status += "Connected\nSSID: " + WiFi.SSID() + "\nIP: " + WiFi.localIP().toString();
    } else {
        status += "Not Connected";
    }
    server->send(200, "text/plain", status);
}

bool WiFiManager_Custom::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager_Custom::getSSID() {
    prefs.begin("wifi", true);
    String v = prefs.getString("ssid", "");
    prefs.end();
    return v;
}

String WiFiManager_Custom::getPassword() {
    prefs.begin("wifi", true);
    String v = prefs.getString("password", "");
    prefs.end();
    return v;
}

void WiFiManager_Custom::reset() {
    Serial.println("[WiFiManager] Resetting WiFi settings...");
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    WiFi.disconnect(true);
}
