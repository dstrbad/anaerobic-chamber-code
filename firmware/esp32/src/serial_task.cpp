#include <Arduino.h>
#include <esp_task_wdt.h>
#include "serial_task.h"
#include "config.h"
#include "log.h"

char sharedJsonBuffer[512] = {0};
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t newDataSemaphore = nullptr;

void serialTask(void* pvParameters) {
    Serial1.begin(SERIAL_MEGA_BAUD, SERIAL_8N1, SERIAL_MEGA_RX, SERIAL_MEGA_TX);
    logf("Serial1 started: RX=%d, TX=%d, baud=%d\n", SERIAL_MEGA_RX, SERIAL_MEGA_TX, SERIAL_MEGA_BAUD);

    esp_task_wdt_add(nullptr);

    // Configure status LED
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    String lineBuffer;
    lineBuffer.reserve(512);

    // Bez ispisa po primljenom bajtu/liniji: ovaj task ima prioritet 2 i
    // blokirani USB ispis bi izgladnio mqttTask na istoj jezgri.
    for (;;) {
        esp_task_wdt_reset();

        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                if (lineBuffer.length() > 0 && lineBuffer[0] == '{') {
                    // Valid JSON line received
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        strncpy(sharedJsonBuffer, lineBuffer.c_str(), sizeof(sharedJsonBuffer) - 1);
                        sharedJsonBuffer[sizeof(sharedJsonBuffer) - 1] = '\0';
                        xSemaphoreGive(dataMutex);
                        xSemaphoreGive(newDataSemaphore);

                        // Blink LED on successful receive
                        digitalWrite(PIN_STATUS_LED, HIGH);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        digitalWrite(PIN_STATUS_LED, LOW);
                    }
                }
                lineBuffer = "";
            } else if (lineBuffer.length() < 500) {
                lineBuffer += c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
