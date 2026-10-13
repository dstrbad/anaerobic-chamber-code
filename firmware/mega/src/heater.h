#pragma once

#include "sensors.h"
#include "safety.h"

void heater_init();
void heater_tickCatalyst(const SensorData& sensors, SystemState& state);

// Katalizator: trenutna ciljna T nakon rampe (0 kad grijač katalizatora ne radi)
float heater_getCatalystTarget();
void heater_tickChamber(const SensorData& sensors, SystemState& state);

// Kaskadna regulacija komore: trenutna ciljna T ploče grijača (0 kad grijač
// komore ne radi) i je li cilj zapeo na HEATER_CHM_PLATE_MAX_C dok je komora
// još ispod SP (zadana T nedostižna uz dopuštenu T ploče).
float heater_getChamberHeaterTarget();
bool  heater_isChamberAtLimit();

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
