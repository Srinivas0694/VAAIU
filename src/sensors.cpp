#include "sensors.hpp"
#include "config.hpp"
#include <Wire.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BME680.h>

static SensirionI2CSen5x sen5x;
static SensirionI2cScd4x scd4x;
static Adafruit_BME680 bme;

void sensors_init() {
  Wire.begin(I2C_SDA, I2C_SCL);

  // Quick I2C bus scan to show attached devices and help debugging
  Serial.println("I2C bus scan start...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x"); Serial.println(addr, HEX);
    }
  }
  Serial.println("I2C scan complete");

  // SEN5x particulate + VOC/NOx
  Serial.println("Initializing SEN5x...");
  sen5x.begin(Wire);
  sen5x.deviceReset();
  delay(500);
  sen5x.startMeasurement();
  // Allow SEN5x some warm-up time for stable readings
  delay(2000);

  // SCD4x CO2 sensor
  Serial.println("Initializing SCD4x...");
  scd4x.begin(Wire, 0x62);
  scd4x.startPeriodicMeasurement();
  // SCD4x may need a few seconds before returning valid CO2
  delay(5000);

  // BME680 temperature/humidity
  Serial.println("Initializing BME680...");
  if (!bme.begin(0x76)) {
    if (!bme.begin(0x77)) {
      Serial.println("BME680 init failed on both addresses");
    } else {
      Serial.println("BME680 initialized at 0x77");
    }
  } else {
    Serial.println("BME680 initialized at 0x76");
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);

  Serial.println("Sensor init complete");
}

#define PM_MAX 1000.0f
#define VOC_MAX 500.0f
#define NOX_MAX 500.0f
#define CO2_MAX 5000
#define TEMP_MIN -40.0f
#define TEMP_MAX 85.0f
#define HUM_MIN 0.0f
#define HUM_MAX 100.0f

static bool isValidPM(float pm) {
  return pm >= 0.0f && pm <= PM_MAX;
}

static bool isValidVOC(float voc) {
  return voc >= 0.0f && voc <= VOC_MAX;
}

static bool isValidCO2(uint16_t co2) {
  return co2 > 0 && co2 <= CO2_MAX;
}

static bool isValidTemp(float temp) {
  return temp >= TEMP_MIN && temp <= TEMP_MAX;
}

static bool isValidHum(float hum) {
  return hum >= HUM_MIN && hum <= HUM_MAX;
}

void sensors_read(
  float &pm1, float &pm25, float &pm4, float &pm10,
  float &voc, float &nox,
  uint16_t &co2,
  float &temp, float &hum
) {
  float t_bme = 0, h_bme = 0;
  float t_sen5 = 0, h_sen5 = 0;
  float temp_pm1 = 0, temp_pm25 = 0, temp_pm4 = 0, temp_pm10 = 0;
  float temp_voc = 0, temp_nox = 0;
  uint16_t temp_co2 = 0;

  bool sen5x_success = false;
  int sen5x_attempts = 0;
  const int SEN5X_MAX_ATTEMPTS = 3;

  // SEN5x reading with retry logic
  while (!sen5x_success && sen5x_attempts < SEN5X_MAX_ATTEMPTS) {
    sen5x_attempts++;
    Serial.print("SEN5x read attempt ");
    Serial.println(sen5x_attempts);

    uint16_t error = sen5x.readMeasuredValues(temp_pm1, temp_pm25, temp_pm4, temp_pm10, h_sen5, t_sen5, temp_voc, temp_nox);

    if (error) {
      Serial.print("❌ SEN5x I2C error: ");
      Serial.println(error);
      delay(500); // Wait before retry
      continue;
    }

    // Check for all zeros (sensor not ready)
    if (temp_pm1 == 0 && temp_pm25 == 0 && temp_pm4 == 0 && temp_pm10 == 0) {
      Serial.println("⚠️  SEN5x returned zeros, sensor may not be ready");
      if (sen5x_attempts < SEN5X_MAX_ATTEMPTS) {
        delay(1000); // Wait longer for sensor to be ready
        continue;
      }
    }

    // Validate readings
    if (isValidPM(temp_pm1) && isValidPM(temp_pm25) && isValidPM(temp_pm4) && isValidPM(temp_pm10)) {
      pm1 = temp_pm1;
      pm25 = temp_pm25;
      pm4 = temp_pm4;
      pm10 = temp_pm10;
      voc = isValidVOC(temp_voc) ? temp_voc : 0;
      nox = isValidVOC(temp_nox) ? temp_nox : 0;
      sen5x_success = true;
      Serial.print("✅ SEN5x PM2.5: ");
      Serial.println(pm25);
    } else {
      Serial.println("❌ SEN5x: invalid sensor values received");
      delay(500);
    }
  }

  if (!sen5x_success) {
    Serial.println("❌ SEN5x: failed to get valid readings after all attempts");
    // Keep previous values or set to 0
    pm1 = pm25 = pm4 = pm10 = voc = nox = 0;
  }

  // SCD4x reading with retry logic
  bool scd4x_success = false;
  int scd4x_attempts = 0;
  const int SCD4X_MAX_ATTEMPTS = 5;

  while (!scd4x_success && scd4x_attempts < SCD4X_MAX_ATTEMPTS) {
    scd4x_attempts++;
    Serial.print("SCD4x read attempt ");
    Serial.println(scd4x_attempts);

    bool ready = false;
    uint16_t error = scd4x.getDataReadyStatus(ready);

    if (error) {
      Serial.print("❌ SCD4x getDataReadyStatus error: ");
      Serial.println(error);
      delay(500);
      continue;
    }

    if (!ready) {
      Serial.println("⚠️  SCD4x not ready, waiting...");
      delay(1000); // SCD4x measurement interval is ~5 seconds
      continue;
    }

    int rc_scd = scd4x.readMeasurement(temp_co2, t_bme, h_bme);
    if (rc_scd == 0 && isValidCO2(temp_co2)) {
      co2 = temp_co2;
      scd4x_success = true;
      Serial.print("✅ SCD4x CO2: ");
      Serial.println(co2);
    } else if (rc_scd != 0) {
      Serial.print("❌ SCD4x read error: ");
      Serial.println(rc_scd);
      delay(500);
    } else {
      Serial.println("❌ SCD4x: invalid CO2 value");
      delay(500);
    }
  }

  if (!scd4x_success) {
    Serial.println("❌ SCD4x: failed to get valid readings after all attempts");
    co2 = 0;
  }

  // BME680 reading with retry logic
  bool bme_success = false;
  int bme_attempts = 0;
  const int BME_MAX_ATTEMPTS = 3;

  while (!bme_success && bme_attempts < BME_MAX_ATTEMPTS) {
    bme_attempts++;
    Serial.print("BME680 read attempt ");
    Serial.println(bme_attempts);

    if (bme.performReading()) {
      if (isValidTemp(bme.temperature) && isValidHum(bme.humidity)) {
        temp = bme.temperature;
        hum = bme.humidity;
        bme_success = true;
        Serial.print("✅ BME680 T:");
        Serial.print(temp);
        Serial.print("°C H:");
        Serial.println(hum);
      } else {
        Serial.println("❌ BME680: invalid temperature/humidity values");
        delay(500);
      }
    } else {
      Serial.println("❌ BME680: performReading failed");
      delay(500);
    }
  }

  if (!bme_success) {
    Serial.println("❌ BME680: failed to get valid readings after all attempts");
    temp = 0;
    hum = 0;
  }
}
