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

// Granice pretlaka po komori (piše safety_check; apsolutna ili granica purge-a)
extern volatile float cached_overp_limit_big;
extern volatile float cached_overp_limit_small;

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

    // Timer5: 50 Hz — sigurnosni ISR (overflow) + PWM grijača.
    // Fast PWM, mod 14 (TOP = ICR5). OC5C = D44 katalizator, OC5B = D45 komora.
    // COM bitove postavlja/briše actuators.cpp; ovdje su svi isključeni.
    TCCR5A = (1 << WGM51);
    TCCR5B = (1 << WGM53) | (1 << WGM52) | (1 << CS52); // predjelitelj 256
    ICR5   = TIMER5_TOP;
    TCNT5  = 0;
    TIMSK5 = (1 << TOIE5);

    // Arduino jezgra pri pokretanju vrti ove timere u PWM modu, pa su njihove
    // zastavice prekida već postavljene. Bez brisanja bi svi ISR-ovi okinuli
    // odmah nakon sei(). Upis 1 briše zastavicu.
    TIFR1 = 0xFF;
    TIFR3 = 0xFF;
    TIFR4 = 0xFF;
    TIFR5 = 0xFF;

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

// Gašenje grijača: dok je COM bit postavljen, PWM jedinica upravlja pinom i
// PORTL nema efekta, zato prvo odspoji PWM pa tek onda spusti pin.
#define KILL_HEATER_CAT() do { TCCR5A &= ~(1 << COM5C1); PORTL &= ~(1 << PL5); } while (0) // D44
#define KILL_HEATER_CHM() do { TCCR5A &= ~(1 << COM5B1); PORTL &= ~(1 << PL4); } while (0) // D45

// Otkucaj sigurnosnog ISR-a — loop() provjerava da se mijenja
static volatile uint8_t timer5_isr_count = 0;

static constexpr uint8_t  TIMER5_TCCR5B_EXPECTED = (1 << WGM53) | (1 << WGM52) | (1 << CS52);
static constexpr uint8_t  TIMER5_WGM_A_MASK      = (1 << WGM51) | (1 << WGM50);
static constexpr uint32_t TIMER5_STALL_MS        = 100;  // 5 propuštenih otkucaja na 50 Hz

bool timers_timer5Ok() {
    // Konfiguracija: mod 14, predjelitelj 256, TOP, overflow prekid
    if (TCCR5B != TIMER5_TCCR5B_EXPECTED) return false;
    if ((TCCR5A & TIMER5_WGM_A_MASK) != (1 << WGM51)) return false;
    if (ICR5 != TIMER5_TOP) return false;
    if (!(TIMSK5 & (1 << TOIE5))) return false;

    // Otkucaj: ISR mora povećati brojač barem jednom u TIMER5_STALL_MS
    static uint8_t  last_count     = 0;
    static uint32_t last_change_ms = 0;
    static bool     started        = false;

    uint32_t now   = millis();
    uint8_t  count = timer5_isr_count;
    if (!started || count != last_count) {
        started        = true;
        last_count     = count;
        last_change_ms = now;
        return true;
    }
    return (now - last_change_ms) <= TIMER5_STALL_MS;
}

// Timer5: Sigurnosni ISR — provodi tvrde limite neovisno o glavnoj petlji
ISR(TIMER5_OVF_vect) {
    uint32_t now = millis();
    timer5_isr_count++;

    // Tvrdi temperaturni limiti (direktna manipulacija registara, sigurno u ISR-u)
    if (cached_t_catalyst > HEATER_CAT_MAX_C) {
        KILL_HEATER_CAT();
    }
    if (cached_t_chamber_heater > HEATER_CHM_MAX_C) {
        KILL_HEATER_CHM();
    }

    // Tvrdi limit pretlaka — ugasi solenoid (D23 = PA1)
    if (cached_p_big > cached_overp_limit_big ||
        cached_p_small > cached_overp_limit_small) {
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
            KILL_HEATER_CAT();    // grijač katalizatora UGAŠEN
            KILL_HEATER_CHM();    // grijač komore UGAŠEN
            isr_fault_flag = true;
        }
    }
}
