#pragma once
#include<config.h>
#include <FastLED.h>

// ================= LED =================
#define LED_PIN 4
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

QueueHandle_t ledQueue;

typedef struct {
    CRGB color;
    uint16_t duration;
    uint8_t repeat;
    uint16_t gap;
} LedBlink;

void fastLED_setup() {
    FastLED.addLeds<NEOPIXEL,LED_PIN>(leds,NUM_LEDS);

    leds[0]=CRGB::Red; FastLED.show(); delay(250);
    leds[0]=CRGB::Yellow; FastLED.show(); delay(250);
    leds[0]=CRGB::Blue; FastLED.show(); delay(250);
    leds[0]=CRGB::Black; FastLED.show();
}

// ================= LED TASK =================
void ledTask(void *param) {
    LedBlink blink;

    for (;;) {
        if (xQueueReceive(ledQueue, &blink, portMAX_DELAY) == pdTRUE) {
            for (int i = 0; i < blink.repeat; i++) {
                leds[0] = blink.color;
                FastLED.show();
                vTaskDelay(pdMS_TO_TICKS(blink.duration));

                leds[0] = CRGB::Black;
                FastLED.show();

                if (i < blink.repeat - 1)
                    vTaskDelay(pdMS_TO_TICKS(blink.gap));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
