// display.cpp
#include "display.h"

// ============================================================
// Global object definitions
// ============================================================
Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &SPI,
    OLED_DC,
    OLED_RST,
    OLED_CS
);

DisplayData displayData = {
    18.6,      // solarVoltage
    6.70,      // batteryVoltage
    7.07,      // ph
    400.0,     // tds
    5.06,      // dissolvedOxygen
    28.05,     // temperature
    5,         // signalLevel
    "ONLINE"   // statusText
};

TaskHandle_t displayTaskHandle = NULL;
QueueHandle_t displayCommandQueue = NULL;

// ============================================================
// Helper functions (static to avoid external linkage)
// ============================================================
static void drawBatteryIcon(float voltage) {
    int fillWidth = 0;
    if (voltage >= 4.2) fillWidth = 12;
    else if (voltage <= 3.0) fillWidth = 0;
    else fillWidth = (int)((voltage - 3.0) / (4.2 - 3.0) * 12);

    display.drawRect(108, 1, 16, 7, SSD1306_WHITE);
    display.fillRect(124, 3, 2, 3, SSD1306_WHITE);
    if (fillWidth > 0) {
        display.fillRect(110, 3, fillWidth, 3, SSD1306_WHITE);
    }
}

static void drawSignalBars(int level) {
    if (level >= 1) display.fillRect(112, 21, 2, 3,  SSD1306_WHITE);
    if (level >= 2) display.fillRect(115, 19, 2, 5,  SSD1306_WHITE);
    if (level >= 3) display.fillRect(118, 17, 2, 7,  SSD1306_WHITE);
    if (level >= 4) display.fillRect(121, 15, 2, 9,  SSD1306_WHITE);
    if (level >= 5) display.fillRect(124, 13, 2, 11, SSD1306_WHITE);
}

// ============================================================
// Display functions
// ============================================================
static void displayBootAnimation() {
    const int barX = 14;
    const int barY = 50;
    const int barWidth = 100;
    const int barHeight = 8;
    const uint32_t animationTime = 5000;
    const uint32_t startTime = millis();

    while (millis() - startTime < animationTime) {
        uint32_t elapsed = millis() - startTime;
        int progress = (elapsed * 100) / animationTime;
        if (progress > 100) progress = 100;

        display.clearDisplay();
        display.setTextSize(3);
        display.setTextColor(SSD1306_WHITE);
        const char* title = "DMA";
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 5);
        display.println(title);

        display.setTextSize(1);
        const char* model = "DL300-26";
        display.getTextBounds(model, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 34);
        display.println(model);

        display.drawRoundRect(barX, barY, barWidth, barHeight, 2, SSD1306_WHITE);
        int fillWidth = ((barWidth - 4) * progress) / 100;
        if (fillWidth > 0) {
            display.fillRoundRect(barX + 2, barY + 2, fillWidth, barHeight - 4, 1, SSD1306_WHITE);
        }
        display.display();
        delay(20);
    }

    // Finish at 100%
    display.clearDisplay();
    display.setTextSize(3);
    const char* title = "DMA";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 5);
    display.println(title);

    display.setTextSize(1);
    const char* model = "DL300-26";
    display.getTextBounds(model, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 34);
    display.println(model);

    display.drawRoundRect(barX, barY, barWidth, barHeight, 2, SSD1306_WHITE);
    display.fillRoundRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, 1, SSD1306_WHITE);
    display.display();
    delay(200);
}

static void displayWelcomeScreen() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1;
    uint16_t w, h;

    display.setTextSize(1);
    const char* welcome = "Welcome to";
    display.getTextBounds(welcome, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 6);
    display.println(welcome);

    const char* product = "DMA DataLogger";
    display.getTextBounds(product, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 22);
    display.println(product);

    const char* model = "Model: DL300-26";
    display.getTextBounds(model, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 38);
    display.println(model);

    const char* powered = "Powered by DMA";
    display.getTextBounds(powered, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 53);
    display.println(powered);

    display.display();
    delay(5000);
}

static void displayHomePage() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    const int LEFT  = 2;
    const int RIGHT = 125;
    const int DIV   = 63;

    display.setCursor(LEFT, 1);
    display.print("DMA DataLogger");

    drawBatteryIcon(displayData.batteryVoltage);

    display.drawLine(LEFT, 10, RIGHT, 10, SSD1306_WHITE);

    display.setCursor(LEFT, 13);
    display.print("Status: ");
    display.print(displayData.statusText);

    drawSignalBars(displayData.signalLevel);

    display.drawLine(LEFT, 27, RIGHT, 27, SSD1306_WHITE);

    char buffer[20];
    snprintf(buffer, sizeof(buffer), "Sol:%.1fV", displayData.solarVoltage);
    display.setCursor(LEFT, 30);
    display.print(buffer);

    snprintf(buffer, sizeof(buffer), "Bat:%.2fV", displayData.batteryVoltage);
    display.setCursor(67, 30);
    display.print(buffer);

    display.drawLine(DIV, 28, DIV, 39, SSD1306_WHITE);
    display.drawLine(LEFT, 40, RIGHT, 40, SSD1306_WHITE);

    snprintf(buffer, sizeof(buffer), "pH:%.2f", displayData.ph);
    display.setCursor(LEFT, 43);
    display.print(buffer);

    snprintf(buffer, sizeof(buffer), "Tmp:%.2f", displayData.temperature);
    display.setCursor(LEFT, 54);
    display.print(buffer);

    snprintf(buffer, sizeof(buffer), "TDS:%.0f", displayData.tds);
    display.setCursor(67, 43);
    display.print(buffer);

    snprintf(buffer, sizeof(buffer), "DO:%.2f", displayData.dissolvedOxygen);
    display.setCursor(67, 54);
    display.print(buffer);

    display.drawLine(DIV, 41, DIV, 63, SSD1306_WHITE);

    display.display();
}

// ============================================================
// Public functions
// ============================================================
void Display_setup() {
    SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
    if (!display.begin(SSD1306_SWITCHCAPVCC)) {
        Serial.println("SSD1306 initialization failed!");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    displayBootAnimation();
    // displayWelcomeScreen(); // optional
    displayHomePage();

    displayCommandQueue = xQueueCreate(DISPLAY_CMD_QUEUE_SIZE, sizeof(DisplayCommandType));
    if (displayCommandQueue == NULL) {
        Serial.println("Display queue creation failed!");
        return;
    }

    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        DISPLAY_TASK_STACK,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &displayTaskHandle,
        0
    );
}

void sendDisplayCommand(DisplayCommandType type) {
    if (displayCommandQueue == NULL) return;
    xQueueSend(displayCommandQueue, &type, pdMS_TO_TICKS(10));
}

void updateSolarVoltage(float value) {
    displayData.solarVoltage = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateBatteryVoltage(float value) {
    displayData.batteryVoltage = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updatePH(float value) {
    displayData.ph = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateTDS(float value) {
    displayData.tds = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateDissolvedOxygen(float value) {
    displayData.dissolvedOxygen = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateTemperature(float value) {
    displayData.temperature = value;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateSignalStrength(int level) {
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    displayData.signalLevel = level;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateStatus(const char* status) {
    displayData.statusText = status;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

void updateAllDisplayValues(float solar, float batt, float ph, float tds, float doVal, float temp, int signal, const char* status) {
    displayData.solarVoltage = solar;
    displayData.batteryVoltage = batt;
    displayData.ph = ph;
    displayData.tds = tds;
    displayData.dissolvedOxygen = doVal;
    displayData.temperature = temp;
    if (signal < 0) signal = 0;
    if (signal > 5) signal = 5;
    displayData.signalLevel = signal;
    displayData.statusText = status;
    sendDisplayCommand(DISPLAY_UPDATE_HOME);
}

// ============================================================
// Display task
// ============================================================
void displayTask(void* parameter) {
    DisplayCommandType cmd;
    Serial.println("Display Task started");

    while (1) {
        if (xQueueReceive(displayCommandQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd) {
                case DISPLAY_BOOT:
                    displayBootAnimation();
                    break;
                case DISPLAY_HOME:
                case DISPLAY_UPDATE_HOME:
                    displayHomePage();
                    break;
                case DISPLAY_CLEAR:
                    display.clearDisplay();
                    display.display();
                    break;
                default:
                    break;
            }
        }
    }
}