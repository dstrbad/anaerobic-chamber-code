#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "log.h"
#include "serial_task.h"
#include "mqtt_task.h"
#include "wifi_manager.h"

void setup() {
    Serial.begin(115200);
    logf("Anaerobic Chamber — ESP32 MQTT Bridge\n");

    // Task watchdog: svaki task se prijavljuje (esp_task_wdt_add) i mora ga
    // hraniti; ako jedan stane dulje od TASK_WDT_TIMEOUT_S, ploča se resetira.
    esp_task_wdt_init(TASK_WDT_TIMEOUT_S, true);

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
