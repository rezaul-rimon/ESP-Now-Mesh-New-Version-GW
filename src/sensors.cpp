#include "sensors.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <algorithm>   // for std::sort

static Adafruit_ADS1115 ads;
SensorData sensorData;    // <-- no 'static', no extra qualifiers

// ---------------------------------------------------------
// Helper: sort, trim extremes, average (returns float)
// ---------------------------------------------------------
static float averageWithTrimming(int* samples, int count, int trimCount) {
    if (count <= 2 * trimCount) {
        long sum = 0;
        for (int i = 0; i < count; i++) sum += samples[i];
        return (float)sum / count;
    }

    std::sort(samples, samples + count);

    long sum = 0;
    int validCount = count - 2 * trimCount;
    for (int i = trimCount; i < count - trimCount; i++) {
        sum += samples[i];
    }
    return (float)sum / validCount;
}

// ---------------------------------------------------------
// Read ADS1115 channel with trimmed average (returns raw average as float)
// ---------------------------------------------------------
static float readAdsChannelWithTrim(uint8_t channel, int samples, int trimCount) {
    int rawValues[samples];
    for (int i = 0; i < samples; i++) {
        rawValues[i] = ads.readADC_SingleEnded(channel);
        delayMicroseconds(500);
    }
    return averageWithTrimming(rawValues, samples, trimCount);
}

// ---------------------------------------------------------
// Read ESP32 ADC pin with trimmed average
// ---------------------------------------------------------
static float readEsp32AdcWithTrim(int pin, int samples, int trimCount) {
    int rawValues[samples];
    for (int i = 0; i < samples; i++) {
        rawValues[i] = analogRead(pin);
        delayMicroseconds(500);
    }
    return averageWithTrimming(rawValues, samples, trimCount);
}

// ---------------------------------------------------------
// Map voltage (0..3.3V) to 0..1024
// ---------------------------------------------------------
static float mapVoltageTo1024(float voltage) {
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > 3.3f) voltage = 3.3f;
    return (voltage / 3.3f) * 1024.0f;
}

// ---------------------------------------------------------
// Initialization
// ---------------------------------------------------------
void initSensors() {
    analogSetPinAttenuation(PIN_SOLAR_ADC, ADC_11db);

    Wire.begin();

    if (!ads.begin()) {
        Serial.println("ADS1115 not found. Check wiring!");
        while (1) {
            delay(10);
        }
    }

    ads.setGain(GAIN_ONE);

    Serial.println("Sensors initialized.");
}

// ---------------------------------------------------------
// Read all sensors and update global sensorData
// ---------------------------------------------------------
void readAllSensors() {
    // 1) Solar (ESP32 internal ADC)
    float solarRawAvg = readEsp32AdcWithTrim(PIN_SOLAR_ADC, SENSOR_SAMPLES, SENSOR_TRIM_COUNT);
    sensorData.solarRaw = (int)solarRawAvg;                     // store as int
    sensorData.solarPinV = solarRawAvg * (3.3f / 4095.0f);
    sensorData.solarVoltage = sensorData.solarPinV * DIVIDER_RATIO;
    sensorData.solarMapped = mapVoltageTo1024(sensorData.solarPinV);

    // 2) ADS1115 channels
    float rawTDS  = readAdsChannelWithTrim(0, SENSOR_SAMPLES, SENSOR_TRIM_COUNT);
    float rawPH   = readAdsChannelWithTrim(1, SENSOR_SAMPLES, SENSOR_TRIM_COUNT);
    float rawNTC  = readAdsChannelWithTrim(2, SENSOR_SAMPLES, SENSOR_TRIM_COUNT);
    float rawBatt = readAdsChannelWithTrim(3, SENSOR_SAMPLES, SENSOR_TRIM_COUNT);

    // Store raw values as int
    sensorData.tdsRaw  = (int)rawTDS;
    sensorData.phRaw   = (int)rawPH;
    sensorData.ntcRaw  = (int)rawNTC;
    sensorData.battRaw = (int)rawBatt;

    // Convert to voltage
    sensorData.tdsV = ads.computeVolts((int16_t)rawTDS);
    sensorData.phV  = ads.computeVolts((int16_t)rawPH);
    sensorData.ntcV = ads.computeVolts((int16_t)rawNTC);
    sensorData.battPinV = ads.computeVolts((int16_t)rawBatt);

    // Map to 0-1024
    sensorData.tdsMapped = mapVoltageTo1024(sensorData.tdsV);
    sensorData.phMapped  = mapVoltageTo1024(sensorData.phV);
    sensorData.ntcMapped = mapVoltageTo1024(sensorData.ntcV);
    sensorData.battMapped = mapVoltageTo1024(sensorData.battPinV);

    // Actual battery voltage
    sensorData.batteryVoltage = sensorData.battPinV * DIVIDER_RATIO;
}