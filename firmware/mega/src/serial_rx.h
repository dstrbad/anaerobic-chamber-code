#pragma once

#include "sensors.h"
#include "safety.h"

// Čita line-delimited JSON komande s ESP32 (Serial1 RX) i primjenjuje ih.
// Svaka komanda prolazi kroz iste validacijske funkcije koje koristi i
// lokalni OLED UI (clamping setpointa, big-purge gate, itd.).
void serial_rx_tick(const SensorData& sensors, SystemState& state);
