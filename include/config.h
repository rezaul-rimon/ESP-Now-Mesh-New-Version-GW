#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_task_wdt.h>

// ==================== Configuration ====================
#define OTA 1  // Set to 1 for OTA mode, 0 for normal mode
#define HW_VERSION "3.1"
#define FW_VERSION "V3.262.3"
#define OTA_DATE "260701"
//-------------------------------------------------------//

#define WORK_PACKAGE "1293"
#define DEVICE_TYPE "03"
#define DEVICE_CODE_UPLOAD_DATE "260813"
#define DEVICE_SERIAL_ID "0001"

#define UNIQUE_DEVICE_ID WORK_PACKAGE DEVICE_TYPE DEVICE_CODE_UPLOAD_DATE DEVICE_SERIAL_ID
//=========================================================================================//

// Global variable declarations (extern)
extern bool deviceArmed;

// Duplicate from gsm.h for global access
extern QueueHandle_t mqttPublishQueue;

extern String DEVICE_ID;
extern String MAC_FALLBACK_ID;

// Serial and SIM-A7670 pin config
#define SerialMon Serial
#define SerialAT Serial1
#define MODEM_TX 17
#define MODEM_RX 16
#define MODEM_PWR 15
#define ACLINE_PIN 34

#define ON HIGH
#define OFF LOW

// Timing Configuration (const variables are okay in header)
const unsigned long HEARTBEAT_INTERVAL = 2 * 60 * 1000UL;  // 5 minute
extern unsigned long lastHeartbeatTime;

const unsigned long DATA_INTERVAL = 5 * 60 * 1000UL;  // 5 minute
extern unsigned long lastDataTime;

const unsigned long MAIN_TASK_PRIORITY = 2;
const unsigned long GSM_ERROR_RETRY_DELAY = 10000UL;

extern unsigned long lastGsmErrorTime;
extern unsigned long buttonHoldStart;
extern bool buttonHolding;

// System State
extern volatile bool mqttConnected;
extern volatile bool gprsConnected;
extern volatile bool deviceOnline;
extern volatile bool otaInProgress;
extern volatile bool tasksSuspended;
extern bool modemWasInitialized;

// ==================== Global Variables ====================
extern const char* MQTT_PUB;
extern const char* MQTT_SUB;
extern const char* MQTT_ACK;

extern const char* MQTT_NODE_HB;
extern const char* MQTT_NODE_DATA;
extern const char* MQTT_NODE_ACK;

struct MQTTMessage {
    char topic[64];
    char payload[128];
};

// Helper function prototype
String getMACDeviceID();

#define DEBUG_MODE true
#define DEBUG_PRINT(x)  if (DEBUG_MODE) { Serial.print(x); }
#define DEBUG_PRINTF(x) if (DEBUG_MODE) { Serial.printf(x); }
#define DEBUG_PRINTLN(x) if (DEBUG_MODE) { Serial.println(x); }