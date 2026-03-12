# Security Hardening Notes

## ⚠️ CRITICAL: Hardcoded AWS Credentials

### Issue
The claim certificate and private key are hardcoded in `src/mqtt_client.cpp:71-122`. Anyone with access to the source code or compiled binary can extract these credentials and impersonate devices.

### Current Risk
- **Severity**: CRITICAL
- **Affected Files**: `src/mqtt_client.cpp` (lines 71-122)
- **Issue**: Claim certificate and private key exposed
- **Impact**: Unauthorized device provisioning, data tampering, account compromise

### Migration Path

#### Option 1: Environment Variables (Recommended for Development)
```bash
# Create .env file (DO NOT COMMIT)
export CLAIM_CERT="-----BEGIN CERTIFICATE-----..."
export CLAIM_KEY="-----BEGIN RSA PRIVATE KEY-----..."

# Build with secrets
pio run -e esp32dev --environment=esp32dev
```

#### Option 2: Secure NVS Storage (Recommended for Production)
Store credentials in encrypted Non-Volatile Storage:

```cpp
#include <Preferences.h>

Preferences nvs;
void loadCredsFromNVS() {
  nvs.begin("credentials", true);
  String cert = nvs.getString("claim_cert", "");
  String key = nvs.getString("claim_key", "");
  nvs.end();
  
  if (cert.length() > 0 && key.length() > 0) {
    net.setCertificate(cert.c_str());
    net.setPrivateKey(key.c_str());
  }
}
```

#### Option 3: AWS IoT Secure Element (Best)
Use hardware security module if your ESP32 supports it.

### Implementation Checklist
- [ ] Remove hardcoded credentials from source
- [ ] Implement NVS encryption
- [ ] Add credential provisioning mechanism
- [ ] Add rotation policy for claim certificate
- [ ] Enable firmware encryption
- [ ] Document credential injection in CI/CD

## Additional Security Improvements (Completed)

✅ **Input Validation**
- WiFi SSID/password length validation (32/63 chars max per WiFi standard)
- Sensor data range validation
- JSON payload size limits

✅ **Watchdog Timer**
- 30-second watchdog enabled to prevent infinite hangs
- Periodic reset in main loop and critical operations

✅ **Timeout Mechanisms**
- 15-second time sync timeout (prevents blocking forever)
- MQTT reconnect retry limits with backoff

✅ **Error Handling**
- All initialization steps now logged
- Sensor read failures with validation
- MQTT connection failures tracked

✅ **Resource Management**
- Proper BLE cleanup and deinit
- Certificate reload only on valid data
- Memory-efficient JSON payload handling

## Remaining Recommendations

### For Production Deployment

1. **Remove Debug Logging**
   - Replace `Serial.println()` calls with proper logging framework
   - Add log levels (ERROR, WARN, INFO, DEBUG)
   - Don't expose device internals in logs

2. **Firmware Signing**
   - Sign all firmware updates
   - Verify signature before applying

3. **OTA Updates**
   - Implement secure OTA mechanism
   - Add version checking
   - Rollback support

4. **Certificate Pinning**
   - Pin AWS IoT endpoint certificate
   - Prevent MITM attacks

5. **Rate Limiting**
   - MQTT publish rate limits
   - BLE command rate limiting

6. **Device Fingerprinting**
   - Don't rely on MAC address alone
   - Add device-specific identifiers

### Code Review Checklist
- [ ] No hardcoded secrets
- [ ] Input validation on all external data
- [ ] Timeout on all blocking operations
- [ ] Error handling on all I/O operations
- [ ] Resource cleanup on exit/error
- [ ] No buffer overflows
- [ ] Memory bounds checking
- [ ] Secure defaults

## References
- [AWS IoT Device Security Best Practices](https://docs.aws.amazon.com/iot/latest/developerguide/security-best-practices.html)
- [ESP32 Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/index.html)
- [OWASP IoT Security Top 10](https://owasp.org/www-project-iot-security/)
