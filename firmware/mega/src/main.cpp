#include <Arduino.h>
#include <avr/wdt.h>
#include "config.h"
#include "timers.h"
#include "sensors.h"
#include "actuators.h"
#include "safety.h"
#include "fsm.h"
#include "heater.h"
#include "display.h"
#include "input.h"
#include "serial_comm.h"
#include "serial_rx.h"
#include "logging.h"

// Zastavice timera — postavljaju ISR-ovi, koristi loop()
volatile bool flag_read_pressure = false;
volatile bool flag_read_gas      = false;
volatile bool flag_read_temp     = false;
volatile bool flag_send_serial   = false;
volatile bool flag_update_display = false;

// Vremenske oznake živosti FSM-a — piše loop(), čita Timer5 ISR
volatile uint32_t operations_fsm_last_tick = 0;
volatile uint32_t thermal_fsm_last_tick    = 0;

// ISR zastavica greške — postavlja Timer5 ISR ako detektira sigurnosni problem
volatile bool isr_fault_flag = false;

// Dijeljeni podaci
SensorData  sensorData;
SystemState systemState;

void setup() {
    // Svi aktuatori UGAŠENI prije svega ostalog
    actuators_init();

    Serial.begin(SERIAL_BAUD_USB);
    Serial1.begin(SERIAL_BAUD_ESP32);

    sensors_init();
    safety_init();
    fsm_init();
    heater_init();
    display_init();
    input_init();
    logging_init();
    timers_init();

    operations_fsm_last_tick = millis();
    thermal_fsm_last_tick    = millis();

    // Omogući hardverski watchdog zadnji — sve mora biti inicijalizirano
    wdt_enable(WDT_TIMEOUT);
}

void loop() {

    input_update();

    // 0. Komande s ESP32 (Serial1 RX) — primjenjuju se prije FSM ticka
    //    tako da efekt bude vidljiv u istoj iteraciji.
    serial_rx_tick(sensorData, systemState);

    // 1. Očitanja senzora pokretana timerima
    if (flag_read_pressure) {
        flag_read_pressure = false;
        sensors_readPressure(sensorData);
    }
    if (flag_read_gas) {
        flag_read_gas = false;
        sensors_readGas(sensorData);
    }
    if (flag_read_temp) {
        flag_read_temp = false;
        sensors_readTemperature(sensorData);
    }

    // 2. Sloj 3 sigurnosti — detaljne provjere svaku iteraciju
    safety_check(sensorData, systemState);

    // Provjeri je li Timer5 ISR podigao zastavicu greške
    if (isr_fault_flag) {
        isr_fault_flag = false;
        safety_handleIsrFault(systemState);
    }

    // 2.5. Remember whether we were in fault before FSM tick
    bool had_fault = (systemState.ops == OpsState::FAULT ||
                      systemState.thermal == ThermalState::HEAT_FAULT);

    // 3. FSM koraci
    fsm_tickOperations(sensorData, systemState);
    fsm_tickThermal(sensorData, systemState);

    // Ažuriraj vremenske oznake živosti (provjerava Timer5 ISR)
    operations_fsm_last_tick = millis();
    thermal_fsm_last_tick    = millis();

    // 2.5 - check.
    bool fault_cleared_this_cycle =
        had_fault &&
        systemState.ops != OpsState::FAULT &&
        systemState.thermal != ThermalState::HEAT_FAULT &&
        systemState.fault == FaultCode::NONE;

    if (fault_cleared_this_cycle) {
        systemState.ui = UIState::HOME;
        systemState.menu_cursor = 0;
        systemState.last_input_ms = millis();
    }

    // 4. Korisničko sučelje (pokrenuto timerom)
    if (flag_update_display) {
        flag_update_display = false;
        // Skip UI input handling in the same cycle that fault was acknowledged
        if (!fault_cleared_this_cycle) {
            fsm_tickUI(sensorData, systemState);
        }

        display_render(sensorData, systemState);
    }

    // 5. Serijski TX prema ESP32 (1 Hz)
    if (flag_send_serial) {
        flag_send_serial = false;
        serial_comm_send(sensorData, systemState);
    }

    // 6. Logiranje na SD karticu
    logging_tick(sensorData, systemState);

    // 7. Nahrani hardverski watchdog
    wdt_reset();
}
