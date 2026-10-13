#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "wifi_manager.h"
#include "config.h"
#include "log.h"

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
    esp_task_wdt_add(nullptr);

    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep zna rušiti dugotrajnu MQTT vezu
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    for (;;) {
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED) {
            digitalWrite(PIN_STATUS_LED, LOW);
            logf("WiFi: not connected, status=%s\n", wifiStatusStr(WiFi.status()));

            // Potpuni reset radija prije ponovnog spajanja
            WiFi.disconnect(true, true);
            vTaskDelay(pdMS_TO_TICKS(200));
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (WiFi.status() == WL_CONNECTED) {
                logf("WiFi: connected, IP=%s, RSSI=%d dBm\n",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
                digitalWrite(PIN_STATUS_LED, HIGH);
            } else {
                logf("WiFi: connect timed out, status=%s\n", wifiStatusStr(WiFi.status()));
            }
        } else {
            digitalWrite(PIN_STATUS_LED, HIGH);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
