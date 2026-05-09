#include<main.h>

// ==================== FreeRTOS Configuration ====================

// Task handles
TaskHandle_t mainTaskHandle = NULL;

// Main Task priorities
#define MAIN_TASK_PRIORITY 2

// Main Task stack sizes
#define MAIN_TASK_STACK 4096

// Queue sizes
#define MQTT_PUB_QUEUE_SIZE 20

//Function prototypes
void mainTask(void* parameter);
void publishHeartbeat();
void publishData();

// ==================== Setup ====================
void setup() {
    SerialMon.begin(115200);
    delay(100);

    SerialMon.println("\n====================================");
    SerialMon.println("  ==    DMA IoT Security GATEWAY    ==");
    SerialMon.println("--------------------------------------");
    SerialMon.printf ("  ==      FW Version: %s        ==", FW_VERSION);
    SerialMon.println();
    SerialMon.printf ("  ==      HW Version: %s        ==", HW_VERSION);
    SerialMon.println();
    SerialMon.println("  ====================================\n");
    SerialMon.println();
    
    FastLED_setup();
    sendLedCommand(LED_OFFLINE);
        
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

    // maxRecentIDs = preferences.getInt("dedup_size", 250);
    // SerialMon.println("Deduplication Size: " + String(maxRecentIDs));
    preferences.end();

    // ---- LDR SENSOR SETUP ----
    #ifdef USE_LDR_SENSOR
        Serial.println("🔄 Initializing LDR Sensor...");
        pinMode(LDR_PIN, INPUT);
        Serial.println("\n✅ LDR Sensor Test");
    #endif

    GSM_setup();

    mqttPublishQueue = xQueueCreate(MQTT_PUB_QUEUE_SIZE, sizeof(MQTTMessage));

    mesh_gw_setup();

    // Create tasks
    xTaskCreatePinnedToCore(mainTask, "MainTask", MAIN_TASK_STACK, NULL, MAIN_TASK_PRIORITY, &mainTaskHandle, 1);

    SerialMon.println("Setup complete!!!");
    SerialMon.println("=================================\n");
}

void loop() {
    // FreeRTOS scheduler takes over - nothing to do here
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ==================== Task Implementations ====================
void mainTask(void* parameter) {
    unsigned long lastHeartbeatTime = 0;
    MQTTMessage mqttMsg;
    
    SerialMon.println("Main Task started");
    
    while (1) {
        if (otaInProgress) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        //SerialData Handle
        //====================================//
        /*
        if (SerialMon.available()) {
            String input = SerialMon.readStringUntil('\n');
            input.trim();
            input.replace(" ", "");

            SerialMon.println("📥 Input: " + input);

            int commaIndex = input.indexOf(',');
            if (commaIndex < 0) {
                SerialMon.println("⚠️ Format: receiver,command");
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
                SerialMon.println("🔐 Encrypting...");
                String encPayload = encryptSimple(payload, enckey);
                SerialMon.println("📤 CMD Sent: " + encPayload);
                esp_now_send(broadcastAddress, (uint8_t*)encPayload.c_str(), encPayload.length());
                SerialMon.println("📤 CMD Sent: " + decryptSimple(encPayload, enckey));
            } else {
                SerialMon.println("⚠️ Sending without encryption!");
                SerialMon.println("📤 CMD Sent: " + payload);
                esp_now_send(broadcastAddress, (uint8_t*)payload.c_str(), payload.length());
            }

            
        }
            */
        //====================================//
        
        // Send heartbeat every minute
        if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
            if (deviceOnline) {
                publishHeartbeat();
                sendLedCommand(LED_HEARTBEAT);
                lastHeartbeatTime = millis();
            }
        }

        if(millis() - lastDataTime >= DATA_INTERVAL) {
            lastDataTime = millis();
            if(deviceOnline) {
                // Read sensors and publish data
                SerialMon.println("Reading sensors and publishing data...");
                publishData();
                // Implement sensor reading and MQTT publishing here
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
//================================================================

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

    message.trim();           // Removes leading/trailing whitespace
    message.replace(" ", ""); // Removes all internal spaces
    SerialMon.println("📥 Message: " + message);

    if(message == "ping") {
        SerialMon.println("MQTT Command: Ping received");
        MQTTMessage response;
        snprintf(response.topic, sizeof(response.topic), "%s", MQTT_LP_ACK);
        snprintf(response.payload, sizeof(response.payload), "%s,%s,%d", DEVICE_ID.c_str(),"gsm_connected", modem.getSignalQuality());
        sendLedCommand(LED_PING_ACK);
        xQueueSend(mqttPublishQueue, &response, pdMS_TO_TICKS(100));
        return;
    }

    if(message == "get_sim_info"){
        String operatorCode = modem.getOperator();
        String operatorName = operatorCode;

        // Convert operator code to readable name
        if (operatorCode == "47001") {
            operatorName = "Grameenphone";
        }
        else if (operatorCode == "47002") {
            operatorName = "Robi";
        }
        else if (operatorCode == "47003") {
            operatorName = "Banglalink";
        }
        else if (operatorCode == "47004") {
            operatorName = "Teletalk";
        }

        Serial.print("Operator: ");
        Serial.println(operatorName);


        // ================= IMSI =================

        String imsi = modem.getIMSI();

        // Last 8 digit only
        String simID = "";

        if (imsi.length() >= 8) {
            simID = imsi.substring(imsi.length() - 8);
        }

        Serial.print("SIM ID: ");
        Serial.println(simID);

        MQTTMessage response2;
        snprintf(response2.topic, sizeof(response2.topic), "%s", MQTT_LP_ACK);
        snprintf(response2.payload, sizeof(response2.payload), "%s,%s,%s", DEVICE_ID.c_str(),operatorName, simID);
        sendLedCommand(LED_PING_ACK);
        xQueueSend(mqttPublishQueue, &response2, pdMS_TO_TICKS(100));
        return;

    }

    if(message == "arm:1") {
        SerialMon.println("MQTT Command: Arm device");
        deviceArmed = true;

        preferences.begin("device_config", false);
        preferences.putBool("armed", true);
        SerialMon.println("Device Armed?: " + String(deviceArmed ? "Yes" : "No"));
        preferences.end();

        return;
    }

    if(message == "arm:0") {
        SerialMon.println("MQTT Command: Disarm device");
        deviceArmed = false;

        preferences.begin("device_config", false);
        preferences.putBool("armed", false);
        SerialMon.println("Device Armed?: " + String(deviceArmed ? "Yes" : "No"));
        preferences.end();

        return;
    }

    else if(message.startsWith("ota")) {
        SerialMon.println("MQTT Command: OTA update received");
        firmwareUrl = defaultFirmwareUrl;

        MQTTMessage response;
        snprintf(response.topic, sizeof(response.topic), "%s", MQTT_LP_ACK);
        snprintf(response.payload, sizeof(response.payload), "%s,%s,%s", DEVICE_ID.c_str(),"OTA update starting", firmwareUrl.c_str());
        xQueueSend(mqttPublishQueue, &response, pdMS_TO_TICKS(100));

        createOTATask();
        return;
    }

    else if (message.startsWith("http://") || message.startsWith("https://")) {
        firmwareUrl = message;

        MQTTMessage response;
        snprintf(response.topic, sizeof(response.topic), "%s", MQTT_LP_ACK);
        snprintf(response.payload, sizeof(response.payload), "%s,%s,%s", DEVICE_ID.c_str(),"OTA update starting", firmwareUrl.c_str());
        xQueueSend(mqttPublishQueue, &response, pdMS_TO_TICKS(100));

        createOTATask();
        return;
    } else if (message.startsWith("ota ")) {
        String link = message.substring(4);
        link.trim();
        if (link.startsWith("http://") || link.startsWith("https://")) {
            firmwareUrl = link;
            createOTATask();
            return;
        }
    }
    
    int commaIndex = message.indexOf(',');
    if (commaIndex < 0) {
        SerialMon.println("⚠️ Format: receiver,command");
        return;
    }

    Message msg;
    msg.sender_id = Local_ID;
    msg.receiver_id = message.substring(0, commaIndex);
    msg.command = message.substring(commaIndex + 1);
    msg.type = MSG_CMD;
    msg.msg_id = generateMessageID();
    msg.last_hop = Local_ID;
    msg.hop_count = 0;

    String nodePayload =
        msg.sender_id + "," +
        msg.receiver_id + "," +
        msg.command + "," +
        String(msg.type) + "," +
        msg.msg_id + "," +
        msg.last_hop + "," +
        String(msg.hop_count);
    
    if(useEncryption) {
        SerialMon.println("🔐 Encrypting...");
        String encPayload = encryptSimple(nodePayload, enckey);
        SerialMon.println("📤 CMD Sent: " + encPayload);
        esp_now_send(broadcastAddress, (uint8_t*)encPayload.c_str(), encPayload.length());
        SerialMon.println("📤 CMD Sent: " + decryptSimple(encPayload, enckey));
    } else {
        SerialMon.println("⚠️ Sending without encryption!");
        SerialMon.println("📤 CMD Sent: " + nodePayload);
        esp_now_send(broadcastAddress, (uint8_t*)nodePayload.c_str(), nodePayload.length());
    }
    
}
//==================================================================

// HB = 1191032506160004,FWV:V1.201,HWV:3.0,ARMED:1,DATA_TYPE:gsm,AC_LINE:0,SD_LOGING:1,HEALTH:123456,UP_TIME:789Min,RSSI:-20
void publishHeartbeat() {
    MQTTMessage hbMsg;
    snprintf(hbMsg.topic, sizeof(hbMsg.topic), "%s", MQTT_LP_HB);
    uint8_t rssi = modem.getSignalQuality();
    int health = ESP.getFreeHeap();
    int uptime_m = millis() / 60000;

    bool acLine = digitalRead(ACLINE_PIN) == HIGH ? true : false;
    bool wifiActive = false;
    bool gsmActive = true;
    bool sdReady = false;

    String payload = String(DEVICE_ID) +  + 
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

// LDR to Lux conversion function
#ifdef USE_LDR_SENSOR
  float ldrToLux(int adc) {
    // Known calibration points
    const int ADC_vals[5] = {4048, 3800, 2096, 1966, 1600};
    const float Lux_vals[5] = {4048, 960, 16.67, 11.67, 10.83};

    // If out of range
    if(adc >= ADC_vals[0]) return Lux_vals[0];
    if(adc <= ADC_vals[4]) return Lux_vals[4];

    // Find which segment
    for(int i=0; i<4; i++){
        if(adc <= ADC_vals[i] && adc >= ADC_vals[i+1]){
        float log_adc1 = log(ADC_vals[i]);
        float log_adc2 = log(ADC_vals[i+1]);
        float log_lux1 = log(Lux_vals[i]);
        float log_lux2 = log(Lux_vals[i+1]);

        float log_adc = log(adc);
        float log_lux = log_lux1 + (log_lux2 - log_lux1) * (log_adc - log_adc1) / (log_adc2 - log_adc1);

        return exp(log_lux);  // return interpolated Lux
        }
    }
    return 0; // fallback
  }
#endif

void publishData(){
  
    float lux = -1;
    // Read LDR value and convert to Lux
    #ifdef USE_LDR_SENSOR
        Serial.print("LDR Value: ");
        int ldrValue = analogRead(LDR_PIN);
        ldrValue = map(ldrValue, 0, 4095, 4095, 0); // Invert reading
        Serial.print(ldrValue);
        Serial.println();

        lux = ldrToLux(ldrValue);
        // lux = lux * 1.45; // Calibration factor
        Serial.print("Calculated Lux: ");
        Serial.print(lux, 2);
        Serial.println(" lx");
    #endif
    //========================================//

    // Get light level from BH1750
    #ifdef USE_GY30
        
        if (lightMeter.measurementReady()) {
        lux = lightMeter.readLightLevel();
        Serial.print("BH1750 Light Level: ");
        Serial.print(lux, 2);
        Serial.println(" lx");
        } else {
        Serial.println("BH1750 Measurement not ready");
        }
    #endif
    //========================================//

    // Prepare and send MQTT data message
    MQTTMessage dataMsg;
    snprintf(dataMsg.topic, sizeof(dataMsg.topic), "%s", MQTT_LP_PUB);
    String payload = String(DEVICE_ID) + ",LUX:" + (lux >= 0 ? String(lux, 2) : "ERROR");
    snprintf(dataMsg.payload, sizeof(dataMsg.payload), "%s", payload.c_str());
    
    if (xQueueSend(mqttPublishQueue, &dataMsg, pdMS_TO_TICKS(100)) == pdTRUE) {
        SerialMon.println("MainTask: Data message queued");
    }
}

// ==================== Helper Functions ====================
void suspendAllTasks() {
    if (xSemaphoreTake(taskSuspendMutex, portMAX_DELAY)) {
        otaInProgress = true;
        tasksSuspended = true;
        
        vTaskSuspend(mainTaskHandle);
        vTaskSuspend(networkTaskHandle);
        // LED task continues to show OTA status
        
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
    
    // Implement your firmware version check logic here
    // This should compare current FW_VERSION with server version
    
    return true;  // Return true if new firmware is available
    // return false;  // Return true if new firmware is available
}

bool parseURL(String url, String &host, String &path, int &port)
{
    port = 80;

    // Remove protocol
    if (url.startsWith("http://")) {
        url.remove(0, 7);
    }
    else if (url.startsWith("https://")) {
        url.remove(0, 8);
        port = 443;
    }

    int slashIndex = url.indexOf('/');
    if (slashIndex == -1) return false;

    String hostPort = url.substring(0, slashIndex);
    path = url.substring(slashIndex);

    // Check if port exists
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
    
    // DON'T disconnect GSM here - OTA needs active connection!
    // Keep GPRS connected throughout OTA
    
    // Create a dedicated client for OTA
    TinyGsmClient otaClient(modem);
    
    unsigned long otaStartTime = millis();
    const unsigned long OTA_TOTAL_TIMEOUT = 300000; // 5 minutes total timeout
    
    // ========== STEP 1: CONNECT TO OTA SERVER ==========
    // SerialMon.printf("[OTA] Connecting to %s:%d\n", otaHost, otaPort);
    SerialMon.printf("[OTA] Connecting to %s:%d\n", host.c_str(), port);
    
    int connectAttempts = 0;
    bool serverConnected = false;
    
    while (connectAttempts < 3 && !serverConnected) {
        connectAttempts++;
        SerialMon.printf("[OTA] Connection attempt %d/3\n", connectAttempts);
        
        if (otaClient.connect(host.c_str(), port)) {
            serverConnected = true;
            SerialMon.println("[OTA] Connected to server");
        } else {
            SerialMon.println("[OTA] Connection failed");
            if (connectAttempts < 3) {
                delay(2000);
                
                // Check if GSM is still connected
                if (!modem.isGprsConnected()) {
                    SerialMon.println("[OTA] GPRS lost during OTA, aborting");
                    return false;
                }
            }
        }
    }
    
    if (!serverConnected) {
        SerialMon.println("[OTA] Failed to connect to server");
        otaClient.stop();
        return false;
    }
    
    // ========== STEP 2: SEND HTTP REQUEST ==========
    SerialMon.printf("[OTA] Requesting firmware: %s\n", path.c_str());
    
    String request = String("GET ") + path + " HTTP/1.1\r\n" +
                    "Host: " + host.c_str() + "\r\n" +
                    "User-Agent: GMS32-OTA\r\n" +
                    "Connection: keep-alive\r\n" +
                    "\r\n";
    
    otaClient.print(request);
    
    // ========== STEP 3: PARSE HTTP RESPONSE ==========
    unsigned long headerTimeout = millis();
    const unsigned long HEADER_TIMEOUT = 10000;
    bool headersComplete = false;
    int contentLength = 0;
    bool httpOK = false;
    
    SerialMon.println("[OTA] Waiting for HTTP response...");
    
    while (millis() - headerTimeout < HEADER_TIMEOUT && !headersComplete) {
        if (otaClient.available()) {
            String line = otaClient.readStringUntil('\n');
            line.trim();
            
            SerialMon.println("[OTA HDR] " + line);
            
            if (line.length() == 0) {
                // Empty line indicates end of headers
                headersComplete = true;
                break;
            }
            
            if (line.startsWith("HTTP/1.")) {
                if (line.indexOf("200") > 0) {
                    httpOK = true;
                }
            }
            
            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(15).toInt();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (!headersComplete) {
        SerialMon.println("[OTA] HTTP header timeout");
        otaClient.stop();
        return false;
    }
    
    if (!httpOK) {
        SerialMon.println("[OTA] HTTP not OK");
        otaClient.stop();
        return false;
    }
    
    if (contentLength <= 0) {
        SerialMon.println("[OTA] Invalid content length");
        otaClient.stop();
        return false;
    }
    
    SerialMon.printf("[OTA] Firmware size: %d bytes\n", contentLength);
    
    // ========== STEP 4: PREPARE UPDATE ==========
    if (!Update.begin(contentLength, U_FLASH)) {
        SerialMon.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
        otaClient.stop();
        return false;
    }
    
    // ========== STEP 5: DOWNLOAD FIRMWARE ==========
    uint8_t buffer[512];
    size_t totalWritten = 0;
    unsigned long lastDataTime = millis();
    unsigned long lastProgressTime = millis();
    const unsigned long DATA_TIMEOUT = 60000; // 30 seconds timeout between data
    const unsigned long PROGRESS_INTERVAL = 1000; // Update progress every second
    
    SerialMon.printf("[OTA] Firmware URL: %s\n", firmwareUrl.c_str());
    SerialMon.println("[OTA] Starting firmware download...");
    
    while (totalWritten < contentLength) {
        // Check total timeout
        if (millis() - otaStartTime > OTA_TOTAL_TIMEOUT) {
            SerialMon.printf("[OTA] Total OTA timeout (%lu ms)\n", millis() - otaStartTime);
            Update.abort();
            otaClient.stop();
            return false;
        }
        
        // Check if GSM is still connected
        if (!modem.isGprsConnected()) {
            SerialMon.println("[OTA] GPRS connection lost during download");
            Update.abort();
            otaClient.stop();
            return false;
        }
        
        // Read available data
        if (otaClient.available()) {
            lastDataTime = millis();
            
            int bytesAvailable = otaClient.available();
            int toRead = min(min(bytesAvailable, (int)sizeof(buffer)), 
                           (int)(contentLength - totalWritten));
            
            if (toRead > 0) {
                int bytesRead = otaClient.read(buffer, toRead);
                
                if (bytesRead > 0) {
                    // Write to flash
                    if (Update.write(buffer, bytesRead) != bytesRead) {
                        SerialMon.printf("[OTA] Write error at byte %d\n", totalWritten);
                        Update.abort();
                        otaClient.stop();
                        return false;
                    }
                    
                    totalWritten += bytesRead;
                    
                    // Show progress
                    if (millis() - lastProgressTime > PROGRESS_INTERVAL) {
                        float percent = (100.0 * totalWritten) / contentLength;
                        SerialMon.printf("[OTA] Progress: %.1f%% (%d/%d bytes)\n", 
                                       percent, totalWritten, contentLength);
                        lastProgressTime = millis();
                    }
                }
            }
        } else {
            // No data available, small delay
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Check for data timeout
            if (millis() - lastDataTime > DATA_TIMEOUT) {
                SerialMon.printf("[OTA] Data timeout after %lu ms (at %d/%d bytes)\n", 
                               millis() - lastDataTime, totalWritten, contentLength);
                Update.abort();
                otaClient.stop();
                return false;
            }
        }
        
        // Feed watchdog
        esp_task_wdt_reset();
    }
    
    // ========== STEP 6: FINALIZE UPDATE ==========
    SerialMon.println("\n[OTA] Download complete, finalizing...");
    
    if (!Update.end()) {
        SerialMon.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        otaClient.stop();
        return false;
    }
    
    if (!Update.isFinished()) {
        SerialMon.println("[OTA] Update not finished");
        otaClient.stop();
        return false;
    }
    
    SerialMon.println("[OTA] Firmware update successful!");
    otaClient.stop();
    
    // Wait a moment and restart
    SerialMon.println("[OTA] Restarting in 3 seconds...");
    delay(3000);
    
    ESP.restart();
    
    return true; // Never reaches here
}

