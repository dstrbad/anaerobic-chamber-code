#pragma once

#include "sensors.h"
#include "safety.h"

void fsm_init();
void fsm_tickOperations(const SensorData& sensors, SystemState& state);
void fsm_tickThermal(const SensorData& sensors, SystemState& state);
void fsm_tickUI(const SensorData& sensors, SystemState& state);
