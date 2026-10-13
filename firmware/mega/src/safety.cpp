#include "safety.h"
#include "config.h"
#include "actuators.h"
#include "timers.h"
#include "purge.h"
#include <util/atomic.h>

// Granice pretlaka po komori za Timer5 ISR (piše safety_check svaku iteraciju)
volatile float cached_overp_limit_big   = OVERPRESSURE_ABS_HPA;
volatile float cached_overp_limit_small = OVERPRESSURE_ABS_HPA;

// Stanje watchdog-a za tlak (prozor: komora koja se pročišćava + druga komora)
static float    watchdog_p_snapshot     = 0.0f;
static float    watchdog_other_snapshot = 0.0f;
static uint32_t watchdog_start_ms       = 0;
static bool     watchdog_active         = false;

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

    // --- Provjera Timer5 (sigurnosni ISR + PWM grijača) ---
    // Ako je timer zaustavljen ili prekonfiguriran, pin grijača može ostati
    // zamrznut na HIGH, a sloj 2 više ne radi. allOff() odspaja PWM i spušta
    // pinove preko PORTL, što radi i bez timera. Greška se ponovno postavlja
    // svaki ciklus dok timer nije ispravan, pa potvrda tipkalom ne pomaže.
    if (!timers_timer5Ok()) {
        actuators_allOff();
        state.fault = FaultCode::TIMER5_FAULT;
        state.ops = OpsState::FAULT;
        state.thermal = ThermalState::HEAT_FAULT;
        return;
    }

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
    // Komora u purge-u: granica izračunata pri pokretanju purge-a (početni
    // tlak + 25, najviše apsolutna). Inače: apsolutna granica. Iste granice
    // se keširaju za Timer5 ISR (sloj 2).
    float limit_big   = (state.ops == OpsState::PURGE_BIG)
                        ? purge_getOverpressureLimit() : OVERPRESSURE_ABS_HPA;
    float limit_small = (state.ops == OpsState::PURGE_SMALL)
                        ? purge_getOverpressureLimit() : OVERPRESSURE_ABS_HPA;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        cached_overp_limit_big   = limit_big;
        cached_overp_limit_small = limit_small;
    }
    if (sensors.p_big_hpa > limit_big || sensors.p_small_hpa > limit_small) {
        actuators_solenoidOff();
        state.fault = FaultCode::OVERPRESSURE;
        state.ops = OpsState::FAULT;
        return;
    }

    // --- Gubitak očitanja tlaka ---
    // Neuspjelo čitanje zadržava zadnju vrijednost, pa bi provjera pretlaka
    // gledala zastarjeli broj. Dok pumpa ili solenoid rade, oba senzora moraju
    // davati ispravna očitanja.
    if (actuators_isPumpOn() || actuators_isSolenoidOn()) {
        uint32_t now = millis();
        if ((now - sensors.p_big_ok_ms)   > PRESSURE_INVALID_TIMEOUT_MS ||
            (now - sensors.p_small_ok_ms) > PRESSURE_INVALID_TIMEOUT_MS) {
            actuators_pumpOff();
            actuators_solenoidOff();
            state.fault = FaultCode::PRESSURE_SENSOR_LOST;
            state.ops = OpsState::FAULT;
            return;
        }
    }

    // --- Watchdog za odziv tlaka ---
    // Komora koja se pročišćava mora reagirati, druga mora mirovati. Pumpa i
    // solenoid rade samo tijekom purge-a; ako ops nije PURGE_SMALL, gleda se
    // velika komora (kao i prije).
    if (actuators_isPumpOn() || actuators_isSolenoidOn()) {
        const bool small = (state.ops == OpsState::PURGE_SMALL);
        const float    current_p = small ? sensors.p_small_hpa : sensors.p_big_hpa;
        const float    other_p   = small ? sensors.p_big_hpa   : sensors.p_small_hpa;
        const float    dp_min    = small ? WATCHDOG_SMALL_DP_MIN_HPA : WATCHDOG_BIG_DP_MIN_HPA;
        const uint32_t window_ms = small ? WATCHDOG_SMALL_WINDOW_MS  : WATCHDOG_BIG_WINDOW_MS;

        if (!watchdog_active) {
            // Počni praćenje
            watchdog_active = true;
            watchdog_start_ms = millis();
            watchdog_p_snapshot = current_p;
            watchdog_other_snapshot = other_p;
        } else {
            // Druga komora: provjera svaku iteraciju (krivi ventil s upaljenom
            // pumpom brzo mijenja tlak — ne čekamo kraj prozora)
            if (fabs(other_p - watchdog_other_snapshot) > WATCHDOG_OTHER_DP_MAX_HPA) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::CROSS_CHAMBER_CHANGE;
                state.ops = OpsState::FAULT;
                watchdog_active = false;
                return;
            }

            // Komora koja se pročišćava: na kraju prozora
            if ((millis() - watchdog_start_ms) > window_ms) {
                if (fabs(current_p - watchdog_p_snapshot) < dp_min) {
                    actuators_pumpOff();
                    actuators_solenoidOff();
                    state.fault = FaultCode::NO_PRESSURE_CHANGE;
                    state.ops = OpsState::FAULT;
                    watchdog_active = false;
                    return;
                }
                // Resetiraj watchdog za nastavak praćenja (obje komore)
                watchdog_start_ms = millis();
                watchdog_p_snapshot = current_p;
                watchdog_other_snapshot = other_p;
            }
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
        case FaultCode::TIMER5_FAULT:              return "TMR5";
        case FaultCode::PRESSURE_SENSOR_LOST:      return "PS_LOST";
        case FaultCode::CROSS_CHAMBER_CHANGE:      return "X_CHM";
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
