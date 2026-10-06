#include "serial_rx.h"
#include "config.h"
#include "heater.h"
#include "purge.h"
#include "actuators.h"
#include <ArduinoJson.h>

// Komandni protokol — line-delimited JSON na Serial1. Parser obrađuje jednu
// liniju po pozivu (čak i ako je pristiglo više linija u bufferu Serial1-a,
// glavna petlja će ih obraditi sljedeće iteracije).
//
// Podržane komande (svaka mora imati polje "cmd"):
//   {"cmd":"set_chm_sp","v":38}              setpoint komore (clamp 25..43)
//   {"cmd":"set_cat_sp","v":52}              setpoint katalizatora (clamp 40..58)
//   {"cmd":"heater_cat","v":true}            katalizator ON/OFF
//   {"cmd":"heater_chm","v":true}            chamber heater ON/OFF
//   {"cmd":"log","v":true}                   active logging ON/OFF
//   {"cmd":"purge","v":"small"|"big"}        pokreni purge (samo IDLE; big traži O2 < 3%)
//   {"cmd":"abort"}                          globalni prekid (isto kao dugi pritisak)
//   {"cmd":"apply","fields":{...}}           bulk: chm_sp, cat_sp, heater_cat, heater_chm, log
//
// Bulk apply NE pokreće purge — purge je uvijek eksplicitna operatorska radnja.

namespace {

constexpr uint16_t LINE_BUFFER_SIZE = 256;

char     line_buffer[LINE_BUFFER_SIZE];
uint16_t line_len = 0;

bool current_cat(const SystemState& s) {
    return s.thermal == ThermalState::HEAT_CATALYST || s.thermal == ThermalState::HEAT_BOTH;
}
bool current_chm(const SystemState& s) {
    return s.thermal == ThermalState::HEAT_CHAMBER || s.thermal == ThermalState::HEAT_BOTH;
}

// Postavlja thermal stanje iz željene kombinacije (cat, chm). NIKAD ne dira
// HEAT_FAULT — termalna greška se mora ručno potvrditi na OLED-u.
void apply_heater_state(SystemState& state, bool cat, bool chm) {
    if (state.thermal == ThermalState::HEAT_FAULT) return;
    if (cat && chm)       state.thermal = ThermalState::HEAT_BOTH;
    else if (cat)         state.thermal = ThermalState::HEAT_CATALYST;
    else if (chm)         state.thermal = ThermalState::HEAT_CHAMBER;
    else                  state.thermal = ThermalState::HEAT_OFF;
}

void handle_set_chm_sp(JsonDocument& doc) {
    if (doc["v"].is<float>() || doc["v"].is<int>()) {
        heater_setChamberSetpoint(doc["v"].as<float>());
        heater_saveChamberSetpoint();
    }
}

void handle_set_cat_sp(JsonDocument& doc) {
    if (doc["v"].is<float>() || doc["v"].is<int>()) {
        heater_setCatalystSetpoint(doc["v"].as<float>());
        heater_saveCatalystSetpoint();
    }
}

void handle_heater_cat(JsonDocument& doc, SystemState& state) {
    if (!doc["v"].is<bool>()) return;
    apply_heater_state(state, doc["v"].as<bool>(), current_chm(state));
}

void handle_heater_chm(JsonDocument& doc, SystemState& state) {
    if (!doc["v"].is<bool>()) return;
    apply_heater_state(state, current_cat(state), doc["v"].as<bool>());
}

void handle_log(JsonDocument& doc, SystemState& state) {
    if (!doc["v"].is<bool>()) return;
    state.logging_active = doc["v"].as<bool>();
}

void handle_purge(JsonDocument& doc, const SensorData& sensors, SystemState& state) {
    const char* kind = doc["v"];
    if (!kind) return;
    if (state.ops != OpsState::IDLE) return;  // Samo iz IDLE
    if (strcmp(kind, "small") == 0) {
        purge_startSmall(sensors, state);
        state.ops = OpsState::PURGE_SMALL;
    } else if (strcmp(kind, "big") == 0 && safety_isBigPurgeAllowed(sensors)) {
        purge_startBig(sensors, state);
        state.ops = OpsState::PURGE_BIG;
    }
    // Inače: tiha odbijenica. Web UI vidi neažurirano stanje pri sljedećem broadcast-u.
}

void handle_apply(JsonDocument& doc, SystemState& state) {
    JsonObject fields = doc["fields"].as<JsonObject>();
    if (fields.isNull()) return;

    if (fields["chm_sp"].is<float>() || fields["chm_sp"].is<int>()) {
        heater_setChamberSetpoint(fields["chm_sp"].as<float>());
        heater_saveChamberSetpoint();
    }
    if (fields["cat_sp"].is<float>() || fields["cat_sp"].is<int>()) {
        heater_setCatalystSetpoint(fields["cat_sp"].as<float>());
        heater_saveCatalystSetpoint();
    }

    bool cat = fields["heater_cat"].is<bool>() ? fields["heater_cat"].as<bool>() : current_cat(state);
    bool chm = fields["heater_chm"].is<bool>() ? fields["heater_chm"].as<bool>() : current_chm(state);
    apply_heater_state(state, cat, chm);

    if (fields["log"].is<bool>()) {
        state.logging_active = fields["log"].as<bool>();
    }
}

void dispatch(JsonDocument& doc, const SensorData& sensors, SystemState& state) {
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if      (strcmp(cmd, "set_chm_sp") == 0) handle_set_chm_sp(doc);
    else if (strcmp(cmd, "set_cat_sp") == 0) handle_set_cat_sp(doc);
    else if (strcmp(cmd, "heater_cat") == 0) handle_heater_cat(doc, state);
    else if (strcmp(cmd, "heater_chm") == 0) handle_heater_chm(doc, state);
    else if (strcmp(cmd, "log")        == 0) handle_log(doc, state);
    else if (strcmp(cmd, "purge")      == 0) handle_purge(doc, sensors, state);
    else if (strcmp(cmd, "abort")      == 0) state.abort_requested = true;
    else if (strcmp(cmd, "apply")      == 0) handle_apply(doc, state);
    // Nepoznate komande tiho ignoriramo.
}

void process_line(const SensorData& sensors, SystemState& state) {
    line_buffer[line_len] = '\0';
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line_buffer);
    if (!err) {
        dispatch(doc, sensors, state);
    }
    line_len = 0;
}

}  // namespace

void serial_rx_tick(const SensorData& sensors, SystemState& state) {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (line_len > 0) process_line(sensors, state);
            continue;
        }
        if (line_len < LINE_BUFFER_SIZE - 1) {
            line_buffer[line_len++] = c;
        } else {
            // Prelijevanje — odbaci liniju i čekaj sljedeći novi red
            line_len = 0;
        }
    }
}
