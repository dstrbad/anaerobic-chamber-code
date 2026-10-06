#pragma once

#include <Arduino.h>

void input_init();
void input_update();

bool input_buttonPressed();     // Single press detected this frame
bool input_longPress();         // Long press (~1s) detected this frame
int8_t input_joystickY();       // -1 = up, 0 = center, +1 = down
int8_t input_joystickX();       // -1 = left, 0 = center, +1 = right
