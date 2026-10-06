#pragma once

#include "sensors.h"
#include "safety.h"

void purge_startSmall(const SensorData& sensors, SystemState& state);
void purge_startBig(const SensorData& sensors, SystemState& state);
void purge_tickSmall(const SensorData& sensors, SystemState& state);
void purge_tickBig(const SensorData& sensors, SystemState& state);
bool purge_isDone();
