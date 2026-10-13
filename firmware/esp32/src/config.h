#pragma once

// =============================================================================
// ESP32 WiFi/MQTT Bridge — Configuration
// =============================================================================
//
// Network credentials and the broker address live in secrets.h, which is
// not committed. Copy secrets.example.h to secrets.h and fill it in.

#include "secrets.h"

// MQTT
#define MQTT_PORT     1883
#define MQTT_PREFIX   "anaerobic/chamber1/"
#define MQTT_CLIENT_ID "anaerobic-esp32"
#define MQTT_RETRY_MS  5000

// Oporavak: restart ako WiFi/MQTT ne radi ovoliko dugo; task watchdog
// resetira ploču ako neki task stane (mqtt.connect zna blokirati ~15 s).
#define MQTT_UNHEALTHY_RESTART_MS 120000
#define TASK_WDT_TIMEOUT_S        30

// Serial connection to Arduino Mega (Serial1 on Arduino Nano ESP32)
#define SERIAL_MEGA_BAUD 115200
#define SERIAL_MEGA_RX   0   // Nano ESP32 RX1 <- Mega TX1 (pin 1)
#define SERIAL_MEGA_TX   1   // Nano ESP32 TX1 -> Mega RX1 (pin 0)

// Status LED
#define PIN_STATUS_LED LED_BUILTIN   // Built-in LED on Arduino Nano ESP32
