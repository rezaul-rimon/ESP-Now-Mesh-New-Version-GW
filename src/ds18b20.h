#pragma once
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 32      // DS18B20 Data Pin

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Maximum expected sensors (safe upper limit)
#define MAX_SENSORS 5

DeviceAddress sensorAddress[MAX_SENSORS];   // Store all sensor addresses
int sensorCount = 0;


// Convert address to string format (HEX 16 chars)
String addressToString(const DeviceAddress deviceAddress) {
    String id = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (deviceAddress[i] < 16) id += "0";
        id += String(deviceAddress[i], HEX);
    }
    id.toUpperCase();
    return id;
}

void ds18b20_setup(){
    sensors.begin();
    Serial.println("Searching for DS18B20 sensors...");
    sensorCount = sensors.getDeviceCount();

    Serial.print("Found ");
    Serial.print(sensorCount);
    Serial.println(" sensor(s).");

    if (sensorCount == 0) {
    Serial.println("No sensors detected!");
    return;
    }

    // Store addresses at startup
    for (int i = 0; i < sensorCount; i++) {
    if (sensors.getAddress(sensorAddress[i], i)) {
        Serial.print("Sensor ");
        Serial.print(i);
        Serial.print(" Address: ");
        Serial.println(addressToString(sensorAddress[i]));
    } else {
        Serial.print("Could not read address for sensor ");
        Serial.println(i);
    }
    }
    Serial.println("----------------------------");
    Serial.println();
}
