#pragma once

#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_task_wdt.h>

// ==================== Configuration ====================
#define OTA 0  // Set to 1 for OTA mode, 0 for normal mode
#define HW_VERSION "3.0"
#define FW_VERSION "V1.103"
#define OTA_DATE "260101"

#define USE_LDR_SENSOR
// #define USE_GY30

// Sensor configuration
#ifdef USE_LDR_SENSOR
    #define LDR_PIN 39 // Pin for LDR sensor
#endif

#define WORK_PACKAGE "1225"
#define DEVICE_TYPE "03"
#define DEVICE_CODE_UPLOAD_DATE "260502"
#define DEVICE_SERIAL_ID "0001"

#define UNIQUE_DEVICE_ID WORK_PACKAGE DEVICE_TYPE DEVICE_CODE_UPLOAD_DATE DEVICE_SERIAL_ID

bool deviceArmed = true;  // Default to armed, can be changed via MQTT command

//Duplicalte from gsm.h for global access
QueueHandle_t mqttPublishQueue = NULL;

String DEVICE_ID = "";
String MAC_FALLBACK_ID = "";


// Serial and SIM-A7670 pin config
#define SerialMon Serial
#define SerialAT Serial1
#define MODEM_TX 17
#define MODEM_RX 16
#define MODEM_PWR 15
#define ACLINE_PIN 34

#define ON HIGH
#define OFF LOW

// Timing Configuration
const unsigned long DATA_INTERVAL = 5 * 60000UL;  // 1 minute
unsigned long lastDataTime = 0;
const unsigned long MAIN_TASK_PRIORITY = 2;
const unsigned long HEARTBEAT_INTERVAL = 15 * 60000UL;
const unsigned long GSM_ERROR_RETRY_DELAY = 10000UL;
unsigned long lastHeartbeat = 0;
unsigned long lastGsmErrorTime = 0;
unsigned long buttonHoldStart = 0;
bool buttonHolding = false;

// System State
volatile bool mqttConnected = false;
volatile bool gprsConnected = false;
volatile bool deviceOnline = false;
volatile bool otaInProgress = false;
volatile bool tasksSuspended = false;
bool modemWasInitialized = false;


// ==================== Global Variables ====================
const char* MQTT_LP_HB = "DMA-LP/GW/HB";
const char* MQTT_LP_PUB = "DMA-LP/GW/PUB";
const char* MQTT_LP_SUB = "DMA-LP/GW/SUB/";
const char* MQTT_LP_ACK = "DMA-LP/GW/ACK";

const char* MQTT_LP_NODE_HB = "DMA-LP/NODE/HB";
const char* MQTT_LP_NODE_ACK = "DMA-LP/NODE/ACK";
struct MQTTMessage {
    char topic[50];
    char payload[100];
};

// Helper functions
String getMACDeviceID();

String getMACDeviceID() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[13];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}
