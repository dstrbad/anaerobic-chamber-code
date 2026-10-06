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

// Serial connection to Arduino Mega (Serial1 on Arduino Nano ESP32)
#define SERIAL_MEGA_BAUD 115200
#define SERIAL_MEGA_RX   0   // Nano ESP32 RX1 <- Mega TX1 (pin 1)
#define SERIAL_MEGA_TX   1   // Nano ESP32 TX1 -> Mega RX1 (pin 0)

// Status LED
#define PIN_STATUS_LED 2      // Built-in LED on most ESP32 devkits
