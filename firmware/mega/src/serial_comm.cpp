#include "serial_comm.h"
#include "config.h"
#include "heater.h"
#include <ArduinoJson.h>

// Šalje JSON okvir prema ESP32 preko Serial1 (1 Hz)
void serial_comm_send(const SensorData& sensors, const SystemState& state) {
    JsonDocument doc;

    // Plinski senzori
    doc["o2"]  = serialized(String(sensors.o2_pct, 2));
    doc["h2"]  = serialized(String(sensors.h2_ppm, 0));
    doc["h2s"] = serialized(String(sensors.h2s_ppm, 2));
    doc["o3"]  = serialized(String(sensors.o3_ppm, 2));
    doc["co2"] = serialized(String(sensors.eco2_ppm, 0));

    // Tlak
    doc["pb"] = serialized(String(sensors.p_big_hpa, 1));
    doc["ps"] = serialized(String(sensors.p_small_hpa, 1));

    // Temperatura
    doc["tc"] = serialized(String(sensors.t_catalyst, 1));
    doc["th"] = serialized(String(sensors.t_chamber_heater, 1));
    doc["ta"] = serialized(String(sensors.t_chamber, 1));

    // Setpointi grijača (oba korisnički podesiva, čuvaju se u EEPROM-u)
    doc["sp_cat"] = serialized(String(heater_getCatalystSetpoint(), 0));
    doc["sp_chm"] = serialized(String(heater_getChamberSetpoint(), 0));

    // Stanje
    doc["st"] = opsStateToString(state.ops);
    doc["ts"] = thermalStateToString(state.thermal);
    doc["f"]  = faultCodeToString(state.fault);

    // Napredak pročišćavanja
    doc["pc"] = state.purge_cycle;
    doc["pt"] = state.purge_total;

    // Aktuatori
    doc["pump"] = state.pump_on ? 1 : 0;
    doc["sol"]  = state.solenoid_on ? 1 : 0;
    doc["cpwm"] = state.catalyst_pwm;
    doc["hpwm"] = state.chamber_pwm;

    // Razno
    doc["wu"] = sensors.warmup_remaining_s;
    doc["up"] = state.uptime_s;

    // Debug: print to USB serial
    Serial.print("TX: ");
    serializeJson(doc, Serial);
    Serial.println();

    // Send to ESP32
    serializeJson(doc, Serial1);
    Serial1.println(); // Razdjeljnik linije
}
