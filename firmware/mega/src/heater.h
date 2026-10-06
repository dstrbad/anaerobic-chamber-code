#pragma once

#include "sensors.h"
#include "safety.h"

void heater_init();
void heater_tickCatalyst(const SensorData& sensors, SystemState& state);
void heater_tickChamber(const SensorData& sensors, SystemState& state);

// Runtime setpointi grijača — korisnik mijenja preko OLED UI ili MQTT komande.
// Postavljaju se na zadane vrijednosti iz config.h ako EEPROM nije inicijaliziran.
// Sve set/save/reload funkcije klampuju u sigurni raspon iz config.h.

float heater_getCatalystSetpoint();
void  heater_setCatalystSetpoint(float c);
void  heater_saveCatalystSetpoint();
void  heater_reloadCatalystSetpoint();

float heater_getChamberSetpoint();
void  heater_setChamberSetpoint(float c);
void  heater_saveChamberSetpoint();
void  heater_reloadChamberSetpoint();
