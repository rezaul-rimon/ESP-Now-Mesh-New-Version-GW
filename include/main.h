#pragma once
#include<Arduino.h>
#include "mesh_gw.h"
#include "led.h"
#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include<config.h>


//Function Prototypes
void ledTask(void *param);
void mainTask(void *param);

Preferences preferences;

// ================= FreeRTOS =================
TaskHandle_t mainTaskHandle;

#define DEBUG_MODE true
#define DEBUG_PRINT(x)  if (DEBUG_MODE) { Serial.print(x); }
#define DEBUG_PRINTF(x) if (DEBUG_MODE) { Serial.printf(x); }
#define DEBUG_PRINTLN(x) if (DEBUG_MODE) { Serial.println(x); }