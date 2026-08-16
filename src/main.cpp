#include "main.h"

// ==================== Global Definitions ====================
SemaphoreHandle_t sensorMutex = NULL;
TaskHandle_t mainTaskHandle = NULL;
Preferences preferences;

// ==================== Setup ====================
void setup() {
    SerialMon.begin(115200);
    delay(100);

    SerialMon.println("\n====================================");
    SerialMon.println(" == DL300-26 for fishRus/MoreFish ==");
    SerialMon.println("--------------------------------------");
    SerialMon.printf ("  ==      FW Version: %s        ==", FW_VERSION);
    SerialMon.println();
    SerialMon.printf ("  ==      HW Version: %s        ==", HW_VERSION);
    SerialMon.println();
    SerialMon.println("  ====================================\n");
    SerialMon.println();
    
    FastLED_setup();
    Display_setup();
    sensorMutex = xSemaphoreCreateMutex();
    initSensors();
    
    sendLedCommand(LED_OFFLINE);
    updateStatus("GSM OFFLINE");

    pinMode(ACLINE_PIN, INPUT);  
    
    // Get MAC address for fallback ID
    MAC_FALLBACK_ID = getMACDeviceID();
    SerialMon.println("MAC Fallback ID: " + MAC_FALLBACK_ID);

    // Read or set Device ID
    #if OTA
        preferences.begin("device", true);
        DEVICE_ID = preferences.getString("DID", MAC_FALLBACK_ID);
        preferences.end();
        SerialMon.println("[OTA MODE] Read-only mode: DEVICE_ID = " + DEVICE_ID);
    #else
        preferences.begin("device", false);
        DEVICE_ID = preferences.getString("DID", "");

        if (DEVICE_ID.isEmpty() || DEVICE_ID != UNIQUE_DEVICE_ID) {
            SerialMon.println("[NORMAL MODE] Invalid or missing ID. Writing new ID...");
            DEVICE_ID = UNIQUE_DEVICE_ID;
            preferences.putString("DID", DEVICE_ID);
        }
        preferences.end();
    #endif

    SerialMon.println("Final Device ID: " + DEVICE_ID);
    SerialMon.println("=================================\n");

    preferences.begin("device_config", false);
    deviceArmed = preferences.getBool("armed", true);
    SerialMon.println("Device Armed?: " + String(deviceArmed ? "Yes" : "No"));

    preferences.end();

    GSM_setup();

    mqttPublishQueue = xQueueCreate(MQTT_PUB_QUEUE_SIZE, sizeof(MQTTMessage));

    mesh_gw_setup();

    xTaskCreatePinnedToCore(mainTask, "MainTask", MAIN_TASK_STACK, NULL, MAIN_TASK_PRIORITY, &mainTaskHandle, 1);

    SerialMon.println("Setup complete!!!");
    SerialMon.println("=================================\n");
}

void loop() {
    // FreeRTOS scheduler takes over - nothing to do here
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ==================== Main Task ====================
void mainTask(void* parameter) {
    unsigned long lastHeartbeatTime = 0;
    unsigned long lastDataTime = 0;  // Added to track sensor data interval
    
    SerialMon.println("Main Task started");
    
    while (1) {
        if (otaInProgress) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Send heartbeat every minute
        if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
            if (deviceOnline) {
                publishHeartbeat();
                vTaskDelay(pdMS_TO_TICKS(100));
                publisMoreFishHeartbeat();
                vTaskDelay(pdMS_TO_TICKS(100));
                SerialMon.println("Heartbeat published");
                sendLedCommand(LED_HEARTBEAT);
                lastHeartbeatTime = millis();
            }
        }

        // Send sensor data every DATA_INTERVAL
        if (millis() - lastDataTime >= DATA_INTERVAL) {
            lastDataTime = millis();
            if (deviceOnline) {
                SerialMon.println("Reading sensors and publishing data...");
                publishSensorsData();
                SerialMon.println("Sensor data published");
                sendLedCommand(LED_RF_HB);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==================== MQTT Callback ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    sendLedCommand(LED_MQTT_RECEIVE);
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    message.trim();

    SerialMon.println("\n========== MQTT Message Received ==========");
    SerialMon.print("Topic: ");
    SerialMon.println(topic);
    SerialMon.print("Payload: ");
    SerialMon.println(message);
    SerialMon.println("==========================================\n");

    message.trim();
    message.replace(" ", "");
    SerialMon.println("📥 Message: " + message);

    if (message == "ping") {
        SerialMon.println("MQTT Command: Ping received");
        publishHeartbeat();
        return;
    }

    if (message == "data") {
        SerialMon.println("MQTT Command: Data Request received");
        publishSensorsData();
        return;
    }

    if (message == "get_sim_info") {
        String operatorCode = modem.getOperator();
        String operatorName = operatorCode;

        if (operatorCode == "47001") operatorName = "Grameenphone";
        else if (operatorCode == "47002") operatorName = "Robi";
        else if (operatorCode == "47003") operatorName = "Banglalink";
        else if (operatorCode == "47004") operatorName = "Teletalk";

        Serial.print("Operator: ");
        Serial.println(operatorName);

        String imsi = modem.getSimCCID();
        String simID = "";
        if (imsi.length() >= 18) {
            simID = imsi.substring(imsi.length() - 18);
        }
        Serial.print("SIM ID: ");
        Serial.println(simID);

        MQTTMessage response2;
        snprintf(response2.topic, sizeof(response2.topic), "%s", MQTT_ACK);
        snprintf(response2.payload, sizeof(response2.payload), "['%s','Operator:%s','ICCID:%s']",
                DEVICE_ID.c_str(), operatorName.c_str(), simID.c_str());
        sendLedCommand(LED_PING_ACK);
        xQueueSend(mqttPublishQueue, &response2, pdMS_TO_TICKS(100));
        return;
    }

    if (message == "arm:1") {
        SerialMon.println("MQTT Command: Arm device");
        deviceArmed = true;
        preferences.begin("device_config", false);
        preferences.putBool("armed", true);
        preferences.end();
        return;
    }

    if (message == "arm:0") {
        SerialMon.println("MQTT Command: Disarm device");
        deviceArmed = false;
        preferences.begin("device_config", false);
        preferences.putBool("armed", false);
        preferences.end();
        return;
    }

    if (message.startsWith("ota")) {
        SerialMon.println("MQTT Command: OTA update received");
        firmwareUrl = defaultFirmwareUrl;

        MQTTMessage response;
        snprintf(response.topic, sizeof(response.topic), "%s", MQTT_ACK);
        snprintf(response.payload, sizeof(response.payload), "%s,%s,%s",
                 DEVICE_ID.c_str(), "OTA update starting", firmwareUrl.c_str());
        xQueueSend(mqttPublishQueue, &response, pdMS_TO_TICKS(100));
        createOTATask();
        return;
    }

    if (message.startsWith("http://") || message.startsWith("https://")) {
        firmwareUrl = message;

        MQTTMessage response;
        snprintf(response.topic, sizeof(response.topic), "%s", MQTT_ACK);
        snprintf(response.payload, sizeof(response.payload), "%s,%s,%s",
                 DEVICE_ID.c_str(), "OTA update starting", firmwareUrl.c_str());
        xQueueSend(mqttPublishQueue, &response, pdMS_TO_TICKS(100));
        createOTATask();
        return;
    }

    if (message.startsWith("ota ")) {
        String link = message.substring(4);
        link.trim();
        if (link.startsWith("http://") || link.startsWith("https://")) {
            firmwareUrl = link;
            createOTATask();
            return;
        }
    }

    // Mesh command forwarding
    int commaIndex = message.indexOf(',');
    if (commaIndex < 0) {
        SerialMon.println("⚠️ Format: receiver,command");
        return;
    }

    char receiver[16];
    char command[64];
    strncpy(receiver, message.substring(0, commaIndex).c_str(), sizeof(receiver) - 1);
    receiver[sizeof(receiver) - 1] = '\0';
    strncpy(command, message.substring(commaIndex + 1).c_str(), sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';
    command[strcspn(command, "\r\n")] = 0;
    receiver[strcspn(receiver, "\r\n")] = 0;

    String msg_id = generateMessageID();
    char nodePayload[ESPNOW_MAX_MSG_LEN + 1];
    snprintf(nodePayload, sizeof(nodePayload), "%s,%s,%s,%d,%s,%s,%d",
             Local_ID, receiver, command, MSG_CMD, msg_id.c_str(), Local_ID, 0);

    SerialMon.print("RAW Payload: ");
    SerialMon.println(nodePayload);

    if (useEncryption) {
        SerialMon.println("🔐 Encrypting...");
        char encPayload[ESPNOW_MAX_MSG_LEN + 1];
        encryptSimple(nodePayload, encPayload, enckey);
        SerialMon.print("Encrypted: ");
        SerialMon.println(encPayload);
        esp_now_send(broadcastAddress, (uint8_t*)encPayload, strlen(encPayload));
    } else {
        SerialMon.println("⚠️ Sending without encryption!");
        esp_now_send(broadcastAddress, (uint8_t*)nodePayload, strlen(nodePayload));
    }
}

// ==================== Heartbeat Publishing ====================
void publishHeartbeat() {
    MQTTMessage hbMsg;
    snprintf(hbMsg.topic, sizeof(hbMsg.topic), "%s", MQTT_PUB);
    uint8_t rssi = modem.getSignalQuality();

    int signalLevel = rssiToSignalLevel(rssi);
    updateSignalStrength(signalLevel);

    int health = ESP.getFreeHeap();
    int uptime_m = millis() / 60000;

    bool acLine = digitalRead(ACLINE_PIN) == HIGH;
    bool gsmActive = true;
    bool sdReady = false;

    String payload = String(DEVICE_ID) +
                     ",FWV:" + FW_VERSION +
                     ",HWV:" + HW_VERSION +
                     ",ARMED:" + (deviceArmed ? "1" : "0") +
                     ",DATA_TYPE:" + (gsmActive ? "gsm" : "ERROR") +
                     ",AC_LINE:" + (acLine ? "1" : "0") +
                     ",SD_LOGING:" + (sdReady ? "1" : "0") +
                     ",HEALTH:" + String(health) +
                     ",UP_TIME:" + String(uptime_m) + "Min" +
                     ",RSSI:" + String(rssi);

    Serial.println(payload);
    snprintf(hbMsg.payload, sizeof(hbMsg.payload), "%s", payload.c_str());
    if (xQueueSend(mqttPublishQueue, &hbMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
        SerialMon.println("MainTask: Heartbeat queued");
    }
}

// void publishHeartbeat() {
//     static uint32_t packetNumber = 0;
//     packetNumber++;

//     static char jsonBuffer[512];

//     uint8_t rssi = modem.getSignalQuality();
//     int health = ESP.getFreeHeap();
//     int uptime_m = millis() / 60000;

//     bool acLine = digitalRead(ACLINE_PIN) == HIGH;
//     bool gsmActive = true;
//     bool sdReady = false;

//     const int itemCount = 9;   // number of fields inside "data"

//     int len = 0;
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "{\"status\":\"success\","
//         "\"device_id\":\"%s\","
//         "\"type\":\"heartbeat\","
//         "\"packet\":%u,"
//         "\"timestamp\":NO_TIMESTAMP,"
//         "\"item_count\":%d,"
//         "\"data\":{",
//         DEVICE_ID.c_str(), packetNumber, itemCount);

//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"FWV\":\"%s\",", FW_VERSION);
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"HWV\":\"%s\",", HW_VERSION);
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"ARMED\":\"%s\",", deviceArmed ? "1" : "0");
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"DATA_TYPE\":\"%s\",", gsmActive ? "gsm" : "ERROR");
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"AC_LINE\":\"%s\",", acLine ? "1" : "0");
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"SD_LOGING\":\"%s\",", sdReady ? "1" : "0");
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"HEALTH\":%d,", health);
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"UP_TIME\":\"%dMin\",", uptime_m);
//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "\"RSSI/CSQ\":%d", rssi);

//     len += snprintf(jsonBuffer + len, sizeof(jsonBuffer) - len,
//         "}}");

//     jsonBuffer[sizeof(jsonBuffer) - 1] = '\0';

//     MQTTMessage hbMsg;
//     snprintf(hbMsg.topic, sizeof(hbMsg.topic), "%s", MQTT_PUB);
//     snprintf(hbMsg.payload, sizeof(hbMsg.payload), "%s", jsonBuffer);

//     if (xQueueSend(mqttPublishQueue, &hbMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
//         SerialMon.println("MainTask: Heartbeat queued");
//     }
//     SerialMon.println(jsonBuffer);
// }

void publisMoreFishHeartbeat() {
    MQTTMessage hbMsg;
    snprintf(hbMsg.topic, sizeof(hbMsg.topic), "%s", MQTT_PUB);
    String payload = String(DEVICE_ID) + "," + "Connected";
    Serial.println(payload);
    snprintf(hbMsg.payload, sizeof(hbMsg.payload), "%s", payload.c_str());
    if (xQueueSend(mqttPublishQueue, &hbMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
        SerialMon.println("MainTask: Heartbeat queued");
    }
}

// ==================== Sensor Data Publishing ====================
void publishSensorsData() {
    readAllSensors();

    if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        SerialMon.println("===== Sensor Data =====");

        // Solar
        SerialMon.print("Solar Raw: ");
        SerialMon.println(sensorData.solarRaw);
        SerialMon.print("Solar Pin V: ");
        SerialMon.println(sensorData.solarPinV, 4);
        SerialMon.print("Solar Voltage: ");
        SerialMon.println(sensorData.solarVoltage, 3);
        SerialMon.print("Solar Mapped (0-1024): ");
        SerialMon.println(sensorData.solarMapped, 1);
        SerialMon.println("------------------------\n");
        updateSolarVoltage(sensorData.solarVoltage);

        // Battery
        SerialMon.print("Batt Raw: ");
        SerialMon.println(sensorData.battRaw);
        SerialMon.print("Batt Pin V: ");
        SerialMon.println(sensorData.battPinV, 4);
        SerialMon.print("Battery Voltage: ");
        SerialMon.println(sensorData.batteryVoltage, 3);
        SerialMon.print("Batt Mapped (0-1024): ");
        SerialMon.println(sensorData.battMapped, 1);
        SerialMon.println("------------------------\n");
        

        float batteryVoltage = sensorData.batteryVoltage;
        
        // Update display voltage
        updateBatteryVoltage(batteryVoltage);
        
        // Calculate percentage and update battery icon
        int percent = batteryVoltageToPercent(batteryVoltage);

        //Update Battery Percentage
        updateBatteryPercentage(percent);
        

        // TDS
        SerialMon.print("TDS Raw: ");
        SerialMon.println(sensorData.tdsRaw);
        SerialMon.print("TDS Voltage: ");
        SerialMon.println(sensorData.tdsV, 4);
        SerialMon.print("TDS Mapped (0-1024): ");
        SerialMon.println(sensorData.tdsMapped, 1);
        SerialMon.println("------------------------\n");
        updateTDS(sensorData.tdsMapped);

        // NTC
        SerialMon.print("NTC Raw: ");
        SerialMon.println(sensorData.ntcRaw);
        SerialMon.print("NTC Voltage: ");
        SerialMon.println(sensorData.ntcV, 4);
        SerialMon.print("NTC Mapped (0-1024): ");
        SerialMon.println(sensorData.ntcMapped, 1);
        SerialMon.println("------------------------\n");
        updateTemperature(sensorData.ntcMapped);

        // pH
        SerialMon.print("pH Raw: ");
        SerialMon.println(sensorData.phRaw);
        SerialMon.print("pH Voltage: ");
        SerialMon.println(sensorData.phV, 4);
        SerialMon.print("pH Mapped (0-1024): ");
        SerialMon.println(sensorData.phMapped, 1);
        SerialMon.println("------------------------\n");
        updatePH(sensorData.phMapped);

        SerialMon.println("============ END ============\n");

        // Publish packet 1
        MQTTMessage dataMsg1;
        snprintf(dataMsg1.payload, sizeof(dataMsg1.payload), "%s,%s,%d,%d",
                 DEVICE_ID.c_str(), DEVICE_ID.c_str(),
                 (int)sensorData.battMapped, (int)sensorData.solarMapped);
        strcpy(dataMsg1.topic, MQTT_PUB);
        if (xQueueSend(mqttPublishQueue, &dataMsg1, pdMS_TO_TICKS(100)) == pdTRUE) {
            SerialMon.println("MQTT: Battery & Solar data queued");
        } else {
            SerialMon.println("MQTT: Failed to queue Battery & Solar data");
        }

        // Publish packet 2
        MQTTMessage dataMsg2;
        snprintf(dataMsg2.payload, sizeof(dataMsg2.payload), "%s,%s,%d,%d,%d",
                 DEVICE_ID.c_str(), DEVICE_ID.c_str(),
                 (int)sensorData.tdsMapped, (int)sensorData.ntcMapped, (int)sensorData.phMapped);
        strcpy(dataMsg2.topic, MQTT_PUB);
        if (xQueueSend(mqttPublishQueue, &dataMsg2, pdMS_TO_TICKS(100)) == pdTRUE) {
            SerialMon.println("MQTT: TDS, NTC, pH data queued");
        } else {
            SerialMon.println("MQTT: Failed to queue TDS, NTC, pH data");
        }

        xSemaphoreGive(sensorMutex);
    } else {
        SerialMon.println("Failed to take sensor mutex");
    }
}

// ==================== Helper Functions ====================
void suspendAllTasks() {
    if (xSemaphoreTake(taskSuspendMutex, portMAX_DELAY)) {
        otaInProgress = true;
        tasksSuspended = true;
        vTaskSuspend(mainTaskHandle);
        vTaskSuspend(networkTaskHandle);
        xSemaphoreGive(taskSuspendMutex);
        SerialMon.println("All tasks suspended for OTA");
    }
}

void resumeAllTasks() {
    if (xSemaphoreTake(taskSuspendMutex, portMAX_DELAY)) {
        otaInProgress = false;
        tasksSuspended = false;
        vTaskResume(mainTaskHandle);
        vTaskResume(networkTaskHandle);
        xSemaphoreGive(taskSuspendMutex);
        SerialMon.println("All tasks resumed");
    }
}

// ==================== OTA Functions ====================
bool checkForNewFirmware() {
    SerialMon.println("OTA: Checking for new firmware...");
    // Implement your actual version check here
    return true;  // Placeholder
}

bool parseURL(String url, String &host, String &path, int &port) {
    port = 80;
    if (url.startsWith("http://")) {
        url.remove(0, 7);
    } else if (url.startsWith("https://")) {
        url.remove(0, 8);
        port = 443;
    }
    int slashIndex = url.indexOf('/');
    if (slashIndex == -1) return false;
    String hostPort = url.substring(0, slashIndex);
    path = url.substring(slashIndex);
    int colonIndex = hostPort.indexOf(':');
    if (colonIndex != -1) {
        host = hostPort.substring(0, colonIndex);
        port = hostPort.substring(colonIndex + 1).toInt();
    } else {
        host = hostPort;
    }
    return true;
}

bool performOTAUpdate() {
    SerialMon.println("\n[OTA] Starting OTA update");

    String host;
    String path;
    int port;
    if (!parseURL(firmwareUrl, host, path, port)) {
        SerialMon.println("[OTA] URL parse failed");
        return false;
    }

    SerialMon.printf("[OTA] Host: %s\n", host.c_str());
    SerialMon.printf("[OTA] Path: %s\n", path.c_str());

    TinyGsmClient otaClient(modem);
    unsigned long otaStartTime = millis();
    const unsigned long OTA_TOTAL_TIMEOUT = 300000;

    // Connect
    SerialMon.printf("[OTA] Connecting to %s:%d\n", host.c_str(), port);
    int connectAttempts = 0;
    bool serverConnected = false;
    while (connectAttempts < 3 && !serverConnected) {
        connectAttempts++;
        if (otaClient.connect(host.c_str(), port)) {
            serverConnected = true;
            SerialMon.println("[OTA] Connected to server");
        } else {
            SerialMon.println("[OTA] Connection failed");
            if (connectAttempts < 3) {
                delay(2000);
                if (!modem.isGprsConnected()) {
                    SerialMon.println("[OTA] GPRS lost during OTA, aborting");
                    return false;
                }
            }
        }
    }
    if (!serverConnected) {
        otaClient.stop();
        return false;
    }

    // Send HTTP request
    String request = String("GET ") + path + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "User-Agent: GMS32-OTA\r\n" +
                     "Connection: keep-alive\r\n\r\n";
    otaClient.print(request);

    // Parse response
    unsigned long headerTimeout = millis();
    bool headersComplete = false;
    int contentLength = 0;
    bool httpOK = false;
    while (millis() - headerTimeout < 10000 && !headersComplete) {
        if (otaClient.available()) {
            String line = otaClient.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) {
                headersComplete = true;
                break;
            }
            if (line.startsWith("HTTP/1.") && line.indexOf("200") > 0) httpOK = true;
            if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!headersComplete || !httpOK || contentLength <= 0) {
        otaClient.stop();
        return false;
    }

    if (!Update.begin(contentLength, U_FLASH)) {
        otaClient.stop();
        return false;
    }

    // Download and write
    uint8_t buffer[512];
    size_t totalWritten = 0;
    unsigned long lastDataTime = millis();
    unsigned long lastProgressTime = millis();
    const unsigned long DATA_TIMEOUT = 60000;
    const unsigned long PROGRESS_INTERVAL = 1000;

    while (totalWritten < contentLength) {
        if (millis() - otaStartTime > OTA_TOTAL_TIMEOUT) {
            Update.abort();
            otaClient.stop();
            return false;
        }
        if (!modem.isGprsConnected()) {
            Update.abort();
            otaClient.stop();
            return false;
        }
        if (otaClient.available()) {
            lastDataTime = millis();
            int bytesAvailable = otaClient.available();
            int toRead = min(min(bytesAvailable, (int)sizeof(buffer)),
                             (int)(contentLength - totalWritten));
            if (toRead > 0) {
                int bytesRead = otaClient.read(buffer, toRead);
                if (bytesRead > 0) {
                    if (Update.write(buffer, bytesRead) != bytesRead) {
                        Update.abort();
                        otaClient.stop();
                        return false;
                    }
                    totalWritten += bytesRead;
                    if (millis() - lastProgressTime > PROGRESS_INTERVAL) {
                        float percent = (100.0 * totalWritten) / contentLength;
                        SerialMon.printf("[OTA] Progress: %.1f%% (%d/%d bytes)\n",
                                         percent, totalWritten, contentLength);
                        lastProgressTime = millis();
                    }
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (millis() - lastDataTime > DATA_TIMEOUT) {
                Update.abort();
                otaClient.stop();
                return false;
            }
        }
        esp_task_wdt_reset();
    }

    if (!Update.end() || !Update.isFinished()) {
        otaClient.stop();
        return false;
    }

    SerialMon.println("[OTA] Firmware update successful!");
    otaClient.stop();
    delay(3000);
    ESP.restart();
    return true;
}
