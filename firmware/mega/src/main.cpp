#include <Arduino.h>
#include <avr/wdt.h>
#include <EEPROM.h>
#include <util/atomic.h>
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

// Uzrok zadnjeg reseta (MCUSR: bit0 PORF, bit1 EXTRF, bit2 BORF, bit3 WDRF)
// i broj pokretanja iz EEPROM-a. Šalje se u telemetriji i logira na SD.
// .noinit jer se early_reset_init() izvršava prije nuliranja .bss-a.
uint8_t  reset_flags __attribute__((section(".noinit")));
uint16_t boot_count = 0;

// Izvršava se odmah nakon reseta, prije konstruktora i setup()-a. Nakon WDT
// reseta watchdog ostaje uključen s ~16 ms timeoutom; ako ga ništa ne ugasi,
// ploča se resetira u beskonačnoj petlji. Zato ga gasimo ovdje, a setup()
// ga ponovno uključuje.
void early_reset_init() __attribute__((naked, used, section(".init3")));
void early_reset_init() {
    reset_flags = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

static void count_boot() {
    EEPROM.get(EEPROM_ADDR_BOOT_COUNT, boot_count);
    boot_count++;
    EEPROM.put(EEPROM_ADDR_BOOT_COUNT, boot_count);
}

void setup() {
    // Svi aktuatori UGAŠENI prije svega ostalog
    actuators_init();

    // Watchdog odmah, s dugim timeoutom za inicijalizaciju. Ako init zapne
    // (npr. I2C), ploča se resetira umjesto da visi zauvijek.
    wdt_enable(WDTO_8S);

    Serial.begin(SERIAL_BAUD_USB);
    Serial1.begin(SERIAL_BAUD_ESP32);

    count_boot();
    Serial.print(F("Boot #"));
    Serial.print(boot_count);
    Serial.print(F(", MCUSR=0x"));
    Serial.println(reset_flags, HEX);

    sensors_init();      wdt_reset();
    safety_init();
    fsm_init();
    heater_init();
    display_init();      wdt_reset();
    input_init();
    logging_init();      wdt_reset();

    // Oznake živosti PRIJE timers_init(): sigurnosni ISR može okinuti odmah
    // nakon sei(), a s oznakama na 0 (i setupom > 2 s) proglasio bi FSM mrtvim.
    operations_fsm_last_tick = millis();
    thermal_fsm_last_tick    = millis();

    timers_init();

    // Radni timeout watchdoga za glavnu petlju
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
        sensors_startGasRead(sensorData);
    }
    sensors_pollGas(sensorData);  // pokupi odgovore ~100 ms nakon upita
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

    // Ažuriraj vremenske oznake živosti (provjerava Timer5 ISR).
    // Atomarno — napola upisan uint32 ISR vidi kao "FSM stao".
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        uint32_t now = millis();
        operations_fsm_last_tick = now;
        thermal_fsm_last_tick    = now;
    }

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

    // 4. Korisničko sučelje
    // UI FSM svaku iteraciju: input_update() drži pritisak tipkala samo jednu
    // iteraciju, pa bi se pritisak izgubio ako UI radi samo na tick timera.
    // Skip UI input handling in the same cycle that fault was acknowledged
    if (!fault_cleared_this_cycle) {
        fsm_tickUI(sensorData, systemState);
    }

    // Crtanje je skupo (~25 ms I2C), pa 5 Hz — ali odmah nakon unosa
    static bool    ui_input_pending = false;
    static uint8_t render_div       = 0;
    if (input_buttonPressed() || input_longPress() || input_joystickY() != 0) {
        ui_input_pending = true;
    }
    if (flag_update_display) {
        flag_update_display = false;
        if (ui_input_pending || ++render_div >= DISPLAY_RENDER_DIV) {
            ui_input_pending = false;
            render_div = 0;
            display_render(sensorData, systemState);
            sensors_i2cAfterDisplay();
        }
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
