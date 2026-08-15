#include "mesh_gw.h"
#include "config.h"   // for MQTTMessage, DEVICE_ID, DEBUG_PRINT, etc.

// ================= DEVICE =================
const char* Local_ID = "gw0";
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ================= DEDUP =================
std::deque<MsgKey> recentMsgKeys;
const size_t maxRecentIDs = 200;

bool useEncryption = false;
const char enckey[] = "dmabd987";
const char encCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+=[]{}|:;<>?,./~";

// ================= QUEUE AND TASK HANDLES =================
TaskHandle_t EspNowOnReceiveTaskHandle = NULL;
QueueHandle_t espNowRxQueue = NULL;

// ================= Helper: Type Name =================
const char* getTypeName(message_type_t type) {
    switch(type) {
        case MSG_CMD: return "Command";
        case MSG_ACK: return "Acknowledgement";
        case MSG_HB:  return "Heartbeat";
        case MSG_SD:  return "Sensor Data";
        default:      return "Unknown";
    }
}

// ================= Unique Message ID =================
String generateMessageID() {
    uint16_t randNum = esp_random() & 0xFFFF;
    char id[5];
    sprintf(id, "%04X", randNum);
    return String(id);
}

// ================= Duplicate Detection =================
bool isDuplicate(const char* sender, message_type_t type, const char* msg_id) {
    MsgKey key;

    strncpy(key.sender, sender, sizeof(key.sender));
    key.sender[sizeof(key.sender) - 1] = '\0';

    key.type = type;

    strncpy(key.msg_id, msg_id, sizeof(key.msg_id));
    key.msg_id[sizeof(key.msg_id) - 1] = '\0';

    // search in deque
    for (auto &k : recentMsgKeys) {
        if (strcmp(k.sender, key.sender) == 0 &&
            k.type == key.type &&
            strcmp(k.msg_id, key.msg_id) == 0) {
            DEBUG_PRINTLN("Duplicate: " + String(k.sender) + ":" +
                          String(k.type) + ":" + String(k.msg_id));
            return true;
        }
    }

    recentMsgKeys.push_back(key);
    if (recentMsgKeys.size() > maxRecentIDs) {
        recentMsgKeys.pop_front();
    }

    return false;
}

// ================= Encryption =================
void encryptSimple(const char* msg, char* out, const char* enckey) {
    int msgLen = strlen(msg);
    int keyLen = strlen(enckey);
    int charsetLen = strlen(encCharset);

    for (int i = 0; i < msgLen; i++) {
        char c = msg[i];
        int index = -1;

        for (int j = 0; j < charsetLen; j++) {
            if (encCharset[j] == c) {
                index = j;
                break;
            }
        }

        if (index == -1) {
            out[i] = c;
            continue;
        }

        int shift = enckey[i % keyLen] + i;
        int newIndex = (index + shift) % charsetLen;
        out[i] = encCharset[newIndex];
    }

    out[msgLen] = '\0';
}

// ================= Decryption =================
void decryptSimple(const char* msg, char* out, const char* enckey) {
    int msgLen = strlen(msg);
    int keyLen = strlen(enckey);
    int charsetLen = strlen(encCharset);

    for (int i = 0; i < msgLen; i++) {
        char c = msg[i];
        int index = -1;

        for (int j = 0; j < charsetLen; j++) {
            if (encCharset[j] == c) {
                index = j;
                break;
            }
        }

        if (index == -1) {
            out[i] = c;
            continue;
        }

        int shift = enckey[i % keyLen] + i;
        int newIndex = index - shift;

        while (newIndex < 0) newIndex += charsetLen;

        out[i] = encCharset[newIndex];
    }

    out[msgLen] = '\0';
}

// ================= ESP-NOW Receive Callback =================
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (len <= 0 || len > ESPNOW_MAX_MSG_LEN) {
        DEBUG_PRINTLN("⚠️ Packet length Exceeded: " + String(len));
        return;
    }

    EspNowRxMessage msg;
    msg.len = len;
    memcpy(msg.data, data, len);
    msg.data[len] = '\0';

    BaseType_t ok = xQueueSendFromISR(espNowRxQueue, &msg, NULL);

    if (ok != pdTRUE) {
        DEBUG_PRINTLN("⚠️ Queue Overflow: Failed to enqueue received message");
    }
}

// ================= Setup =================
void mesh_gw_setup() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed");
        return;
    }

    espNowRxQueue = xQueueCreate(MAX_FWDS, sizeof(EspNowRxMessage));
    if (espNowRxQueue == NULL) {
        DEBUG_PRINTLN("RX Queue Create Failed");
    }

    xTaskCreatePinnedToCore(
        EspNowOnReceiveTask,
        "EspNowRx",
        ONRECEIVE_TASK_STACK,
        NULL,
        ONRECEIVE_TASK_PRIORITY,
        &EspNowOnReceiveTaskHandle,
        1
    );

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(onReceive);
}

// ================= ESP-NOW Receive Task =================
void EspNowOnReceiveTask(void *pvParameters) {
    EspNowRxMessage rxMsg;

    while (true) {
        if (xQueueReceive(espNowRxQueue, &rxMsg, portMAX_DELAY) == pdTRUE) {
            char msg[ESPNOW_MAX_MSG_LEN + 1];

            int len = rxMsg.len;
            if (len > ESPNOW_MAX_MSG_LEN) len = ESPNOW_MAX_MSG_LEN;

            memcpy(msg, rxMsg.data, len);
            msg[len] = '\0';

            int commas = 0;
            for (int i = 0; msg[i] != '\0'; i++) {
                if (msg[i] == ',') commas++;
            }

            if (commas != 6) {
                DEBUG_PRINTLN("❌ Invalid packet");
                DEBUG_PRINTLN(rxMsg.data);
                continue;
            }

            DEBUG_PRINTLN();
            DEBUG_PRINTLN("============================================");
            DEBUG_PRINT("📥 Received from Queue:");
            DEBUG_PRINTLN(rxMsg.data);
            DEBUG_PRINTLN("============================================");
            DEBUG_PRINTLN();

            char *saveptr;
            char *sender = strtok_r(msg, ",", &saveptr);
            char *receiver = strtok_r(NULL, ",", &saveptr);
            char *command = strtok_r(NULL, ",", &saveptr);
            char *typeStr = strtok_r(NULL, ",", &saveptr);
            char *msg_id = strtok_r(NULL, ",", &saveptr);
            char *last_hop = strtok_r(NULL, ",", &saveptr);
            char *hopStr = strtok_r(NULL, ",", &saveptr);

            if (sender == NULL || receiver == NULL || command == NULL ||
                typeStr == NULL || msg_id == NULL || last_hop == NULL || hopStr == NULL) {
                DEBUG_PRINTLN("❌ Invalid packet");
                DEBUG_PRINTLN(rxMsg.data);
                continue;
            }

            sender[strcspn(sender, "\r\n\t ")] = 0;
            receiver[strcspn(receiver, "\r\n\t ")] = 0;
            command[strcspn(command, "\r\n\t ")] = 0;
            msg_id[strcspn(msg_id, "\r\n\t ")] = 0;
            last_hop[strcspn(last_hop, "\r\n\t ")] = 0;
            typeStr[strcspn(typeStr, "\r\n\t ")] = 0;
            hopStr[strcspn(hopStr, "\r\n\t ")] = 0;

            message_type_t type = (message_type_t)atoi(typeStr);
            int hop_count = atoi(hopStr);

            if (type == MSG_CMD) {
                Serial.print("⚙️ Processing command: ");
                Serial.println(command);
                Serial.println("GW Should not execute commands");
                sendLedCommand(LED_RF_CMD);
                continue;
            }

            if (isDuplicate(sender, type, msg_id)) {
                Serial.println("⚠️ Duplicate ignored");
                continue;
            }

            if (type == MSG_HB) {
                MQTTMessage hbMsg;
                strcpy(hbMsg.topic, MQTT_NODE_HB);
                snprintf(hbMsg.payload, sizeof(hbMsg.payload), "%s,%s,%s",
                         DEVICE_ID.c_str(), sender, command);
                if (xQueueSend(mqttPublishQueue, &hbMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
                    SerialMon.println("MainTask: Node Heartbeat queued");
                }
                sendLedCommand(LED_RF_HB);
                continue;
            }

            if (type == MSG_SD) {
                Serial.print("🌡️ Sensor Data from ");
                Serial.print(sender);
                Serial.print(": ");
                Serial.println(command);
                MQTTMessage sdMsg;
                strcpy(sdMsg.topic, MQTT_NODE_DATA);
                snprintf(sdMsg.payload, sizeof(sdMsg.payload), "%s,%s,%s",
                         DEVICE_ID.c_str(), sender, command);
                if (xQueueSend(mqttPublishQueue, &sdMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
                    SerialMon.println("MainTask: Node Sensor Data queued");
                }
                sendLedCommand(LED_RF_HB);
                continue;
            }

            if (type == MSG_ACK) {
                Serial.print("✅ ACK from ");
                Serial.print(sender);
                Serial.print(": ");
                Serial.println(command);
                MQTTMessage ackMsg;
                strcpy(ackMsg.topic, MQTT_NODE_ACK);
                snprintf(ackMsg.payload, sizeof(ackMsg.payload), "%s,%s,%s",
                         DEVICE_ID.c_str(), sender, command);
                if (xQueueSend(mqttPublishQueue, &ackMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
                    SerialMon.println("MainTask: Node ACK queued");
                }
                sendLedCommand(LED_RF_ACK);
                continue;
            }

            Serial.printf("✅ %s | From:%s -> To:%s | CMD:%s | ID:%s | Hop:%d | Last:%s\n",
                          getTypeName(type), sender, receiver, command, msg_id, hop_count, last_hop);
        }
    }
}