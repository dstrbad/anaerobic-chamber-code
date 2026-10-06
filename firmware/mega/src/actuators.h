#pragma once

#include <Arduino.h>

void actuators_init();

void actuators_pumpOn();
void actuators_pumpOff();
void actuators_refreshPumpTimeout();

void actuators_solenoidOn();
void actuators_solenoidOff();
void actuators_refreshSolenoidTimeout();

void actuators_setCatalystPWM(uint8_t pwm);
void actuators_catalystOff();

void actuators_setChamberPWM(uint8_t pwm);
void actuators_chamberOff();

void actuators_allOff();

bool actuators_isPumpOn();
bool actuators_isSolenoidOn();
