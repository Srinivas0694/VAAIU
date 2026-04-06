#include "mqtt_client.hpp"
#define DEVICE_TOKEN "AQI_ESP32_001"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define FIRMWARE_VERSION "1.0.3"

String lastDeviceToken = "";
unsigned long lastTimestamp = 0;

String generateMessageToken() {
  uint64_t chipId = ESP.getEfuseMac();
  unsigned long ts = time(nullptr);
  uint16_t rnd = random(1000, 9999);

  char token[64];
  snprintf(token, sizeof(token), "%llX-%lu-%u", chipId, ts, rnd);

  return String(token);
}

// ================= MQTT SETTINGS =================
#define MQTT_ENDPOINT "au7tuevsm9a5y-ats.iot.ap-south-1.amazonaws.com"
#define MQTT_PORT     8883
String clientId = "aqi-esp32-";

// Fleet provisioning template
#define TEMPLATE_NAME "AQI_Fleet_Template"

// MQTT topics
#define CREATE_CERT_PUB "$aws/certificates/create/json"
#define CREATE_CERT_SUB "$aws/certificates/create/json/accepted"

#define PROVISION_PUB  "$aws/provisioning-templates/" TEMPLATE_NAME "/provision/json"
#define PROVISION_SUB  "$aws/provisioning-templates/" TEMPLATE_NAME "/provision/json/accepted"
#define PROVISION_REJ  "$aws/provisioning-templates/" TEMPLATE_NAME "/provision/json/rejected"

// Data topic
//#define DATA_TOPIC "aqi/device/data"

// ================= FLAGS =================
bool provisioningDone = false;
static bool certRequested = false;
static bool provisionRequested = false;

// ================= ERROR HANDLING & RETRY =================
#define MQTT_RECONNECT_INTERVAL_BASE 5000
#define MQTT_MAX_RETRIES 10
#define CERT_REQUEST_TIMEOUT 30000  // 30 seconds
#define PROVISION_TIMEOUT 60000     // 60 seconds
#define PUBLISH_RETRY_MAX 3

static unsigned long lastCertRequestTime = 0;
static unsigned long lastProvisionRequestTime = 0;
static int certRequestRetries = 0;
static int provisionRetries = 0;
static int publishRetries = 0;

// Connection state tracking
enum MqttState {
  MQTT_STATE_DISCONNECTED,
  MQTT_STATE_CONNECTING,
  MQTT_STATE_CONNECTED,
  MQTT_STATE_CERT_REQUESTING,
  MQTT_STATE_PROVISIONING,
  MQTT_STATE_READY
};
static MqttState currentMqttState = MQTT_STATE_DISCONNECTED;
// ================= CERTIFICATES =================
// ✅ Amazon Root CA (KEEP – public, safe)
static const char AWS_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// 🔴 CLAIM CERTIFICATE (ONE TIME, SAME FOR ALL DEVICES)
static const char CLAIM_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUbhmjTi5l5Uqxdo/HVKLQAsVLvLAwDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDEwNzA1NDUx
NFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAOYZ4LxYQI1tLoKOn6D8
iZLzC4gX2uubwfbpFpBtPBctKRohjyyHUm4ifoUNc6gWrNUQy92gp952gyNNY5fE
Ol7gZhxCV/yDWZVTdk1YytX3NFNZi6F+FX2g/gqeH7wES8QKfvMWD3NOkO0vtFmw
75us2waZ1QJXQaclGh6uc4RQrMeO2Y/Fk/uSmJ7CSZrM6pXnuLprBqisvjuD8xZp
R6CwXxJLqA0XZ7cRBo3woxgeKUIQfwvWXIU0ersrvu87hIjbFtc2YDL6mmXLHn1A
lKzGWPMGMcYkzJWhQ/cAUt9oUUQv8eQDgogyz+UNXBI4hCm3mPLpJhKnnDud+W1q
XgsCAwEAAaNgMF4wHwYDVR0jBBgwFoAUip2lD7DXg2qbivo9isKd/iGROuEwHQYD
VR0OBBYEFLAB3CVzwmRfkxZKtWZW72tLUfxwMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBYFHO5pLgmCvn+BSLShQLENRKH
JOjH8b1hK3awXl07sVppreoof7CDy5KLxqwMTFGBQLAM7UbV6pONXok6m0g3pu9E
5PgevuWZACQ+rnoW2sDCecM+nlFOgODKDBtgLqchrotqqYD6u9NfgI5ObBGr1wk9
hNh6/f62/0TEIrOY5M/yY9puHvCgiC9MxsUQPtORBEFbDF/DbM7P/0dx1MouNbYE
R3jJGce6Pvvta5bF/a/uZAsAwRL6X5bFWkJZ+pNAKv2De5HhccFwRhVNGFYbOhMx
eH79s8fwy75je0/6P5SRqfTHXA4ahMgzTZRwsLZiMdMIMrN9xgflXlcILfC5
-----END CERTIFICATE-----
)EOF";

static const char CLAIM_PRIVATE_KEY[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEA5hngvFhAjW0ugo6foPyJkvMLiBfa65vB9ukWkG08Fy0pGiGP
LIdSbiJ+hQ1zqBas1RDL3aCn3naDI01jl8Q6XuBmHEJX/INZlVN2TVjK1fc0U1mL
oX4VfaD+Cp4fvARLxAp+8xYPc06Q7S+0WbDvm6zbBpnVAldBpyUaHq5zhFCsx47Z
j8WT+5KYnsJJmszqlee4umsGqKy+O4PzFmlHoLBfEkuoDRdntxEGjfCjGB4pQhB/
C9ZchTR6uyu+7zuEiNsW1zZgMvqaZcsefUCUrMZY8wYxxiTMlaFD9wBS32hRRC/x
5AOCiDLP5Q1cEjiEKbeY8ukmEqecO535bWpeCwIDAQABAoIBAQCiPYLRKiSx01bk
5S02dHmILGhoF/HbCGLV9nlbcjnZWZVOgHUT/4Imd5nftKFk1kAcpxAnf3x9hfBm
9s0PGGPTu8Mjj7+8It0KReP3G3FBNmEll9C1GFKM1vPohp93kUveuUvTmC1irvXO
10EBsJjxmgqA9/xR/zYiZS2qjnSSyH0WcMg82OHIyGlRXFglooBi+aECDIgvsf4Q
TELYTOrN5GRnKtyOaF42m7lVR9ZiXw767sjMHGqHllkIhEguMHbUKCw0VSl/AvoH
FuIioJcyru8A6zpu4gs7PRN+vEWtJN4WCKR80zCLzSfIWnSL/qXZLDdJfCbUguog
0rt3cjCJAoGBAPRSOJ8igyxTUqBNrNou29Dy4EYB5SW1WuZ2/LjuXsDC+U5eA0S9
RfMtxSX7YyAvvW98PR3jrJoqd4fAWjZqVgwbOMu9hatH2bCo3qXl8Sw2Q8GlZfAq
SatLAByk47AoTbRH+KbizhAhF8Xzq2zn+zhM4APHpRnse2ednGCucDn3AoGBAPEZ
pO0mlwO44UjU3HP7MVY8nDQ5rjyU4hF1ypI5McWdGyIrPLdybWHFtgCwJv020/ZU
v8ZYroGYyYs0UR9J88ZUNe7/9Ggy7jRsSIXx/b5CloxKWb2Zl86IYfiJPf1s+Da7
rnZH7eO8S+vE3IZiNDNQ0pgXG9MxmmvUrRqTdNeNAoGBAMFjr6Pu6ouUbKussCyH
uMEc7n8bkukVMx2Hg6VSMTg9XJ82dTnc49iIGyxkXuMkRrtPSQU2qPHiuXh/viii
ZPfyODO1EXAxUFOwZ4RDjXHkhh9qr1S59FQc6rrBneRbEp6n9V8L2fYUa7/pj4CF
42l+GDeFuN7bPBcZP6WbjgOjAoGBAIYdpcjl9otzhJ5nClraGI/IF3xVub6pczgT
NiQw/KbYxgcz1gqV9JK20I+Bba7dmPsmGafUHP6qZzKwX4/wK1Lf9UCF0QdFjYxL
z+vyuRvlHqnbkiAOGIwjAZyne3xF6a1IqbvSN1q/m9wDHkkCx9bj1sIT2L6akTP6
knB+JjjhAoGAAPEsc+Oz14CQ6pZW0MFJI8l87csVN5Piim0n58+5DJo61sLKY57P
moMXM+wg1ks4jfBjLjKMfPQFHHP+R8oxXThEuzfBQytbAnryRW2Wvk4tMwGBh00g
OtvLTXmgPR2JQJwC3QuQJppUEhJhBZDOwfAXnWRsxs7FdfRAvTuIx4s=
-----END RSA PRIVATE KEY-----
)EOF";

// ================= GLOBALS =================
WiFiClientSecure net;
PubSubClient mqtt(net);

String ownershipToken = "";
String certificatePem = "";
String privateKey = "";
bool usingProvisionedCerts = false;

// ================= CALLBACK =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {

  DynamicJsonDocument doc(6144);
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("❌ JSON Parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  String t = String(topic);
  Serial.print("📩 MQTT RX: ");
  Serial.println(t);

  // Certificate created
  if (t == CREATE_CERT_SUB) {
    if (doc.containsKey("certificateOwnershipToken") && 
        doc.containsKey("certificatePem") && 
        doc.containsKey("privateKey")) {
      
      ownershipToken = doc["certificateOwnershipToken"].as<String>();
      certificatePem = doc["certificatePem"].as<String>();
      privateKey = doc["privateKey"].as<String>();
      
      // Validate certificate format
      if (certificatePem.startsWith("-----BEGIN CERTIFICATE-----") && 
          privateKey.startsWith("-----BEGIN RSA PRIVATE KEY-----")) {
        Serial.println("✅ Valid certificate and private key received");
        currentMqttState = MQTT_STATE_CERT_REQUESTING; // Will transition to provisioning
      } else {
        Serial.println("❌ Invalid certificate format received");
        // Reset and retry
        certRequested = false;
        certRequestRetries = 0;
        lastCertRequestTime = 0;
      }
    } else {
      Serial.println("❌ Certificate response missing required fields");
      certRequested = false;
      certRequestRetries = 0;
      lastCertRequestTime = 0;
    }
  }

  // Provision success
  if (t == PROVISION_SUB) {
    provisioningDone = true;
    Serial.println("🎉 Device provisioned successfully. Switching credentials...");
    currentMqttState = MQTT_STATE_READY;
    mqtt.disconnect(); // Trigger reconnect with new certs
  }

  // Provision rejected
  if (t == PROVISION_REJ) {
    Serial.println("❌ Provision rejected!");
    Serial.print("Error Detail: ");
    serializeJson(doc, Serial);
    Serial.println();
    
    // Check for common errors
    if (doc.containsKey("errorMessage")) {
      String errorMsg = doc["errorMessage"].as<String>();
      Serial.print("Message: ");
      Serial.println(errorMsg);
      
      // Handle specific error types
      if (errorMsg.indexOf("CertificateAlreadyProvisioned") >= 0) {
        Serial.println("ℹ️  Certificate already provisioned - device may already be registered");
        provisioningDone = true;
        currentMqttState = MQTT_STATE_READY;
        return;
      } else if (errorMsg.indexOf("ResourceConflict") >= 0) {
        Serial.println("ℹ️  Resource conflict - device ID may already exist");
      } else if (errorMsg.indexOf("ValidationException") >= 0) {
        Serial.println("ℹ️  Validation error - check device parameters");
      }
    }
    
    // Reset provisioning state for retry
    provisionRequested = false;
    provisionRetries = 0;
    lastProvisionRequestTime = 0;
    currentMqttState = MQTT_STATE_CONNECTED;
  }
}

// ================= INIT =================
void mqtt_init() {
  clientId += String(ESP.getEfuseMac(), HEX);

  net.setCACert(AWS_ROOT_CA);
  net.setCertificate(CLAIM_CERT);
  net.setPrivateKey(CLAIM_PRIVATE_KEY);

  mqtt.setServer(MQTT_ENDPOINT, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(4096);
}

#define MQTT_RECONNECT_INTERVAL_BASE 5000
#define MQTT_MAX_RETRIES 10

void mqtt_loop() {
  static unsigned long lastTry = 0;
  static int retryCount = 0;
  unsigned long now = millis();

  // Calculate exponential backoff for reconnection
  unsigned long reconnectInterval = MQTT_RECONNECT_INTERVAL_BASE * (1 << min(retryCount, 4)); // Max 5x multiplier

  if (!mqtt.connected()) {
    currentMqttState = MQTT_STATE_DISCONNECTED;
    
    if (now - lastTry > reconnectInterval) {
      lastTry = now;

      if (provisioningDone && !usingProvisionedCerts) {
        if (certificatePem.length() > 0 && privateKey.length() > 0) {
          Serial.println("🔄 Re-configuring with permanent credentials...");
          net.setCertificate(certificatePem.c_str());
          net.setPrivateKey(privateKey.c_str());
          usingProvisionedCerts = true;
          retryCount = 0;
          currentMqttState = MQTT_STATE_CONNECTING;
        } else {
          Serial.println("❌ No valid provisioned credentials available");
          return;
        }
      }

      Serial.print("🔗 MQTT reconnect attempt ");
      Serial.print(retryCount + 1);
      Serial.print("/");
      Serial.print(MQTT_MAX_RETRIES);
      Serial.println("...");

      currentMqttState = MQTT_STATE_CONNECTING;
      if (mqtt.connect(clientId.c_str())) {
        Serial.println("✅ MQTT connected");
        retryCount = 0;
        currentMqttState = MQTT_STATE_CONNECTED;

        if (!usingProvisionedCerts) {
          mqtt.subscribe(CREATE_CERT_SUB);
          mqtt.subscribe(PROVISION_SUB);
          mqtt.subscribe(PROVISION_REJ);
          Serial.println("📡 Subscribed to provisioning topics");
          currentMqttState = MQTT_STATE_CERT_REQUESTING;
        } else {
          Serial.println("🚀 Ready for data publishing");
          currentMqttState = MQTT_STATE_READY;
        }
      } else {
        retryCount++;
        Serial.print("❌ MQTT connection failed, rc=");
        Serial.println(mqtt.state());
        
        if (retryCount >= MQTT_MAX_RETRIES) {
          Serial.println("⚠️  MQTT max retries reached, will retry with exponential backoff");
          // Don't reset retryCount here, let it continue with backoff
        }
      }
    }
    return;
  }

  // MQTT is connected
  mqtt.loop();

  // Handle provisioning workflow
  if (usingProvisionedCerts) {
    currentMqttState = MQTT_STATE_READY;
    return;
  }

  // Certificate request with timeout and retry
  if (!certRequested || (certRequested && now - lastCertRequestTime > CERT_REQUEST_TIMEOUT)) {
    if (certRequestRetries < 3) { // Max 3 certificate requests
      if (mqtt.publish(CREATE_CERT_PUB, "{}")) {
        certRequested = true;
        lastCertRequestTime = now;
        certRequestRetries++;
        Serial.print("📨 Certificate request sent (attempt ");
        Serial.print(certRequestRetries);
        Serial.println("/3)");
      } else {
        Serial.println("❌ Failed to send certificate request");
      }
    } else {
      Serial.println("❌ Certificate request failed after 3 attempts");
      // Could implement fallback or alert here
    }
    return;
  }

  // Provisioning request with timeout and retry
  if (ownershipToken.length() > 0 && (!provisionRequested || (provisionRequested && now - lastProvisionRequestTime > PROVISION_TIMEOUT))) {
    if (provisionRetries < 3) { // Max 3 provisioning attempts
      DynamicJsonDocument doc(2048);
      doc["certificateOwnershipToken"] = ownershipToken;
      
      JsonObject params = doc.createNestedObject("parameters");
      params["SerialNumber"] = clientId;

      char payload[512];
      serializeJson(doc, payload);

      if (mqtt.publish(PROVISION_PUB, payload)) {
        provisionRequested = true;
        lastProvisionRequestTime = now;
        provisionRetries++;
        Serial.print("📦 Provisioning request sent (attempt ");
        Serial.print(provisionRetries);
        Serial.println("/3)");
        currentMqttState = MQTT_STATE_PROVISIONING;
      } else {
        Serial.println("❌ Failed to send provisioning request");
      }
    } else {
      Serial.println("❌ Provisioning failed after 3 attempts");
      // Reset certificate state to try again
      certRequested = false;
      certRequestRetries = 0;
      ownershipToken = "";
      currentMqttState = MQTT_STATE_CONNECTED;
    }
  }
}

// ================= DATA PUBLISH =================
bool mqtt_publish_with_retry(
  float pm1, float pm25, float pm4, float pm10,
  float voc, float nox,
  uint16_t co2,
  float temp, float hum,
  int maxRetries = PUBLISH_RETRY_MAX
) {
  if (!mqtt.connected() || !provisioningDone) {
    Serial.println("[MQTT_PUB] Not publishing: MQTT not connected or provisioning not done.");
    return false;
  }

  StaticJsonDocument<256> doc;
  // 🆕 Unique token per message
  String msgToken = generateMessageToken();
  doc["Device Token"] = msgToken;

  // 🕒 Timestamp
  doc["timestamp"] = time(nullptr);

  // 📊 Sensor Data
  doc["pm1"]  = pm1;
  doc["pm25"] = pm25;
  doc["pm4"]  = pm4;
  doc["pm10"] = pm10;
  doc["voc"]  = voc;
  doc["nox"]  = nox;
  doc["co2"]  = co2;
  doc["temp"] = temp;
  doc["hum"]  = hum;

  char payload[256];
  serializeJson(doc, payload);

   // 🔥 Dynamic topic based on token
  String topic = "aqi/device/" + msgToken;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    if (mqtt.publish(topic.c_str(), payload)) {
      Serial.print("📤 MQTT data published successfully on attempt ");
      Serial.println(attempt);
      return true;
    } else {
      Serial.print("❌ MQTT publish failed on attempt ");
      Serial.print(attempt);
      Serial.print("/");
      Serial.println(maxRetries);
      
      if (attempt < maxRetries) {
        // Wait before retry with exponential backoff
        unsigned long waitTime = 1000 * attempt; // 1s, 2s, 3s...
        Serial.print("⏳ Waiting ");
        Serial.print(waitTime);
        Serial.println("ms before retry...");
        delay(waitTime);
      }
    }
  }

  Serial.println("❌ MQTT publish failed after all retries");
  return false;
}

void mqtt_publish(
  float pm1, float pm25, float pm4, float pm10,
  float voc, float nox,
  uint16_t co2,
  float temp, float hum
) {
  mqtt_publish_with_retry(pm1, pm25, pm4, pm10, voc, nox, co2, temp, hum);
}

// Publishes a reset event with metadata
void mqtt_publish_reset_event(const char* device_id, const char* event, const char* reset_reason, const char* timestamp, const char* firmware_version) {
  if (!mqtt.connected() || !provisioningDone) return;
  StaticJsonDocument<256> doc;
  doc["device_id"] = device_id;
  doc["event"] = event;
  doc["reset_reason"] = reset_reason;
  doc["timestamp"] = timestamp;
  doc["firmware_version"] = firmware_version;
  char payload[256];
  serializeJson(doc, payload);
  String topic = String("aqi/device/") + device_id + "/event";
  mqtt.publish(topic.c_str(), payload);
  Serial.println("📤 MQTT reset event published");
}

// Get current MQTT connection state for debugging
String mqtt_get_state_info() {
  String info = "MQTT State: ";
  
  switch (currentMqttState) {
    case MQTT_STATE_DISCONNECTED: info += "DISCONNECTED"; break;
    case MQTT_STATE_CONNECTING: info += "CONNECTING"; break;
    case MQTT_STATE_CONNECTED: info += "CONNECTED"; break;
    case MQTT_STATE_CERT_REQUESTING: info += "CERT_REQUESTING"; break;
    case MQTT_STATE_PROVISIONING: info += "PROVISIONING"; break;
    case MQTT_STATE_READY: info += "READY"; break;
  }
  
  info += " | Connected: " + String(mqtt.connected() ? "YES" : "NO");
  info += " | Provisioned: " + String(provisioningDone ? "YES" : "NO");
  info += " | Using Certs: " + String(usingProvisionedCerts ? "YES" : "NO");
  info += " | Cert Requested: " + String(certRequested ? "YES" : "NO");
  info += " | Provision Requested: " + String(provisionRequested ? "YES" : "NO");
  
  return info;
}

bool mqtt_is_connected() {
  return mqtt.connected();
}

void mqtt_force_disconnect() {
  if (mqtt.connected()) {
    mqtt.disconnect();
    currentMqttState = MQTT_STATE_DISCONNECTED;
  }
}
