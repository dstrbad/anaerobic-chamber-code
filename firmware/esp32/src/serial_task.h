#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Shared data buffer (written by serial task, read by MQTT task)
extern char sharedJsonBuffer[512];
extern SemaphoreHandle_t dataMutex;
extern SemaphoreHandle_t newDataSemaphore;

void serialTask(void* pvParameters);
