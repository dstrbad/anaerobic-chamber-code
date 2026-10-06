#include <Arduino.h>
#include <WiFi.h>
#include "wifi_manager.h"
#include "config.h"

void wifiTask(void* pvParameters) {
    pinMode(PIN_STATUS_LED, OUTPUT);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            digitalWrite(PIN_STATUS_LED, LOW);
            Serial.println("WiFi disconnected, reconnecting...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.print("WiFi connected: ");
                Serial.println(WiFi.localIP());
                digitalWrite(PIN_STATUS_LED, HIGH);
            }
        } else {
            digitalWrite(PIN_STATUS_LED, HIGH);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
