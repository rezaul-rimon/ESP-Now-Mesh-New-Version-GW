#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

#define OLED_MOSI       23
#define OLED_CLK        18
#define OLED_DC         13
#define OLED_CS         5
#define OLED_RST        14

#define DISPLAY_TASK_PRIORITY   1
#define DISPLAY_CMD_QUEUE_SIZE  10
#define DISPLAY_TASK_STACK      4096

enum DisplayCommandType {
    DISPLAY_BOOT,
    DISPLAY_OFFLINE,
    DISPLAY_GSM_INIT,
    DISPLAY_GSM_CONNECTING,
    DISPLAY_GSM_CONNECTED,
    DISPLAY_MQTT_CONNECTING,
    DISPLAY_ONLINE,
    DISPLAY_SENSOR_DATA,
    DISPLAY_RF_CMD,
    DISPLAY_RF_ACK,
    DISPLAY_RF_HB,
    DISPLAY_OTA,
    DISPLAY_ERROR,
    DISPLAY_HOME,
    DISPLAY_UPDATE_HOME,
    DISPLAY_CLEAR
};

struct DisplayData {
    float solarVoltage;
    float batteryVoltage;
    float ph;
    float tds;
    float dissolvedOxygen;   // DO
    float temperature;
    int   signalLevel;       // 0..5
    const char* statusText;
    int   batteryPercent;    // 0..100   ✅ <-- added
};

extern Adafruit_SSD1306 display;
extern DisplayData displayData;
extern TaskHandle_t displayTaskHandle;
extern QueueHandle_t displayCommandQueue;

void Display_setup();
void sendDisplayCommand(DisplayCommandType type);
void displayTask(void* parameter);

// Update functions
void updateSolarVoltage(float value);
void updateBatteryVoltage(float value);
void updateBatteryPercentage(int percent);
void updatePH(float value);
void updateTDS(float value);
void updateDissolvedOxygen(float value);
void updateTemperature(float value);
void updateSignalStrength(int level);
void updateStatus(const char* status);
void updateAllDisplayValues(float solar, float batt, float ph, float tds, float doVal, float temp, int signal, const char* status);