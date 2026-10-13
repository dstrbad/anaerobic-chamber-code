#include "heater.h"
#include "config.h"
#include "actuators.h"
#include <EEPROM.h>

static constexpr float CAT_HYSTERESIS = 1.0f;

// Katalizator: čisti ON/OFF (255/0), kako je testirano na komori. Pravi PWM
// na D44/D45 radi od prelaska Timer5 na Fast PWM (timers.cpp), pa se ovdje
// mogu vratiti međustupnjevi snage ako zatreba.

// --- Grijač komore: kaskadna regulacija --------------------------------------
//
// Potrebna T ploče grijača ovisi o T laboratorija, brtvama, punjenju komore…
// pa fiksni pomak iznad zadane T nikad ne odgovara. Zato dvije petlje:
//
//  Vanjska (PI, svakih CHM_OUTER_PERIOD_MS): greška T zraka u komori ->
//    ciljna T ploče = SP + offset + Kp*greška, ograničena na PLATE_MAX.
//    Integral (offset) uči koliko ploča mora biti toplija od SP. Dok je cilj
//    na granici, offset ne raste dalje (anti-windup).
//  Unutarnja (PI, svaka iteracija): greška T ploče -> PWM 0..255.
//
// Offset se pamti kao omjer offset / (SP - T_lab) u EEPROM-u (najviše jednom
// na sat, samo kad je komora blizu SP), pa nakon restarta ili promjene SP
// regulacija kreće od naučene vrijednosti. Početna procjena: omjer 1.0
// (mjereno: ploča 42 °C -> komora 31 °C uz lab 20 °C, tj. komora dobiva
// ~polovicu viška T ploče, pa ploča treba SP + (SP - 20)).
//
// Pojačanja su početne vrijednosti — podesiti na komori.

static constexpr float    CHM_LAB_C            = 20.0f;   // Tipična T laboratorija
static constexpr uint32_t CHM_OUTER_PERIOD_MS  = 10000;
static constexpr float    CHM_OUTER_KP         = 2.0f;    // °C ploče po °C greške komore
static constexpr float    CHM_OUTER_KI         = 0.0022f; // °C ploče po °C·s (Ti ≈ 15 min)
static constexpr float    CHM_OFFSET_MIN_C     = -5.0f;
static constexpr float    CHM_INNER_KP         = 40.0f;   // PWM po °C greške ploče
static constexpr float    CHM_INNER_KI         = 0.3f;    // PWM po °C·s (Ti ≈ 2 min)
static constexpr float    CHM_RATIO_DEFAULT    = 1.0f;
static constexpr float    CHM_RATIO_MAX        = 5.0f;
static constexpr uint32_t CHM_SAVE_PERIOD_MS   = 3600000; // EEPROM najviše 1x/h
static constexpr float    CHM_SAVE_NEAR_SP_C   = 1.0f;    // spremi samo kad je komora blizu SP
static constexpr uint32_t CHM_RESTART_GAP_MS   = 5000;    // pauza dulja od ovog = novo uključenje

static float    chm_offset        = NAN;   // naučeni pomak ploče iznad SP (°C)
static float    chm_offset_sp     = NAN;   // SP za koji vrijedi chm_offset
static float    chm_target        = 0.0f;  // trenutna ciljna T ploče (°C)
static float    chm_inner_i       = 0.0f;  // integral unutarnje petlje (PWM)
static uint32_t chm_last_call_ms  = 0;
static uint32_t chm_last_outer_ms = 0;
static uint32_t chm_last_inner_ms = 0;
static uint32_t chm_last_save_ms  = 0;
static bool     chm_at_limit      = false;

// Runtime setpointi — mijenjaju se iz UI/MQTT-a, traju preko EEPROM-a.
static float runtime_cat_setpoint = HEATER_CAT_SETPOINT_C;
static float runtime_chm_setpoint = HEATER_CHM_SETPOINT_C;

static float clampCat(float c) {
    if (c < HEATER_CAT_SETPOINT_MIN_C) return HEATER_CAT_SETPOINT_MIN_C;
    if (c > HEATER_CAT_SETPOINT_MAX_C) return HEATER_CAT_SETPOINT_MAX_C;
    return c;
}
static float clampChm(float c) {
    if (c < HEATER_CHM_SETPOINT_MIN_C) return HEATER_CHM_SETPOINT_MIN_C;
    if (c > HEATER_CHM_SETPOINT_MAX_C) return HEATER_CHM_SETPOINT_MAX_C;
    return c;
}

void heater_init() {
    heater_reloadCatalystSetpoint();
    heater_reloadChamberSetpoint();
}

float heater_getCatalystSetpoint() { return runtime_cat_setpoint; }

void heater_setCatalystSetpoint(float c) { runtime_cat_setpoint = clampCat(c); }

void heater_saveCatalystSetpoint() {
    EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
    EEPROM.update(EEPROM_ADDR_CAT_SP, (uint8_t)(runtime_cat_setpoint + 0.5f));
}

void heater_reloadCatalystSetpoint() {
    if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_VALUE) {
        runtime_cat_setpoint = clampCat((float)EEPROM.read(EEPROM_ADDR_CAT_SP));
    } else {
        runtime_cat_setpoint = HEATER_CAT_SETPOINT_C;
    }
}

float heater_getChamberSetpoint() { return runtime_chm_setpoint; }

void heater_setChamberSetpoint(float c) { runtime_chm_setpoint = clampChm(c); }

void heater_saveChamberSetpoint() {
    EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VALUE);
    EEPROM.update(EEPROM_ADDR_CHM_SP, (uint8_t)(runtime_chm_setpoint + 0.5f));
}

void heater_reloadChamberSetpoint() {
    if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC_VALUE) {
        runtime_chm_setpoint = clampChm((float)EEPROM.read(EEPROM_ADDR_CHM_SP));
    } else {
        runtime_chm_setpoint = HEATER_CHM_SETPOINT_C;
    }
}

void heater_tickCatalyst(const SensorData& sensors, SystemState& state) {
    if (!sensors.t_catalyst_valid) return;

    float setpoint = runtime_cat_setpoint;
    float t = sensors.t_catalyst;

    uint8_t pwm = state.catalyst_pwm;

    // Bang-bang s histerezom; unutar pojasa zadrži prethodno stanje
    if (t < (setpoint - CAT_HYSTERESIS)) {
        pwm = 255;
    } else if (t > (setpoint + CAT_HYSTERESIS)) {
        pwm = 0;
    }

    actuators_setCatalystPWM(pwm);
    state.catalyst_pwm = pwm;
}

// Raspon (SP - T_lab) s donjom granicom, da omjer ostane smislen za niske SP
static float chmSpan(float sp) {
    return max(sp - CHM_LAB_C, 1.0f);
}

static float chmLoadRatio() {
    float r;
    EEPROM.get(EEPROM_ADDR_CHM_RATIO, r);
    if (isnan(r) || r < 0.0f || r > CHM_RATIO_MAX) r = CHM_RATIO_DEFAULT;
    return r;
}

static void chmMaybeSave(uint32_t now, float sp, float t_chamber) {
    if (now - chm_last_save_ms < CHM_SAVE_PERIOD_MS) return;
    if (fabs(sp - t_chamber) > CHM_SAVE_NEAR_SP_C) return;  // nije ustaljeno
    chm_last_save_ms = now;
    float r = constrain(chm_offset / chmSpan(sp), 0.0f, CHM_RATIO_MAX);
    if (fabs(r - chmLoadRatio()) > 0.02f) {
        EEPROM.put(EEPROM_ADDR_CHM_RATIO, r);
    }
}

static float chmClampOffset(float offset, float sp) {
    return constrain(offset, CHM_OFFSET_MIN_C, HEATER_CHM_PLATE_MAX_C - sp);
}

void heater_tickChamber(const SensorData& sensors, SystemState& state) {
    // Bez senzora ploče nema regulacije; PWM ostaje, sigurnosni modul nakon
    // TEMP_INVALID_TIMEOUT_MS gasi grijač i diže TS_CHM (kao i prije).
    if (!sensors.t_chamber_heater_valid) return;

    const uint32_t now = millis();
    const float    sp  = runtime_chm_setpoint;

    // Prvo uključenje ili nastavak nakon pauze: kreni od naučenog omjera,
    // resetiraj unutarnju petlju i odmah izračunaj cilj
    const bool restart = (chm_last_call_ms == 0) || (now - chm_last_call_ms > CHM_RESTART_GAP_MS);
    chm_last_call_ms = now;
    if (isnan(chm_offset)) {
        chm_offset    = chmClampOffset(chmLoadRatio() * chmSpan(sp), sp);
        chm_offset_sp = sp;
    }
    if (sp != chm_offset_sp) {
        // Promjena SP: preračunaj naučeni pomak preko omjera
        float ratio   = chm_offset / chmSpan(chm_offset_sp);
        chm_offset    = chmClampOffset(ratio * chmSpan(sp), sp);
        chm_offset_sp = sp;
    }
    if (restart) {
        chm_inner_i       = 0.0f;
        chm_last_inner_ms = now;
        chm_last_outer_ms = now - CHM_OUTER_PERIOD_MS;
    }

    // --- Vanjska petlja: T komore -> ciljna T ploče ---
    if (now - chm_last_outer_ms >= CHM_OUTER_PERIOD_MS) {
        const float dt = (now - chm_last_outer_ms) / 1000.0f;
        chm_last_outer_ms = now;

        // Bez senzora zraka zadrži zadnji cilj (ograničen na PLATE_MAX)
        if (sensors.t_chamber_valid) {
            const float e   = sp - sensors.t_chamber;
            const float raw = sp + chm_offset + CHM_OUTER_KP * e;
            chm_at_limit = (raw >= HEATER_CHM_PLATE_MAX_C) && (e > 0);
            if (!chm_at_limit) {
                chm_offset = chmClampOffset(chm_offset + CHM_OUTER_KI * e * dt, sp);
            }
            chm_target = constrain(sp + chm_offset + CHM_OUTER_KP * e,
                                   0.0f, HEATER_CHM_PLATE_MAX_C);
            chmMaybeSave(now, sp, sensors.t_chamber);
        }
    }

    // --- Unutarnja petlja: T ploče -> PWM ---
    const float dt_i = (now - chm_last_inner_ms) / 1000.0f;
    chm_last_inner_ms = now;

    const float err = chm_target - sensors.t_chamber_heater;
    const float p   = CHM_INNER_KP * err;
    float out = p + chm_inner_i;
    if (!((out >= 255.0f && err > 0) || (out <= 0.0f && err < 0))) {
        chm_inner_i = constrain(chm_inner_i + CHM_INNER_KI * err * dt_i, 0.0f, 255.0f);
    }
    out = constrain(p + chm_inner_i, 0.0f, 255.0f);

    // Tvrda granica regulacije: na/iznad PLATE_MAX ploča se ne grije, bez
    // obzira na izlaz PI-ja (isklop i greška tek na HEATER_CHM_MAX_C)
    uint8_t pwm = (sensors.t_chamber_heater >= HEATER_CHM_PLATE_MAX_C)
                  ? 0 : (uint8_t)(out + 0.5f);

    actuators_setChamberPWM(pwm);
    state.chamber_pwm = pwm;
}

float heater_getChamberHeaterTarget() {
    // Kad grijač komore ne radi, cilj nije relevantan
    if (chm_last_call_ms == 0 || millis() - chm_last_call_ms > CHM_RESTART_GAP_MS) return 0.0f;
    return chm_target;
}

bool heater_isChamberAtLimit() {
    if (chm_last_call_ms == 0 || millis() - chm_last_call_ms > CHM_RESTART_GAP_MS) return false;
    return chm_at_limit;
}
