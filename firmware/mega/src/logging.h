#pragma once

#include "sensors.h"
#include "safety.h"

void logging_init();
void logging_tick(const SensorData& sensors, const SystemState& state);
