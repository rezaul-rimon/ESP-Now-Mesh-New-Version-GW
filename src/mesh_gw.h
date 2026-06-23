#pragma once
#include <WiFi.h>
#include <esp_now.h>
#include <deque>
#include <algorithm>
#include<led.h>

// ================= DEVICE =================
const char* Local_ID = "gw0";
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ================= DEDUP =================
std::deque<String> recentMsgKeys;
const size_t maxRecentIDs = 200; // Can be made configurable via Preferences

// ================= ENCRYPTION =================
bool useEncryption = false;
String enckey = "dmabd987";
String encCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+=[]{}|:;<>?,./~";


// ================= ENUM =================
typedef enum {
    MSG_CMD = 1,
    MSG_ACK,
    MSG_HB,
    MSG_SD
} message_type_t;


// ================= MESSAGE =================
struct Message {
    String sender_id;
    String receiver_id;
    String command;
    message_type_t type;
    String msg_id;
    String last_hop;
    uint8_t hop_count;
};

//==================Function Prototypes==================
const char* getTypeName(message_type_t type);
String generateMessageID();
bool isDuplicate(const String& sender, const String& msg_id);
String encryptSimple(String msg, String enckey);
String decryptSimple(String msg, String enckey);
void onReceive(const uint8_t *mac, const uint8_t *incomingData, int len);
void mesh_gw_setup();
//============================================================================//

// Helper for Serial Monitor
const char* getTypeName(message_type_t type) {
    switch(type) {
        case MSG_CMD: return "Command";
        case MSG_ACK: return "Acknowledgement";
        case MSG_HB:  return "Heartbeat";
        case MSG_SD: return "Sensor Data";
        default:      return "Unknown";
    }
}

// ================= MESSAGE ID =================
String generateMessageID() {
    uint16_t randNum = esp_random() & 0xFFFF;
    char id[5];
    sprintf(id, "%04X", randNum);
    return String(id);
}

// ================= DEDUP =================
bool isDuplicate(const String& sender, const String& msg_id) {
    String key = sender + ":" + msg_id;

    if (std::find(recentMsgKeys.begin(), recentMsgKeys.end(), key) != recentMsgKeys.end()) {
        return true;
    }

    recentMsgKeys.push_back(key);
    if (recentMsgKeys.size() > maxRecentIDs) {
        recentMsgKeys.pop_front();
    }

    return false;
}

// ================= ENCRYPTION =================
String encryptSimple(String msg, String enckey)
{
    String out = "";

    for (int i = 0; i < msg.length(); i++)
    {
        char c = msg[i];
        int index = encCharset.indexOf(c);

        if (index == -1) {
            out += c; // keep delimiters like , / - & %
            continue;
        }

        int shift = enckey[i % enckey.length()] + i;
        int newIndex = (index + shift) % encCharset.length();
        out += encCharset[newIndex];
    }

    return out;
}

//================= DECRYPTION =================
String decryptSimple(String msg, String enckey)
{
    String out = "";

    for (int i = 0; i < msg.length(); i++)
    {
        char c = msg[i];
        int index = encCharset.indexOf(c);

        if (index == -1) {
            out += c;
            continue;
        }

        int shift = enckey[i % enckey.length()] + i;
        int newIndex = index - shift;

        while (newIndex < 0)
            newIndex += encCharset.length();

        out += encCharset[newIndex];
    }

    return out;
}

// ================= RECEIVE CALLBACK =================
void onReceive(const uint8_t *mac, const uint8_t *incomingData, int len) {
    String rawMsg((char*)incomingData, len);
    // Serial.println("\n📥 Received: " + rawMsg);

    // Expected:
    // sender,receiver,command,type,msg_id,last_hop,hop_count

    String msg;
    if(useEncryption) {
        msg = decryptSimple(rawMsg, enckey);
    } else {
        msg = rawMsg;
    }

    int commas[6];
    int idx = -1;

    for (int i = 0; i < 6; i++) {
        idx = msg.indexOf(',', idx + 1);
        if (idx < 0) {
            // Serial.println("❌ Invalid format");
            return;
        }
        commas[i] = idx;
    }
    Serial.println("📥 Message: " + msg);

    String sender   = msg.substring(0, commas[0]);
    String receiver = msg.substring(commas[0] + 1, commas[1]);
    String command  = msg.substring(commas[1] + 1, commas[2]);
    message_type_t type = (message_type_t) msg.substring(commas[2] + 1, commas[3]).toInt();
    String msg_id   = msg.substring(commas[3] + 1, commas[4]);
    String last_hop = msg.substring(commas[4] + 1, commas[5]);
    uint8_t hop_count = msg.substring(commas[5] + 1).toInt();

    Serial.println("Raw Type: " + String(type));
    Serial.println("Type: " + String(getTypeName(type)));

    if(type == MSG_CMD) {
        // Process command
        Serial.println("⚙️ Processing command: " + command);
        Serial.println("GW Should not executr commands");
        sendLedCommand(LED_RF_CMD);
        return;
    }

    // Dedup
    if (isDuplicate(sender, msg_id)) {
        Serial.println("⚠️ Duplicate ignored");
        return;
    }
    
    if(type == MSG_HB) {
        Serial.println("💓 Heartbeat from " + sender);
        MQTTMessage hbMsg;
        strcpy(hbMsg.topic, MQTT_LP_NODE_HB);
        snprintf(hbMsg.payload, sizeof(hbMsg.payload), "%s,%s,%s", DEVICE_ID.c_str(), sender.c_str(), command.c_str());
        if (xQueueSend(mqttPublishQueue, &hbMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            SerialMon.println("MainTask: Node Heartbeat queued");
        }
        sendLedCommand(LED_RF_HB);
        return;
    }

    if(type == MSG_SD) {
        Serial.println("🌡️ Sensor Data from " + sender + ": " + command);
        MQTTMessage sdMsg;
        strcpy(sdMsg.topic, MQTT_LP_NODE_SD);
        snprintf(sdMsg.payload, sizeof(sdMsg.payload), "%s,%s,%s", DEVICE_ID.c_str(), sender.c_str(), command.c_str());
        if (xQueueSend(mqttPublishQueue, &sdMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            SerialMon.println("MainTask: Node Sensor Data queued");
        }
        sendLedCommand(LED_RF_HB);
        return;
    }

    if(type == MSG_ACK) {
        Serial.println("✅ ACK from " + sender + ": " + command);
        MQTTMessage ackMsg;
        strcpy(ackMsg.topic, MQTT_LP_NODE_ACK);
        snprintf(ackMsg.payload, sizeof(ackMsg.payload), "%s,%s,%s", DEVICE_ID.c_str(), sender.c_str(), command.c_str());
        if (xQueueSend(mqttPublishQueue, &ackMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
            SerialMon.println("MainTask: Node ACK queued");
        }
        sendLedCommand(LED_RF_ACK);
        return;
    }

    Serial.printf("✅ %s | From:%s → To:%s | CMD:%s | ID:%s | Hop:%d | Last:%s\n",
                    getTypeName(type),
                    sender.c_str(),
                    receiver.c_str(),
                    command.c_str(),
                    msg_id.c_str(),
                    hop_count,
                    last_hop.c_str());
}

// ================= SETUP =================
void mesh_gw_setup(){
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW Init Failed");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(onReceive);
}