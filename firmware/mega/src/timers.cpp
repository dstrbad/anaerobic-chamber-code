#include "timers.h"
#include "config.h"

// Vanjske zastavice (definirane u main.cpp)
extern volatile bool flag_read_pressure;
extern volatile bool flag_read_gas;
extern volatile bool flag_read_temp;
extern volatile bool flag_send_serial;
extern volatile bool flag_update_display;

// Vanjske vremenske oznake živosti i zastavica greške (definirane u main.cpp)
extern volatile uint32_t operations_fsm_last_tick;
extern volatile uint32_t thermal_fsm_last_tick;
extern volatile bool isr_fault_flag;

// Keširane vrijednosti senzora za sigurnosni ISR (piše sensors modul)
extern volatile float cached_t_catalyst;
extern volatile float cached_t_chamber_heater;
extern volatile float cached_p_big;
extern volatile float cached_p_small;

// Vremenske oznake aktuatora (piše actuators modul)
extern volatile uint32_t pump_on_since;
extern volatile uint32_t solenoid_on_since;

void timers_init() {
    cli(); // Onemogući prekide tijekom postavljanja

    // Timer1: 10 Hz — očitanje tlaka
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS12); // CTC, predjelitelj 256
    OCR1A = (62500 / TIMER1_FREQ_HZ) - 1;
    TIMSK1 |= (1 << OCIE1A);

    // Timer3: 1 Hz — plinski senzori, temperatura, serijski TX
    TCCR3A = 0;
    TCCR3B = (1 << WGM32) | (1 << CS32); // CTC, predjelitelj 256
    OCR3A = (62500 / TIMER3_FREQ_HZ) - 1;
    TIMSK3 |= (1 << OCIE3A);

    // Timer4: 20 Hz — osvježavanje ekrana, unos
    TCCR4A = 0;
    TCCR4B = (1 << WGM42) | (1 << CS42); // CTC, predjelitelj 256
    OCR4A = (62500 / TIMER4_FREQ_HZ) - 1;
    TIMSK4 |= (1 << OCIE4A);

    // Timer5: 50 Hz — sigurnosni ISR (neovisan o glavnoj petlji)
    TCCR5A = 0;
    TCCR5B = (1 << WGM52) | (1 << CS52); // CTC, predjelitelj 256
    OCR5A = (62500 / TIMER5_FREQ_HZ) - 1;
    TIMSK5 |= (1 << OCIE5A);

    sei(); // Ponovno omogući prekide
}

// --- Timer ISR-ovi ---

ISR(TIMER1_COMPA_vect) {
    flag_read_pressure = true;
}

ISR(TIMER3_COMPA_vect) {
    flag_read_gas    = true;
    flag_read_temp   = true;
    flag_send_serial = true;
}

ISR(TIMER4_COMPA_vect) {
    flag_update_display = true;
}

// Timer5: Sigurnosni ISR — provodi tvrde limite neovisno o glavnoj petlji
ISR(TIMER5_COMPA_vect) {
    uint32_t now = millis();

    // Tvrdi temperaturni limiti (direktna manipulacija portova, sigurno u ISR-u)
    if (cached_t_catalyst > HEATER_CAT_MAX_C) {
        // D44 = PL5
        PORTL &= ~(1 << PL5);
    }
    if (cached_t_chamber_heater > HEATER_CHM_MAX_C) {
        // D45 = PL4
        PORTL &= ~(1 << PL4);
    }

    // Tvrdi limit pretlaka — ugasi solenoid (D23 = PA1)
    if (cached_p_big > (1013.25f + OVERPRESSURE_CUTOFF_HPA) ||
        cached_p_small > (1013.25f + OVERPRESSURE_CUTOFF_HPA)) {
        PORTA &= ~(1 << PA1);
    }

    // Tvrdi timeout aktuatora
    if (pump_on_since > 0 && (now - pump_on_since) > MAX_ACTUATOR_ON_MS) {
        PORTA &= ~(1 << PA0); // D22 pumpa UGAŠENA
        PORTA &= ~(1 << PA1); // D23 solenoid UGAŠEN
        isr_fault_flag = true;
    }
    if (solenoid_on_since > 0 && (now - solenoid_on_since) > MAX_ACTUATOR_ON_MS) {
        PORTA &= ~(1 << PA1); // D23 solenoid UGAŠEN
        isr_fault_flag = true;
    }

    // Provjera živosti FSM-a
    if (now > 2000) { // Preskoči prvu sekundu nakon pokretanja
        if ((now - operations_fsm_last_tick) > FSM_LIVENESS_TIMEOUT_MS ||
            (now - thermal_fsm_last_tick) > FSM_LIVENESS_TIMEOUT_MS) {
            PORTA &= ~(1 << PA0); // pumpa UGAŠENA
            PORTA &= ~(1 << PA1); // solenoid UGAŠEN
            PORTL &= ~(1 << PL5); // grijač katalizatora UGAŠEN
            PORTL &= ~(1 << PL4); // grijač komore UGAŠEN
            isr_fault_flag = true;
        }
    }
}
