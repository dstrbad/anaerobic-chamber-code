#include <Arduino.h>
#include "serial_task.h"
#include "config.h"

char sharedJsonBuffer[512] = {0};
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t newDataSemaphore = nullptr;

void serialTask(void* pvParameters) {
    Serial1.begin(SERIAL_MEGA_BAUD, SERIAL_8N1, SERIAL_MEGA_RX, SERIAL_MEGA_TX);
    Serial.printf("Serial1 started: RX=%d, TX=%d, baud=%d\n", SERIAL_MEGA_RX, SERIAL_MEGA_TX, SERIAL_MEGA_BAUD);
    Serial.println("Listening for Mega data...");

    // Configure status LED
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    String lineBuffer;
    lineBuffer.reserve(512);

    for (;;) {
        // Debug: check if any data available
        if (Serial1.available()) {
            Serial.printf("Serial1 available: %d bytes\n", Serial1.available());
        }

        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                if (lineBuffer.length() > 0 && lineBuffer[0] == '{') {
                    // Valid JSON line received
                    Serial.print("RX: ");
                    Serial.println(lineBuffer);

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
