#include "actuators.h"
#include "config.h"

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
    analogWrite(PIN_HEATER_CAT, 0);
    analogWrite(PIN_HEATER_CHM, 0);

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

void actuators_setCatalystPWM(uint8_t pwm) {
    analogWrite(PIN_HEATER_CAT, pwm);
}

void actuators_catalystOff() {
    analogWrite(PIN_HEATER_CAT, 0);
}

void actuators_setChamberPWM(uint8_t pwm) {
    analogWrite(PIN_HEATER_CHM, pwm);
}

void actuators_chamberOff() {
    analogWrite(PIN_HEATER_CHM, 0);
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
