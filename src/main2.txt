#include<main.h>


// ================= MAIN TASK =================
void mainTask(void *param) {
    for (;;) {
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            input.trim();
            input.replace(" ", "");

            Serial.println("📥 Input: " + input);

            int commaIndex = input.indexOf(',');
            if (commaIndex < 0) {
                Serial.println("⚠️ Format: receiver,command");
                continue;
            }

            Message msg;
            msg.sender_id = Local_ID;
            msg.receiver_id = input.substring(0, commaIndex);
            msg.command = input.substring(commaIndex + 1);
            msg.type = MSG_CMD;
            msg.msg_id = generateMessageID();
            msg.last_hop = Local_ID;
            msg.hop_count = 0;

            String payload =
                msg.sender_id + "," +
                msg.receiver_id + "," +
                msg.command + "," +
                String(msg.type) + "," +
                msg.msg_id + "," +
                msg.last_hop + "," +
                String(msg.hop_count);
            
            if(useEncryption) {
                Serial.println("🔐 Encrypting...");
                String encPayload = encryptSimple(payload, enckey);
                Serial.println("📤 CMD Sent: " + encPayload);
                esp_now_send(broadcastAddress, (uint8_t*)encPayload.c_str(), encPayload.length());
                Serial.println("📤 CMD Sent: " + decryptSimple(encPayload, enckey));
            } else {
                Serial.println("⚠️ Sending without encryption!");
                Serial.println("📤 CMD Sent: " + payload);
                esp_now_send(broadcastAddress, (uint8_t*)payload.c_str(), payload.length());
            }

            
        }
    }
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);

    Serial.println("🔄 Starting Gateway...");
    fastLED_setup();
    mesh_setup();

    ledQueue = xQueueCreate(10, sizeof(LedBlink));

    xTaskCreatePinnedToCore(ledTask, "LED Task", 2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(mainTask, "Main Task", 8192, NULL, 1, &mainTaskHandle, 1);

    Serial.println("✅ Gateway Ready!");
}

// ================= LOOP =================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(100));
}
