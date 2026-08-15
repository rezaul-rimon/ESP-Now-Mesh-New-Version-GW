#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <deque>
#include <algorithm>
#include "led.h"

// ================= DEVICE =================
extern const char* Local_ID;
extern uint8_t broadcastAddress[6];

// ========= ESP-NOW on Receive Queue Size =========
#define ESPNOW_MAX_MSG_LEN 80
#define MAX_FWDS 1000

// ================= DEDUP =================
struct MsgKey {
    char sender[10];
    uint8_t type;
    char msg_id[6];
};

extern std::deque<MsgKey> recentMsgKeys;
extern const size_t maxRecentIDs;

extern bool useEncryption;
extern const char enckey[];
extern const char encCharset[];

// ================= ENUM =================
typedef enum {
    MSG_CMD = 1,
    MSG_ACK,
    MSG_HB,
    MSG_SD
} message_type_t;

struct Message {
    char sender_id[10];
    char receiver_id[10];
    char command[40];
    uint8_t type;
    char msg_id[6];
    char last_hop[10];
    uint8_t hop_count;
};

struct EspNowRxMessage {
    int len;
    char data[ESPNOW_MAX_MSG_LEN + 1];
};

// ================= QUEUE AND TASK HANDLES =================
extern TaskHandle_t EspNowOnReceiveTaskHandle;
extern QueueHandle_t espNowRxQueue;

#define ONRECEIVE_TASK_STACK 16 * 1024
#define ONRECEIVE_TASK_PRIORITY 2

// ================= Function Prototypes =================
const char* getTypeName(message_type_t type);
String generateMessageID();
bool isDuplicate(const char* sender, message_type_t type, const char* msg_id);
void encryptSimple(const char* msg, char* out, const char* enckey);
void decryptSimple(const char* msg, char* out, const char* enckey);
void onReceive(const uint8_t *mac, const uint8_t *data, int len);
void mesh_gw_setup();
void EspNowOnReceiveTask(void *pvParameters);