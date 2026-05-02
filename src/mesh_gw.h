#pragma once
#include <WiFi.h>
#include <esp_now.h>
#include <deque>
#include <algorithm>
#include<led.h>

// ================= ENUM =================
typedef enum {
    MSG_CMD = 1,
    MSG_ACK,
    MSG_HB,
    MSG_TMP
} message_type_t;

// Helper for Serial Monitor
const char* getTypeName(message_type_t type) {
    switch(type) {
        case MSG_CMD: return "Command";
        case MSG_ACK: return "Acknowledgement";
        case MSG_HB:  return "Heartbeat";
        case MSG_TMP: return "Temperature";
        default:      return "Unknown";
    }
}

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

// ================= DEVICE =================
const char* Local_ID = "gw0";
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ================= DEDUP =================
std::deque<String> recentMsgKeys;
const size_t maxRecentIDs = 50;

//==================Function Prototypes==================
const char* getTypeName(message_type_t type);
bool isDuplicate(const String& sender, const String& msg_id);
String generateMessageID();
String encryptSimple(String msg, String enckey);
String decryptSimple(String msg, String enckey);
void mesh_setup();
void onReceive(const uint8_t *mac, const uint8_t *incomingData, int len);
//============================================================================//


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

// ================= MESSAGE ID =================
String generateMessageID() {
    uint16_t randNum = esp_random() & 0xFFFF;
    char id[5];
    sprintf(id, "%04X", randNum);
    return String(id);
}

// ================= ENCRYPTION =================
bool useEncryption = true;
String enckey = "dmabd987";
String encCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+=[]{}|:;<>?,./~";

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

    // Dedup
    if (isDuplicate(sender, msg_id)) {
        Serial.println("⚠️ Duplicate ignored");
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

    // LED Indication
    LedBlink blink;
    switch(type) {
        case MSG_ACK: blink = {CRGB::Green,150,1,150}; break;
        case MSG_HB:  blink = {CRGB::Blue,150,1,150}; break;
        case MSG_TMP: blink = {CRGB::Red,150,1,150}; break;
        default: return;
    }

    xQueueSend(ledQueue, &blink, 0);
}


void mesh_setup(){
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