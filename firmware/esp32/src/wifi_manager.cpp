#include <Arduino.h>
#include <WiFi.h>
#include "wifi_manager.h"
#include "config.h"

static const char* wifiStatusStr(wl_status_t s) {
    switch (s) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (AP not found)";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED (wrong password?)";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        case WL_NO_SHIELD:       return "NO_SHIELD";
        default:                 return "UNKNOWN";
    }
}

void wifiTask(void* pvParameters) {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep zna rušiti dugotrajnu MQTT vezu
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            digitalWrite(PIN_STATUS_LED, LOW);
            Serial.print("WiFi: not connected, status=");
            Serial.println(wifiStatusStr(WiFi.status()));

            // Potpuni reset radija prije ponovnog spajanja
            WiFi.disconnect(true, true);
            vTaskDelay(pdMS_TO_TICKS(200));
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.print("WiFi: connected, IP=");
                Serial.print(WiFi.localIP());
                Serial.print(", RSSI=");
                Serial.print(WiFi.RSSI());
                Serial.println(" dBm");
                digitalWrite(PIN_STATUS_LED, HIGH);
            } else {
                Serial.print("WiFi: connect timed out, status=");
                Serial.println(wifiStatusStr(WiFi.status()));
            }
        } else {
            digitalWrite(PIN_STATUS_LED, HIGH);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
