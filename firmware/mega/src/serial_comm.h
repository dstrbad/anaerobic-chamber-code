#pragma once

#include "sensors.h"
#include "safety.h"

void serial_comm_send(const SensorData& sensors, const SystemState& state);
