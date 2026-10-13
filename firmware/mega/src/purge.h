#pragma once

#include "sensors.h"
#include "safety.h"

void purge_startSmall(const SensorData& sensors, SystemState& state);
void purge_startBig(const SensorData& sensors, SystemState& state);
void purge_tickSmall(const SensorData& sensors, SystemState& state);
void purge_tickBig(const SensorData& sensors, SystemState& state);
bool purge_isDone();

// Granica pretlaka (hPa, apsolutno) za komoru koja se trenutno pročišćava,
// izračunata pri pokretanju purge-a. Vrijedi samo dok je ops PURGE_SMALL/BIG.
float purge_getOverpressureLimit();
