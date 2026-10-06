#pragma once

#include "sensors.h"
#include "safety.h"

void display_init();
void display_render(const SensorData& sensors, const SystemState& state);
