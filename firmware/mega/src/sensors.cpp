#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "DFRobot_MultiGasSensor.h"
#include "DFRobot_ENS160.h"

// Keširane vrijednosti za sigurnosni ISR (volatile, piše se ovdje, čita Timer5 ISR)
volatile float cached_t_catalyst       = 0.0f;
volatile float cached_t_chamber_heater = 0.0f;
volatile float cached_p_big            = 0.0f;
volatile float cached_p_small          = 0.0f;

static Adafruit_BMP280 bmp;
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature ds18b20(&oneWire);

// DFRobot plinski senzori (svi na MUX kanalu 7)
static DFRobot_GAS_I2C gasSensorO2(&Wire, I2C_O2_ADDR);
static DFRobot_GAS_I2C gasSensorH2(&Wire, I2C_H2_ADDR);
static DFRobot_GAS_I2C gasSensorH2S(&Wire, I2C_H2S_ADDR);
static DFRobot_GAS_I2C gasSensorO3(&Wire, I2C_O3_ADDR);
static DFRobot_ENS160_I2C aqSensor(&Wire, I2C_AQ_ADDR);

static bool gas_o2_ok  = false;
static bool gas_h2_ok  = false;
static bool gas_h2s_ok = false;
static bool gas_o3_ok  = false;
static bool gas_aq_ok  = false;

// DS18B20 adrese uređaja (kopirane iz config.h ROM konstanti)
static DeviceAddress addr_catalyst;
static DeviceAddress addr_chamber_heater;
static DeviceAddress addr_chamber;

static uint32_t warmup_start_ms = 0;

// Odabir I2C mux kanala
static void selectMuxChannel(uint8_t channel) {
    Wire.beginTransmission(I2C_MUX_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

// Isključi mux (svi kanali zatvoreni)
static void disableMux() {
    Wire.beginTransmission(I2C_MUX_ADDR);
    Wire.write(0);
    Wire.endTransmission();
}

void sensors_init() {
    Wire.begin();

    // Inicijalizacija BMP280 na obje mux pozicije (adresa 0x77)
    selectMuxChannel(I2C_MUX_CH_P_BIG);
    if (!bmp.begin(I2C_BMP280_ADDR)) {
        Serial.println(F("BMP280 (velika) init neuspjeh"));
    }

    selectMuxChannel(I2C_MUX_CH_P_SMALL);
    if (!bmp.begin(I2C_BMP280_ADDR)) {
        Serial.println(F("BMP280 (mala) init neuspjeh"));
    }

    disableMux();

    // Inicijalizacija DS18B20 — čitanje po ROM adresi
    ds18b20.begin();
    ds18b20.setWaitForConversion(false); // Neblokirajuće čitanje

    // Kopiraj ROM adrese iz config.h
    memcpy(addr_catalyst,       ROM_T_CATALYST,       8);
    memcpy(addr_chamber_heater, ROM_T_CHAMBER_HEATER, 8);
    memcpy(addr_chamber,        ROM_T_CHAMBER,        8);

    uint8_t count = ds18b20.getDeviceCount();
    Serial.print(F("DS18B20 pronađeno: "));
    Serial.println(count);

    // Inicijalizacija DFRobot plinskih senzora na mux kanalu 7
    selectMuxChannel(I2C_MUX_CH_GAS);

    gas_o2_ok = gasSensorO2.begin();
    if (gas_o2_ok) {
        gasSensorO2.changeAcquireMode(gasSensorO2.INITIATIVE);
        gasSensorO2.setTempCompensation(gasSensorO2.OFF);
    } else {
        Serial.println(F("O2 senzor (SEN0465) init neuspjeh"));
    }

    gas_h2_ok = gasSensorH2.begin();
    if (gas_h2_ok) {
        gasSensorH2.changeAcquireMode(gasSensorH2.INITIATIVE);
        gasSensorH2.setTempCompensation(gasSensorH2.OFF);
    } else {
        Serial.println(F("H2 senzor (SEN0473) init neuspjeh"));
    }

    gas_h2s_ok = gasSensorH2S.begin();
    if (gas_h2s_ok) {
        gasSensorH2S.changeAcquireMode(gasSensorH2S.INITIATIVE);
        gasSensorH2S.setTempCompensation(gasSensorH2S.OFF);
    } else {
        Serial.println(F("H2S senzor (SEN0467) init neuspjeh"));
    }

    gas_o3_ok = gasSensorO3.begin();
    if (gas_o3_ok) {
        gasSensorO3.changeAcquireMode(gasSensorO3.INITIATIVE);
        gasSensorO3.setTempCompensation(gasSensorO3.OFF);
    } else {
        Serial.println(F("O3 senzor (SEN0472) init neuspjeh"));
    }

    gas_aq_ok = (aqSensor.begin() == NO_ERR);
    if (gas_aq_ok) {
        aqSensor.setPWRMode(ENS160_STANDARD_MODE);
        aqSensor.setTempAndHum(25.0, 50.0);
    } else {
        Serial.println(F("AQ senzor (SEN0514/ENS160) init neuspjeh"));
    }

    disableMux();

    warmup_start_ms = millis();
}

void sensors_readPressure(SensorData& data) {
    // Velika komora — mux kanal 2, BMP280 na 0x77
    selectMuxChannel(I2C_MUX_CH_P_BIG);
    data.p_big_hpa = bmp.readPressure() / 100.0f + P_BIG_OFFSET_HPA;
    cached_p_big = data.p_big_hpa;

    // Mala komora — mux kanal 4, BMP280 na 0x77
    selectMuxChannel(I2C_MUX_CH_P_SMALL);
    data.p_small_hpa = bmp.readPressure() / 100.0f + P_SMALL_OFFSET_HPA;
    cached_p_small = data.p_small_hpa;

    disableMux();
}

void sensors_readGas(SensorData& data) {
    // Ažuriraj status zagrijavanja
    uint32_t elapsed = (millis() - warmup_start_ms) / 1000;
    if (elapsed >= GAS_WARMUP_S) {
        data.gas_warmed_up = true;
        data.warmup_remaining_s = 0;
    } else {
        data.gas_warmed_up = false;
        data.warmup_remaining_s = GAS_WARMUP_S - elapsed;
    }

    // Čitanje plinskih senzora preko mux kanala 7
    selectMuxChannel(I2C_MUX_CH_GAS);

    if (gas_o2_ok) {
    float val = gasSensorO2.readGasConcentrationPPM();

    if (val >= 0) {
        data.o2_raw_pct = val;  // raw SEN0465 value, %Vol

        float corrected = val - O2_ZERO_OFFSET_PCT;

        if (corrected < O2_LOW_CUTOFF_PCT) {
            corrected = 0.0f;
        }

        data.o2_pct = corrected; // corrected/display value
    }
}

    if (gas_h2_ok) {
        float val = gasSensorH2.readGasConcentrationPPM();
        if (val >= 0) data.h2_ppm = val;
    }

    if (gas_h2s_ok) {
        float val = gasSensorH2S.readGasConcentrationPPM();
        if (val >= 0) data.h2s_ppm = val;
    }

    if (gas_o3_ok) {
        float val = gasSensorO3.readGasConcentrationPPM();
        if (val >= 0) data.o3_ppm = val;
    }

    if (gas_aq_ok) {
        uint16_t eco2 = aqSensor.getECO2();
        if (eco2 > 0) data.eco2_ppm = (float)eco2;
    }

    disableMux();
}

void sensors_readTemperature(SensorData& data) {
    ds18b20.requestTemperatures();

    // Čitanje po ROM adresi — ne ovisi o redoslijedu otkrivanja
    float t;

    t = ds18b20.getTempC(addr_catalyst);
    data.t_catalyst_valid = (t != DEVICE_DISCONNECTED_C) && (t != 85.0f);
    if (data.t_catalyst_valid) {
        data.t_catalyst = t;
        cached_t_catalyst = t;
    }

    t = ds18b20.getTempC(addr_chamber_heater);
    data.t_chamber_heater_valid = (t != DEVICE_DISCONNECTED_C) && (t != 85.0f);
    if (data.t_chamber_heater_valid) {
        data.t_chamber_heater = t;
        cached_t_chamber_heater = t;
    }

    t = ds18b20.getTempC(addr_chamber);
    data.t_chamber_valid = (t != DEVICE_DISCONNECTED_C) && (t != 85.0f);
    if (data.t_chamber_valid) {
        data.t_chamber = t;
    }
}
