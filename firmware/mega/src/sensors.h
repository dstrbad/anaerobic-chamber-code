#pragma once

#include <Arduino.h>

// Podaci senzora — ažurira se iz sensors.cpp, dijeli se među modulima
struct SensorData {
    // Plinski senzori
    float o2_pct       = 0.0f;
    float o2_raw_pct   = 0.0f;
    float h2_ppm       = 0.0f;
    float h2s_ppm      = 0.0f;
    float o3_ppm       = 0.0f;
    float eco2_ppm     = 0.0f;

    // Tlak (hPa)
    float p_big_hpa    = 0.0f;
    float p_small_hpa  = 0.0f;

    // Temperatura (C)
    float t_catalyst        = 0.0f;
    float t_chamber_heater  = 0.0f;
    float t_chamber         = 0.0f;

    // Valjanost temperaturnog senzora
    bool t_catalyst_valid       = false;
    bool t_chamber_heater_valid = false;
    bool t_chamber_valid        = false;

    // Praćenje zagrijavanja plinskih senzora
    bool     gas_warmed_up      = false;
    uint16_t warmup_remaining_s = 0;
};

void sensors_init();
void sensors_readPressure(SensorData& data);
void sensors_readGas(SensorData& data);
void sensors_readTemperature(SensorData& data);
