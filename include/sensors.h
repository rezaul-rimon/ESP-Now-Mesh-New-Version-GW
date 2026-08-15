#pragma once

#include <Arduino.h>

#define PIN_SOLAR_ADC       35
#define PIN_TDS_RELAY       25
#define DIVIDER_RATIO       ((22.0 + 4.7) / 4.7)

#define SENSOR_SAMPLES      30
#define SENSOR_TRIM_COUNT   5

struct SensorData {
    // Raw values are now integers
    int solarRaw;
    float solarPinV;
    float solarVoltage;
    float solarMapped;

    int tdsRaw;
    float tdsV;
    float tdsMapped;

    int phRaw;
    float phV;
    float phMapped;

    int ntcRaw;
    float ntcV;
    float ntcMapped;

    int battRaw;
    float battPinV;
    float batteryVoltage;
    float battMapped;
};

extern SensorData sensorData;

void initSensors();
void readAllSensors();