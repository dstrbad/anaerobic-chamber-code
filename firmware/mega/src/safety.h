#pragma once

#include "sensors.h"

// Kodovi grešaka — koristi Sloj 3, prijavljuje na UI/serijski
enum class FaultCode : uint8_t {
    NONE = 0,
    NO_PRESSURE_CHANGE,         // Nema promjene tlaka
    OVERPRESSURE,               // Pretlak
    OVERTEMP_CATALYST,          // Pregrijavanje katalizatora
    OVERTEMP_CHAMBER,           // Pregrijavanje komore
    TEMP_SENSOR_LOST_CATALYST,  // Izgubljen senzor katalizatora
    TEMP_SENSOR_LOST_CHAMBER,   // Izgubljen senzor komore
    O2_TOO_HIGH_FOR_BIG_PURGE,  // O2 previsok za veliki purge
    ISR_SAFETY_TRIGGERED,       // ISR sigurnosni isklop
    GLOBAL_ABORT,               // Globalni prekid
};

// Stanja operacijskog FSM-a
enum class OpsState : uint8_t {
    INIT,
    IDLE,
    PURGE_SMALL,
    PURGE_BIG,
    FAULT,
};

// Stanja termalnog FSM-a
enum class ThermalState : uint8_t {
    HEAT_OFF,
    HEAT_CATALYST,
    HEAT_CHAMBER,
    HEAT_BOTH,
    HEAT_FAULT,
};

// Stanja FSM-a korisničkog sučelja
enum class UIState : uint8_t {
    HOME,
    MENU_ROOT,
    MENU_PURGE,
    MENU_HEATING,
    MENU_AIRQUALITY,
    MENU_LOGGING,
};

// Stanje sustava — dijeli se među svim modulima
struct SystemState {
    OpsState     ops           = OpsState::INIT;
    ThermalState thermal       = ThermalState::HEAT_OFF;
    UIState      ui            = UIState::HOME;
    uint8_t      purge_cycle   = 0;
    uint8_t      purge_total   = 0;
    FaultCode    fault         = FaultCode::NONE;
    bool         pump_on       = false;
    bool         solenoid_on   = false;
    uint8_t      catalyst_pwm  = 0;
    uint8_t      chamber_pwm   = 0;
    uint32_t     uptime_s      = 0;
    bool         abort_requested = false;

    // UI navigacija
    uint8_t      menu_cursor     = 0;
    uint32_t     last_input_ms   = 0;

    // Aktivno logiranje — zadano UKLJUČENO, ostaje uključeno dok ga korisnik
    // ne isključi. Definira interval između CSV redaka (10s vs 1h kad je off).
    bool         logging_active  = true;

    // Korisnik trenutno uređuje neki setpoint — pokreće dedicated edit ekran.
    // editing_chamber: true = komora, false = katalizator (samo kad editing_setpoint=true)
    bool         editing_setpoint = false;
    bool         editing_chamber  = true;
};

void safety_init();
void safety_check(const SensorData& sensors, SystemState& state);
void safety_handleIsrFault(SystemState& state);
bool safety_isBigPurgeAllowed(const SensorData& sensors);

const char* faultCodeToString(FaultCode code);
const char* opsStateToString(OpsState state);
const char* thermalStateToString(ThermalState state);
