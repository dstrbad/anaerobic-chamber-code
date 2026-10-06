#include "purge.h"
#include "config.h"
#include "actuators.h"

// Faze unutar ciklusa pročišćavanja
enum class PurgePhase : uint8_t {
    IDLE,
    EVACUATING,           // Izvlačenje zraka
    STABILIZE_POST_EVAC,  // Stabilizacija nakon evakuacije
    REFILLING,            // Punjenje plinom
    STABILIZE_POST_REFILL,// Stabilizacija nakon punjenja
    DONE,
};

static PurgePhase phase       = PurgePhase::IDLE;
static uint8_t    cycle       = 0;
static uint8_t    totalCycles = 0;
static float      targetP     = 0.0f;
static float      refillP     = 0.0f;
static uint32_t   phaseStart  = 0;
static uint32_t   timeout_ms  = 0;
static bool       isBigPurge  = false;

static void startEvacuation() {
    phase = PurgePhase::EVACUATING;
    phaseStart = millis();
    actuators_pumpOn();
}

static void startRefill() {
    phase = PurgePhase::REFILLING;
    phaseStart = millis();
    actuators_solenoidOn();
    // TODO: zujalica na prijelazu
}

void purge_startSmall(const SensorData& sensors, SystemState& state) {
    isBigPurge = false;
    totalCycles = PURGE_SMALL_CYCLES;
    cycle = 0;
    timeout_ms = PURGE_SMALL_TIMEOUT_MS;

    float startP = sensors.p_small_hpa;
    targetP = max(startP - PURGE_SMALL_DP_HPA, PURGE_SMALL_FLOOR_HPA);
    // Cilj punjenja: max od P_big, 1 atm (1013.25 hPa), ili početni tlak
    refillP = max(max(sensors.p_big_hpa, 1013.25f), startP) - 15.0f; // Blagi pad za sigurnost

    state.purge_cycle = 0;
    state.purge_total = totalCycles;
    state.ops = OpsState::PURGE_SMALL;

    startEvacuation();
}

void purge_startBig(const SensorData& sensors, SystemState& state) {
    isBigPurge = true;
    totalCycles = PURGE_BIG_CYCLES;
    cycle = 0;
    timeout_ms = PURGE_BIG_TIMEOUT_MS;

    float startP = sensors.p_big_hpa;
    targetP = max(startP - PURGE_BIG_DP_HPA, PURGE_BIG_FLOOR_HPA);
    refillP = startP;

    state.purge_cycle = 0;
    state.purge_total = totalCycles;
    state.ops = OpsState::PURGE_BIG;

    startEvacuation();
}

void purge_tickSmall(const SensorData& sensors, SystemState& state) {
    float p = sensors.p_small_hpa;
    uint32_t elapsed = millis() - phaseStart;

    switch (phase) {
        case PurgePhase::EVACUATING:
            if (p <= targetP) {
                actuators_pumpOff();
                phase = PurgePhase::STABILIZE_POST_EVAC;
                phaseStart = millis();
                // TODO: zujalica
            } else if (elapsed > timeout_ms) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::NO_PRESSURE_CHANGE;
                state.ops = OpsState::FAULT;
                phase = PurgePhase::IDLE;
            }
            break;

        case PurgePhase::STABILIZE_POST_EVAC:
            if ((millis() - phaseStart) > 500) {
                startRefill();
            }
            break;

        case PurgePhase::REFILLING:
            if (p >= refillP) {
                actuators_solenoidOff();
                phase = PurgePhase::STABILIZE_POST_REFILL;
                phaseStart = millis();
                // TODO: zujalica
            } else if (elapsed > timeout_ms) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::NO_PRESSURE_CHANGE;
                state.ops = OpsState::FAULT;
                phase = PurgePhase::IDLE;
            }
            break;

        case PurgePhase::STABILIZE_POST_REFILL:
            if ((millis() - phaseStart) > 500) {
                cycle++;
                state.purge_cycle = cycle;
                if (cycle >= totalCycles) {
                    phase = PurgePhase::DONE;
                    // TODO: zujalica završetka
                } else {
                    // Preračunaj cilj za sljedeći ciklus
                    float startP = sensors.p_small_hpa;
                    targetP = max(startP - PURGE_SMALL_DP_HPA, PURGE_SMALL_FLOOR_HPA);
                    startEvacuation();
                }
            }
            break;

        default:
            break;
    }
}

void purge_tickBig(const SensorData& sensors, SystemState& state) {
    float p = sensors.p_big_hpa;
    uint32_t elapsed = millis() - phaseStart;

    switch (phase) {
        case PurgePhase::EVACUATING:
            if (p <= targetP) {
                actuators_pumpOff();
                phase = PurgePhase::STABILIZE_POST_EVAC;
                phaseStart = millis();
            } else if (elapsed > timeout_ms) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::NO_PRESSURE_CHANGE;
                state.ops = OpsState::FAULT;
                phase = PurgePhase::IDLE;
            }
            break;

        case PurgePhase::STABILIZE_POST_EVAC:
            if ((millis() - phaseStart) > 500) {
                startRefill();
            }
            break;

        case PurgePhase::REFILLING:
            if (p >= refillP) {
                actuators_solenoidOff();
                phase = PurgePhase::STABILIZE_POST_REFILL;
                phaseStart = millis();
            } else if (elapsed > timeout_ms) {
                actuators_pumpOff();
                actuators_solenoidOff();
                state.fault = FaultCode::NO_PRESSURE_CHANGE;
                state.ops = OpsState::FAULT;
                phase = PurgePhase::IDLE;
            }
            break;

        case PurgePhase::STABILIZE_POST_REFILL:
            if ((millis() - phaseStart) > 500) {
                cycle++;
                state.purge_cycle = cycle;
                if (cycle >= totalCycles) {
                    phase = PurgePhase::DONE;
                } else {
                    float startP = sensors.p_big_hpa;
                    targetP = max(startP - PURGE_BIG_DP_HPA, PURGE_BIG_FLOOR_HPA);
                    startEvacuation();
                }
            }
            break;

        default:
            break;
    }
}

bool purge_isDone() {
    return phase == PurgePhase::DONE;
}
