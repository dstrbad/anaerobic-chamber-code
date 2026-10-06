#include "heater.h"
#include "config.h"
#include "actuators.h"
#include <EEPROM.h>

// Jednostavna bang-bang regulacija s histerezom
static constexpr float HYSTERESIS = 1.0f; // +/- 1 C oko zadane vrijednosti
static constexpr float CHM_HEATER_BOOST_C = 3.0f; // grijač komore smije biti do +3 C iznad zadane T komore
static constexpr float CAT_HYSTERESIS = 1.0f;
static constexpr float CAT_SOFT_ZONE_C = 15.0f;   // start slowing 15 C before setpoint
static constexpr uint8_t CAT_PWM_FULL = 255;
static constexpr uint8_t CAT_PWM_SOFT = 140;     
static constexpr uint8_t CAT_PWM_HOLD = 80;  

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

    if (t < (setpoint - CAT_SOFT_ZONE_C)) {
        // Far from target: heat fast
        pwm = CAT_PWM_FULL;

    } else if (t < (setpoint - CAT_HYSTERESIS)) {
        // Close to target: slow down to reduce overshoot
        pwm = CAT_PWM_SOFT;

    } else if (t < setpoint) {
        // Very close: gentle heating / holding
        pwm = CAT_PWM_HOLD;

    } else if (t > (setpoint + CAT_HYSTERESIS)) {
        // Above upper band: off
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
