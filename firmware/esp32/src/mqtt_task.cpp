#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "mqtt_task.h"
#include "serial_task.h"
#include "config.h"

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static void publishField(const char* subtopic, const char* value, bool retain = true) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s%s", MQTT_PREFIX, subtopic);
    mqtt.publish(topic, value, retain);
}

// Mega šalje senzorske vrijednosti kao JSON brojeve (serialized()), ne
// stringove, pa is<const char*>() nikad nije true za njih.
static void publishNumber(const char* subtopic, JsonVariantConst v) {
    if (v.isNull()) return;

    char buf[32];
    if (v.is<float>() || v.is<double>()) {
        snprintf(buf, sizeof(buf), "%.2f", v.as<float>());
    } else {
        snprintf(buf, sizeof(buf), "%ld", v.as<long>());
    }
    publishField(subtopic, buf);
}

static void publishAll(const char* json) {
    // Publish bulk topic
    char topic[64];
    snprintf(topic, sizeof(topic), "%sbulk", MQTT_PREFIX);
    mqtt.publish(topic, json, true);

    // Parse and publish individual topics
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return;

    char buf[32];

    publishNumber("sensor/o2", doc["o2"]);
    publishNumber("sensor/h2", doc["h2"]);
    publishNumber("sensor/h2s", doc["h2s"]);
    publishNumber("sensor/o3", doc["o3"]);
    publishNumber("sensor/co2", doc["co2"]);
    publishNumber("sensor/pressure_big", doc["pb"]);
    publishNumber("sensor/pressure_small", doc["ps"]);
    publishNumber("sensor/temp_catalyst", doc["tc"]);
    publishNumber("sensor/temp_heater", doc["th"]);
    publishNumber("sensor/temp_chamber", doc["ta"]);

    if (doc["sp_cat"].is<const char*>()) publishField("setpoint/catalyst", doc["sp_cat"]);
    if (doc["sp_chm"].is<const char*>()) publishField("setpoint/chamber",  doc["sp_chm"]);

    if (doc["st"].is<const char*>())  publishField("status/state", doc["st"]);
    if (doc["ts"].is<const char*>())  publishField("status/thermal", doc["ts"]);
    if (doc["f"].is<const char*>())   publishField("status/fault", doc["f"]);

    snprintf(buf, sizeof(buf), "%u", doc["up"].as<uint32_t>());
    publishField("status/uptime", buf);

    snprintf(buf, sizeof(buf), "%u", doc["wu"].as<uint16_t>());
    publishField("status/warmup", buf);

    // Purge progress as JSON
    snprintf(buf, sizeof(buf), "{\"current\":%u,\"total\":%u}",
             doc["pc"].as<uint8_t>(), doc["pt"].as<uint8_t>());
    publishField("status/purge_progress", buf);

    snprintf(buf, sizeof(buf), "%u", doc["pump"].as<uint8_t>());
    publishField("actuator/pump", buf);
    snprintf(buf, sizeof(buf), "%u", doc["sol"].as<uint8_t>());
    publishField("actuator/solenoid", buf);
    snprintf(buf, sizeof(buf), "%u", doc["cpwm"].as<uint8_t>());
    publishField("actuator/catalyst_pwm", buf);
    snprintf(buf, sizeof(buf), "%u", doc["hpwm"].as<uint8_t>());
    publishField("actuator/chamber_pwm", buf);
}

static void reconnectMqtt() {
    while (!mqtt.connected()) {
        Serial.print("MQTT connecting...");
        if (mqtt.connect(MQTT_CLIENT_ID)) {
            Serial.println("connected");
            // Subscribe to command topics
            char topic[64];
            snprintf(topic, sizeof(topic), "%scommand/#", MQTT_PREFIX);
            mqtt.subscribe(topic);
        } else {
            Serial.print("failed, rc=");
            Serial.println(mqtt.state());
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

// Forwarding logic for incoming MQTT commands. We do *not* try to validate
// JSON here — the Mega has the authoritative parser and validators. Our job
// is to be a faithful pipe with one safety check: bound the line length so
// a malicious / corrupt message can't blow the Mega's 256-byte line buffer.
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    if (length == 0 || length > 240) return;
    if (payload[0] != '{') return;  // basic sanity — must look like JSON object

    // Copy into a stack buffer so we can null-terminate without mutating the
    // library's reused buffer, and so we can guarantee a single trailing newline.
    char buf[256];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    // Isti UART koji serial_task otvara prema Megi (RX=0/TX=1)
    Serial1.print(buf);
    Serial1.print('\n');
}

void mqttTask(void* pvParameters) {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);

    for (;;) {
        if (!mqtt.connected()) {
            reconnectMqtt();
        }
        mqtt.loop();

        // Wait for new data from serial task
        if (xSemaphoreTake(newDataSemaphore, pdMS_TO_TICKS(5000)) == pdTRUE) {
            char localBuf[512];
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                strncpy(localBuf, sharedJsonBuffer, sizeof(localBuf));
                xSemaphoreGive(dataMutex);
            }
            publishAll(localBuf);
        }
    }
}
