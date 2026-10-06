#include <Arduino.h>
#include "config.h"
#include "serial_task.h"
#include "mqtt_task.h"
#include "wifi_manager.h"

void setup() {
    Serial.begin(115200);
    Serial.println("Anaerobic Chamber — ESP32 MQTT Bridge");

    // Create synchronization primitives
    dataMutex = xSemaphoreCreateMutex();
    newDataSemaphore = xSemaphoreCreateBinary();

    // Launch FreeRTOS tasks
    xTaskCreatePinnedToCore(wifiTask,   "wifi",   4096, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(serialTask, "serial", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(mqttTask,   "mqtt",   8192, nullptr, 1, nullptr, 1);
}

void loop() {
    // All work is done in FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}
