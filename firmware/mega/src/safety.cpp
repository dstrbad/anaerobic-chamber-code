#include "safety.h"
#include "config.h"
#include "actuators.h"

// Stanje watchdog-a za tlak
static float    watchdog_p_snapshot = 0.0f;
static uint32_t watchdog_start_ms   = 0;
static bool     watchdog_active     = false;

// Praćenje DS18B20 timeout-a
static uint32_t t_catalyst_invalid_since = 0;
static uint32_t t_chamber_h_invalid_since = 0;

void safety_init() {
    watchdog_active = false;
}

void safety_check(const SensorData& sensors, SystemState& state) {
    state.uptime_s = millis() / 1000;
    state.pump_on = actuators_isPumpOn();
    state.solenoid_on = actuators_isSolenoidOn();

    // --- Provjera globalnog prekida ---
    if (state.abort_requested) {
        state.abort_requested = false;
        if (state.ops == OpsState::PURGE_SMALL || state.ops == OpsState::PURGE_BIG) {
            // Tijekom purge-a: ugasi pumpu + solenoid, zadrži grijače
            actuators_pumpOff();
            actuators_solenoidOff();
        } else {
            actuators_allOff();
        }
        state.fault = FaultCode::GLOBAL_ABORT;
        state.ops = OpsState::FAULT;
        return;
    }

    // --- Provjera pretlaka ---
    float p_ref = 1013.25f; // Standardna atmosfera kao referenca
    if (sensors.p_big_hpa > (p_ref + OVERPRESSURE_CUTOFF_HPA) ||
        sensors.p_small_hpa > (p_ref + OVERPRESSURE_CUTOFF_HPA)) {
        actuators_solenoidOff();
        state.fault = FaultCode::OVERPRESSURE;
        state.ops = OpsState::FAULT;
        return;
    }

    // --- Watchdog za odziv tlaka ---
    if (actuators_isPumpOn() || actuators_isSolenoidOn()) {
        if (!watchdog_active) {
            // Počni praćenje
            watchdog_active = true;
            watchdog_start_ms = millis();
            watchdog_p_snapshot = (state.ops == OpsState::PURGE_SMALL)
                                 ? sensors.p_small_hpa
                                 : sensors.p_big_hpa;
        } else if ((millis() - watchdog_start_ms) > WATCHDOG_WINDOW_MS) {
            float current_p = (state.ops == OpsState::PURGE_SMALL)
                              ? sensors.p_small_hpa
                              : sensors.p_big_hpa;
            float dp = fabs(current_p - watchdog_p_snapshot);
            if (dp < WATCHDOG_DP_MIN_HPA) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::NO_PRESSURE_CHANGE;
                state.ops = OpsState::FAULT;
                watchdog_active = false;
                return;
            }
            // Resetiraj watchdog za nastavak praćenja
            watchdog_start_ms = millis();
            watchdog_p_snapshot = current_p;
        }
    } else {
        watchdog_active = false;
    }

    // --- Provjera previsoke temperature ---
    if (sensors.t_catalyst_valid && sensors.t_catalyst > HEATER_CAT_MAX_C) {
        actuators_catalystOff();
        state.fault = FaultCode::OVERTEMP_CATALYST;
        state.thermal = ThermalState::HEAT_FAULT;
    }
    if (sensors.t_chamber_heater_valid && sensors.t_chamber_heater > HEATER_CHM_MAX_C) {
        actuators_chamberOff();
        state.fault = FaultCode::OVERTEMP_CHAMBER;
        state.thermal = ThermalState::HEAT_FAULT;
    }

    // --- DS18B20 timeout (bitno samo ako je grijač aktivan) ---
    if (!sensors.t_catalyst_valid && state.catalyst_pwm > 0) {
        if (t_catalyst_invalid_since == 0) {
            t_catalyst_invalid_since = millis();
        } else if ((millis() - t_catalyst_invalid_since) > TEMP_INVALID_TIMEOUT_MS) {
            actuators_catalystOff();
            state.fault = FaultCode::TEMP_SENSOR_LOST_CATALYST;
            state.thermal = ThermalState::HEAT_FAULT;
        }
    } else {
        t_catalyst_invalid_since = 0;
    }

    if (!sensors.t_chamber_heater_valid && state.chamber_pwm > 0) {
        if (t_chamber_h_invalid_since == 0) {
            t_chamber_h_invalid_since = millis();
        } else if ((millis() - t_chamber_h_invalid_since) > TEMP_INVALID_TIMEOUT_MS) {
            actuators_chamberOff();
            state.fault = FaultCode::TEMP_SENSOR_LOST_CHAMBER;
            state.thermal = ThermalState::HEAT_FAULT;
        }
    } else {
        t_chamber_h_invalid_since = 0;
    }

    // Osvježi timeout aktuatora ako su namjerno aktivni
    if (actuators_isPumpOn())     actuators_refreshPumpTimeout();
    if (actuators_isSolenoidOn()) actuators_refreshSolenoidTimeout();
}

void safety_handleIsrFault(SystemState& state) {
    // ISR je već ugasio aktuatore preko direktnog pristupa portovima.
    // Postavi grešku u stanje da FSM pravilno reagira.
    if (state.fault == FaultCode::NONE) {
        state.fault = FaultCode::ISR_SAFETY_TRIGGERED;
    }
    state.ops = OpsState::FAULT;
    state.thermal = ThermalState::HEAT_FAULT;
}

bool safety_isBigPurgeAllowed(const SensorData& sensors) {
    return sensors.o2_pct <= O2_BIG_PURGE_MAX_PCT;
}

const char* faultCodeToString(FaultCode code) {
    switch (code) {
        case FaultCode::NONE:                      return "NONE";
        case FaultCode::NO_PRESSURE_CHANGE:        return "NO_DP";
        case FaultCode::OVERPRESSURE:              return "OVERP";
        case FaultCode::OVERTEMP_CATALYST:         return "OT_CAT";
        case FaultCode::OVERTEMP_CHAMBER:          return "OT_CHM";
        case FaultCode::TEMP_SENSOR_LOST_CATALYST: return "TS_CAT";
        case FaultCode::TEMP_SENSOR_LOST_CHAMBER:  return "TS_CHM";
        case FaultCode::O2_TOO_HIGH_FOR_BIG_PURGE: return "O2_HI";
        case FaultCode::ISR_SAFETY_TRIGGERED:      return "ISR";
        case FaultCode::GLOBAL_ABORT:              return "ABORT";
        default:                                   return "UNK";
    }
}

const char* opsStateToString(OpsState state) {
    switch (state) {
        case OpsState::INIT:        return "INIT";
        case OpsState::IDLE:        return "IDLE";
        case OpsState::PURGE_SMALL: return "PURGE_S";
        case OpsState::PURGE_BIG:   return "PURGE_B";
        case OpsState::FAULT:       return "FAULT";
        default:                    return "UNK";
    }
}

const char* thermalStateToString(ThermalState state) {
    switch (state) {
        case ThermalState::HEAT_OFF:      return "OFF";
        case ThermalState::HEAT_CATALYST: return "CAT";
        case ThermalState::HEAT_CHAMBER:  return "CHM";
        case ThermalState::HEAT_BOTH:     return "BOTH";
        case ThermalState::HEAT_FAULT:    return "FAULT";
        default:                          return "UNK";
    }
}
