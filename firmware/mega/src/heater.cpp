#include "heater.h"
#include "config.h"
#include "actuators.h"
#include <EEPROM.h>

// Jednostavna bang-bang regulacija s histerezom
static constexpr float HYSTERESIS = 1.0f; // +/- 1 C oko zadane vrijednosti
static constexpr float CHM_HEATER_BOOST_C = 3.0f; // grijač komore smije biti do +3 C iznad zadane T komore
static constexpr float CAT_HYSTERESIS = 1.0f;

// Katalizator: čisti ON/OFF (255/0), kako je testirano na komori. Pravi PWM
// na D44/D45 radi od prelaska Timer5 na Fast PWM (timers.cpp), pa se ovdje
// mogu vratiti međustupnjevi snage ako zatreba.

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

void heater_tickChamber(const SensorData& sensors, SystemState& state) {
    if (!sensors.t_chamber_valid || !sensors.t_chamber_heater_valid) return;

    float setpoint = runtime_chm_setpoint;
    float heater_setpoint = setpoint + CHM_HEATER_BOOST_C;

    // --- Outer loop: treba li grijati komoru? ---
    bool demand_heat = (sensors.t_chamber < (setpoint - HYSTERESIS));

    if (!demand_heat) {
        // Komora je dovoljno topla → ugasi grijač
        actuators_setChamberPWM(0);
        state.chamber_pwm = 0;
        return;
    }

    // --- Inner loop: regulacija temperature grijača ---
    static bool heater_on = false;

    if (sensors.t_chamber_heater < (heater_setpoint - HYSTERESIS)) {
        heater_on = true;
    } else if (sensors.t_chamber_heater > (heater_setpoint + HYSTERESIS)) {
        heater_on = false;
    }

    uint8_t pwm = heater_on ? 255 : 0;
    actuators_setChamberPWM(pwm);
    state.chamber_pwm = pwm;
}
