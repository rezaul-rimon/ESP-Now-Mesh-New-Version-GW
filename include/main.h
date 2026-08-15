#pragma once

#include <Arduino.h>
#include "config.h"
#include "mesh_gw.h"
#include "led.h"
#include "gsm.h"
#include "display.h"
#include "sensors.h"
#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

// ==================== Main Task Configuration ====================
#define MAIN_TASK_PRIORITY 2
#define MAIN_TASK_STACK 4096
#define MQTT_PUB_QUEUE_SIZE 200

// ==================== Global Variables ====================
extern SemaphoreHandle_t sensorMutex;
extern TaskHandle_t mainTaskHandle;

// ==================== Function Prototypes ====================
void setup();
void loop();
void mainTask(void* parameter);

// MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length);
void publishHeartbeat();
void publisMoreFishHeartbeat();
void publishSensorsData();

// OTA
void suspendAllTasks();
void resumeAllTasks();
bool checkForNewFirmware();
bool performOTAUpdate();
bool parseURL(String url, String &host, String &path, int &port);