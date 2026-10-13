#include "serial_comm.h"
#include "config.h"
#include "heater.h"

// Definirano u main.cpp
extern uint8_t  reset_flags;
extern uint16_t boot_count;

// JSON okvir se slaže u statički buffer (bez String/JsonDocument na heapu
// svake sekunde — s ~4 KB slobodnog RAM-a heap se kroz dane fragmentira).
// Ključevi i broj decimala isti su kao prije; ESP32 i collector ih čitaju.

namespace {

char   frame[448];
size_t frame_len;
bool   frame_overflow;

void append(const char* s) {
    size_t n = strlen(s);
    if (frame_len + n >= sizeof(frame)) {
        frame_overflow = true;
        return;
    }
    memcpy(frame + frame_len, s, n);
    frame_len += n;
    frame[frame_len] = '\0';
}

void key(const char* k) {
    append(frame_len > 1 ? ",\"" : "\"");
    append(k);
    append("\":");
}

void addFloat(const char* k, float v, uint8_t decimals) {
    char buf[16];
    dtostrf(v, 1, decimals, buf);
    key(k);
    append(buf);
}

void addUint(const char* k, uint32_t v) {
    char buf[12];
    ultoa(v, buf, 10);
    key(k);
    append(buf);
}

void addStr(const char* k, const char* v) {
    key(k);
    append("\"");
    append(v);
    append("\"");
}

}  // namespace

// Šalje JSON okvir prema ESP32 preko Serial1 (1 Hz)
void serial_comm_send(const SensorData& sensors, const SystemState& state) {
    frame_len = 0;
    frame_overflow = false;
    append("{");

    // Plinski senzori
    addFloat("o2",  sensors.o2_pct, 2);
    addFloat("h2",  sensors.h2_ppm, 0);
    addFloat("h2s", sensors.h2s_ppm, 2);
    addFloat("o3",  sensors.o3_ppm, 2);
    addFloat("co2", sensors.eco2_ppm, 0);

    // Tlak
    addFloat("pb", sensors.p_big_hpa, 1);
    addFloat("ps", sensors.p_small_hpa, 1);

    // Temperatura
    addFloat("tc", sensors.t_catalyst, 1);
    addFloat("th", sensors.t_chamber_heater, 1);
    addFloat("ta", sensors.t_chamber, 1);

    // Setpointi grijača (oba korisnički podesiva, čuvaju se u EEPROM-u)
    addFloat("sp_cat", heater_getCatalystSetpoint(), 0);
    addFloat("sp_chm", heater_getChamberSetpoint(), 0);

    // Stanje
    addStr("st", opsStateToString(state.ops));
    addStr("ts", thermalStateToString(state.thermal));
    addStr("f",  faultCodeToString(state.fault));

    // Napredak pročišćavanja
    addUint("pc", state.purge_cycle);
    addUint("pt", state.purge_total);

    // Aktuatori
    addUint("pump", state.pump_on ? 1 : 0);
    addUint("sol",  state.solenoid_on ? 1 : 0);
    addUint("cpwm", state.catalyst_pwm);
    addUint("hpwm", state.chamber_pwm);

    // Razno
    addUint("wu",    sensors.warmup_remaining_s);
    addUint("up",    state.uptime_s);
    addUint("rst",   reset_flags);      // MCUSR zadnjeg reseta
    addUint("boots", boot_count);
    addUint("i2c",   i2c_error_count);  // I2C greške od pokretanja

    append("}");
    if (frame_overflow) return;  // nepotpun JSON ne šaljemo

#if SERIAL_DEBUG_TX
    Serial.print(F("TX: "));
    Serial.println(frame);
#endif

    // Send to ESP32
    Serial1.println(frame); // Razdjeljnik linije
}
