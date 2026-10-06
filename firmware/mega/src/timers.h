#pragma once

#include <Arduino.h>

// Initialize hardware timers (Timer1, Timer3, Timer4 for flags; Timer5 for safety ISR)
void timers_init();
