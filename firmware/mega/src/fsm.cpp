#include "fsm.h"
#include "config.h"
#include "actuators.h"
#include "purge.h"
#include "heater.h"
#include "input.h"

void fsm_init() {
    // Operacijski FSM počinje u INIT, prelazi u IDLE kad su senzori spremni
}

void fsm_tickOperations(const SensorData& sensors, SystemState& state) {
    switch (state.ops) {
        case OpsState::INIT:
            // TODO: Provjeri da su svi kritični senzori dali barem jedno očitanje
            state.ops = OpsState::IDLE;
            break;

        case OpsState::IDLE:
            // Čekanje korisničkog unosa (meni pokreće purge)
            break;

        case OpsState::PURGE_SMALL:
            purge_tickSmall(sensors, state);
            if (purge_isDone()) {
                state.ops = OpsState::IDLE;
            }
            break;

        case OpsState::PURGE_BIG:
            purge_tickBig(sensors, state);
            if (purge_isDone()) {
                state.ops = OpsState::IDLE;
            }
            break;

        case OpsState::FAULT:
            // Aktuatori već ugašeni od sigurnosnog modula.
            // Ostani ovdje dok korisnik ne potvrdi (pritisak tipkala).
            if (input_buttonPressed()) {
                state.fault = FaultCode::NONE;
                state.ops = OpsState::IDLE;
            }
            break;
    }
}

void fsm_tickThermal(const SensorData& sensors, SystemState& state) {
    switch (state.thermal) {
        case ThermalState::HEAT_OFF:
            actuators_catalystOff();
            actuators_chamberOff();
            state.catalyst_pwm = 0;
            state.chamber_pwm = 0;
            break;

        case ThermalState::HEAT_CATALYST:
            actuators_chamberOff();
            state.chamber_pwm = 0;
            heater_tickCatalyst(sensors, state);
            break;

        case ThermalState::HEAT_CHAMBER:
            actuators_catalystOff();
            state.catalyst_pwm = 0;
            heater_tickChamber(sensors, state);
            break;

        case ThermalState::HEAT_BOTH:
            heater_tickCatalyst(sensors, state);
            heater_tickChamber(sensors, state);
            break;

        case ThermalState::HEAT_FAULT:
            actuators_catalystOff();
            actuators_chamberOff();
            state.catalyst_pwm = 0;
            state.chamber_pwm = 0;
            if (input_buttonPressed()) {
                state.fault = FaultCode::NONE;
                state.thermal = ThermalState::HEAT_OFF;
            }
            break;
    }
}

// Broj stavki u pojedinom meniju
static constexpr uint8_t MENU_ROOT_ITEMS  = 4;  // Purge, Heating, Air quality, Logging
static constexpr uint8_t MENU_PURGE_ITEMS = 3;  // Small, Big, Nazad
static constexpr uint8_t MENU_HEAT_ITEMS  = 5;  // Kat ON/OFF, Kom ON/OFF, SP kat, SP kom, Nazad
static constexpr uint8_t MENU_AQ_ITEMS    = 1;  // Nazad (ekran samo prikazuje podatke)
static constexpr uint8_t MENU_LOG_ITEMS   = 2;  // Aktivno log ON/OFF, Nazad

static uint8_t menuItemCount(UIState ui) {
    switch (ui) {
        case UIState::MENU_ROOT:       return MENU_ROOT_ITEMS;
        case UIState::MENU_PURGE:      return MENU_PURGE_ITEMS;
        case UIState::MENU_HEATING:    return MENU_HEAT_ITEMS;
        case UIState::MENU_AIRQUALITY: return MENU_AQ_ITEMS;
        case UIState::MENU_LOGGING:    return MENU_LOG_ITEMS;
        default:                       return 0;
    }
}

static void enterMenu(UIState target, SystemState& state) {
    state.ui = target;
    state.menu_cursor = 0;
    state.last_input_ms = millis();
}

void fsm_tickUI(const SensorData& sensors, SystemState& state) {
    int8_t jy = input_joystickY();
    bool   btn = input_buttonPressed();
    bool   longPress = input_longPress();

    // --- Globalni prekid (dugi pritisak) — dostupan uvijek ---
    if (longPress) {
        // Iznimka: u edit modu setpointa, dugi pritisak = poništi (vrati spremljenu vrijednost)
        if (state.editing_setpoint) {
            if (state.editing_chamber) heater_reloadChamberSetpoint();
            else                       heater_reloadCatalystSetpoint();
            state.editing_setpoint = false;
            state.last_input_ms = millis();
            return;
        }
        // Prekini purge ako je aktivan (grijač ostaje)
        if (state.ops == OpsState::PURGE_SMALL || state.ops == OpsState::PURGE_BIG) {
            actuators_pumpOff();
            actuators_solenoidOff();
            state.ops = OpsState::IDLE;
        } else {
            // Potpuni abort — ugasi sve
            state.abort_requested = true;
        }
        enterMenu(UIState::HOME, state);
        return;
    }

    // --- Ažuriranje aktivnosti joysticka/tipkala ---
    bool anyInput = btn || (jy != 0);
    if (anyInput) {
        state.last_input_ms = millis();
    }

    // --- Timeout neaktivnosti — povratak na HOME ---
    if (state.ui != UIState::HOME) {
        if ((millis() - state.last_input_ms) >= MENU_TIMEOUT_MS) {
            // Ako je bio u edit modu, poništi promjene
            if (state.editing_setpoint) {
                if (state.editing_chamber) heater_reloadChamberSetpoint();
                else                       heater_reloadCatalystSetpoint();
                state.editing_setpoint = false;
            }
            enterMenu(UIState::HOME, state);
            return;
        }
    }

    switch (state.ui) {
        case UIState::HOME:
            // Pritisak tipkala otvara glavni meni
            if (btn) {
                enterMenu(UIState::MENU_ROOT, state);
            }
            break;

        case UIState::MENU_ROOT:
            // Joystick gore/dolje — pomicanje kursora
            if (jy != 0) {
                uint8_t n = menuItemCount(state.ui);
                state.menu_cursor = (state.menu_cursor + n + jy) % n;
            }
            // Tipkalo — potvrda izbora
            if (btn) {
                switch (state.menu_cursor) {
                    case 0: enterMenu(UIState::MENU_PURGE, state); break;
                    case 1: enterMenu(UIState::MENU_HEATING, state); break;
                    case 2: enterMenu(UIState::MENU_AIRQUALITY, state); break;
                    case 3: enterMenu(UIState::MENU_LOGGING, state); break;
                }
            }
            break;

        case UIState::MENU_PURGE:
            if (jy != 0) {
                uint8_t n = menuItemCount(state.ui);
                state.menu_cursor = (state.menu_cursor + n + jy) % n;
            }
            if (btn) {
                switch (state.menu_cursor) {
                    case 0: // Pokreni mali purge
                        if (state.ops == OpsState::IDLE) {
                            purge_startSmall(sensors, state);
                            enterMenu(UIState::HOME, state);
                        }
                        break;
                    case 1: // Pokreni veliki purge
                        if (state.ops == OpsState::IDLE && safety_isBigPurgeAllowed(sensors)) {
                            purge_startBig(sensors, state);
                            enterMenu(UIState::HOME, state);
                        }
                        break;
                    case 2: // Nazad
                        enterMenu(UIState::MENU_ROOT, state);
                        break;
                }
            }
            break;

        case UIState::MENU_HEATING:
            if (state.editing_setpoint) {
                // U edit modu joystick mijenja odabrani setpoint (1°C koraci),
                // tipkalo potvrđuje i sprema u EEPROM.
                if (jy != 0) {
                    if (state.editing_chamber) {
                        heater_setChamberSetpoint(heater_getChamberSetpoint() + (float)jy);
                    } else {
                        heater_setCatalystSetpoint(heater_getCatalystSetpoint() + (float)jy);
                    }
                }
                if (btn) {
                    if (state.editing_chamber) heater_saveChamberSetpoint();
                    else                       heater_saveCatalystSetpoint();
                    state.editing_setpoint = false;
                }
                break;
            }
            if (jy != 0) {
                uint8_t n = menuItemCount(state.ui);
                state.menu_cursor = (state.menu_cursor + n + jy) % n;
            }
            if (btn) {
                switch (state.menu_cursor) {
                    case 0: // Katalizator ON/OFF
                        if (state.thermal == ThermalState::HEAT_OFF) {
                            state.thermal = ThermalState::HEAT_CATALYST;
                        } else if (state.thermal == ThermalState::HEAT_CATALYST) {
                            state.thermal = ThermalState::HEAT_OFF;
                        } else if (state.thermal == ThermalState::HEAT_CHAMBER) {
                            state.thermal = ThermalState::HEAT_BOTH;
                        } else if (state.thermal == ThermalState::HEAT_BOTH) {
                            state.thermal = ThermalState::HEAT_CHAMBER;
                        }
                        break;
                    case 1: // Komora ON/OFF
                        if (state.thermal == ThermalState::HEAT_OFF) {
                            state.thermal = ThermalState::HEAT_CHAMBER;
                        } else if (state.thermal == ThermalState::HEAT_CHAMBER) {
                            state.thermal = ThermalState::HEAT_OFF;
                        } else if (state.thermal == ThermalState::HEAT_CATALYST) {
                            state.thermal = ThermalState::HEAT_BOTH;
                        } else if (state.thermal == ThermalState::HEAT_BOTH) {
                            state.thermal = ThermalState::HEAT_CATALYST;
                        }
                        break;
                    case 2: // Postavi SP katalizatora
                        state.editing_setpoint = true;
                        state.editing_chamber  = false;
                        break;
                    case 3: // Postavi SP komore
                        state.editing_setpoint = true;
                        state.editing_chamber  = true;
                        break;
                    case 4: // Nazad
                        enterMenu(UIState::MENU_ROOT, state);
                        break;
                }
            }
            break;

        case UIState::MENU_AIRQUALITY:
            // Samo prikazuje podatke — tipkalo za nazad
            if (btn) {
                enterMenu(UIState::MENU_ROOT, state);
            }
            break;

        case UIState::MENU_LOGGING:
            if (jy != 0) {
                uint8_t n = menuItemCount(state.ui);
                state.menu_cursor = (state.menu_cursor + n + jy) % n;
            }
            if (btn) {
                switch (state.menu_cursor) {
                    case 0: // Aktivno logiranje ON/OFF
                        state.logging_active = !state.logging_active;
                        break;
                    case 1: // Nazad
                        enterMenu(UIState::MENU_ROOT, state);
                        break;
                }
            }
            break;
    }
}
