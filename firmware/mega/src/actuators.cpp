#include "actuators.h"
#include "config.h"
#include <util/atomic.h>

// Vremenske oznake za ISR sigurnosne provjere (volatile, čita Timer5 ISR)
volatile uint32_t pump_on_since     = 0;
volatile uint32_t solenoid_on_since = 0;

static bool pump_active     = false;
static bool solenoid_active = false;

void actuators_init() {
    // Postavi sve aktuatorske pinove kao OUTPUT i LOW prije svega ostalog
    pinMode(PIN_PUMP, OUTPUT);
    pinMode(PIN_SOLENOID, OUTPUT);
    pinMode(PIN_HEATER_CAT, OUTPUT);
    pinMode(PIN_HEATER_CHM, OUTPUT);

    digitalWrite(PIN_PUMP, LOW);
    digitalWrite(PIN_SOLENOID, LOW);
    digitalWrite(PIN_HEATER_CAT, LOW);   // gasi i Arduino PWM s Timer5
    digitalWrite(PIN_HEATER_CHM, LOW);

    pump_on_since = 0;
    solenoid_on_since = 0;
}

// --- Pumpa ---

void actuators_pumpOn() {
    if (!pump_active) {
        pump_on_since = millis();
        pump_active = true;
    }
    digitalWrite(PIN_PUMP, HIGH);
}

void actuators_pumpOff() {
    digitalWrite(PIN_PUMP, LOW);
    pump_on_since = 0;
    pump_active = false;
}

void actuators_refreshPumpTimeout() {
    if (pump_active) {
        pump_on_since = millis();
    }
}

// --- Solenoid ---

void actuators_solenoidOn() {
    if (!solenoid_active) {
        solenoid_on_since = millis();
        solenoid_active = true;
    }
    digitalWrite(PIN_SOLENOID, HIGH);
}

void actuators_solenoidOff() {
    digitalWrite(PIN_SOLENOID, LOW);
    solenoid_on_since = 0;
    solenoid_active = false;
}

void actuators_refreshSolenoidTimeout() {
    if (solenoid_active) {
        solenoid_on_since = millis();
    }
}

// --- Grijači ---
//
// Ne koristimo analogWrite(): Timer5 radi u Fast PWM modu s TOP = ICR5
// (timers.cpp), pa se 0..255 skalira na 0..TIMER5_TOP. 0 i 255 odspajaju PWM
// i drže pin preko PORTL (čisto OFF / čisto ON). Sigurnosni ISR mijenja iste
// registre (TCCR5A, PORTL), pa read-modify-write mora biti atomaran — inače
// bismo mogli vratiti COM bit koji je ISR upravo obrisao.

static void heaterWrite(volatile uint16_t* ocr, uint8_t com_bit, uint8_t port_bit, uint8_t pwm) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (pwm == 0) {
            TCCR5A &= ~(1 << com_bit);
            PORTL  &= ~(1 << port_bit);
        } else if (pwm == 255) {
            TCCR5A &= ~(1 << com_bit);
            PORTL  |=  (1 << port_bit);
        } else {
            *ocr = (uint16_t)(((uint32_t)pwm * TIMER5_TOP) / 255);
            PORTL  &= ~(1 << port_bit);  // kad se PWM odspoji, pin pada na LOW
            TCCR5A |=  (1 << com_bit);
        }
    }
}

void actuators_setCatalystPWM(uint8_t pwm) {
    heaterWrite(&OCR5C, COM5C1, PL5, pwm);  // D44
}

void actuators_catalystOff() {
    heaterWrite(&OCR5C, COM5C1, PL5, 0);
}

void actuators_setChamberPWM(uint8_t pwm) {
    heaterWrite(&OCR5B, COM5B1, PL4, pwm);  // D45
}

void actuators_chamberOff() {
    heaterWrite(&OCR5B, COM5B1, PL4, 0);
}

// --- Pomoćne funkcije ---

void actuators_allOff() {
    actuators_pumpOff();
    actuators_solenoidOff();
    actuators_catalystOff();
    actuators_chamberOff();
}

bool actuators_isPumpOn()     { return pump_active; }
bool actuators_isSolenoidOn() { return solenoid_active; }
