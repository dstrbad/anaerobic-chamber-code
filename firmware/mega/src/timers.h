#pragma once

#include <Arduino.h>

// Initialize hardware timers (Timer1, Timer3, Timer4 for flags; Timer5 for safety ISR + heater PWM)
void timers_init();

// Provjera iz loop()-a: je li Timer5 još konfiguriran kako ga je postavio
// timers_init() i otkucava li sigurnosni ISR. false = sigurnosni sloj 2 i PWM
// grijača nisu pouzdani.
bool timers_timer5Ok();
