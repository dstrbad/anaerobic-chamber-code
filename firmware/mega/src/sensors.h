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

    // Tlak (hPa) — pri neuspjelom čitanju ostaje zadnja ispravna vrijednost
    float p_big_hpa    = 0.0f;
    float p_small_hpa  = 0.0f;
    uint32_t p_big_ok_ms   = 0;   // millis() zadnjeg ispravnog očitanja (0 = nikad)
    uint32_t p_small_ok_ms = 0;

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

// Broj I2C grešaka (timeout / senzor ne odgovara) od pokretanja — telemetrija
extern uint16_t i2c_error_count;

void sensors_init();

// Poziva se nakon display_render(): vraća I2C na 100 kHz i obnavlja sabirnicu
// ako je crtanje završilo timeoutom.
void sensors_i2cAfterDisplay();
void sensors_readPressure(SensorData& data);
// Plinski senzori u dva koraka, bez blokiranja: startGasRead() šalje upit
// (1 Hz), pollGas() se zove svaku iteraciju i pokupi odgovore nakon ~100 ms.
void sensors_startGasRead(SensorData& data);
void sensors_pollGas(SensorData& data);
void sensors_readTemperature(SensorData& data);
