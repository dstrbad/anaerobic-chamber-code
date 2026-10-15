#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <util/atomic.h>
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

// Jedan objekt po senzoru — begin() učitava tvorničku kalibraciju čipa, pa
// dijeljeni objekt računa oba tlaka s kalibracijom zadnje inicijaliziranog.
static Adafruit_BMP280 bmpBig;
static Adafruit_BMP280 bmpSmall;
static bool bmp_big_ok   = false;   // begin() uspio; ako nije, ponavlja se pri čitanju
static bool bmp_small_ok = false;
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

// Timer5 ISR čita keširane floatove; upis mora biti atomaran da ISR ne vidi
// napola upisanu vrijednost.
static void setCached(volatile float& v, float value) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = value; }
}

uint16_t i2c_error_count = 0;

// Oslobađanje zapele sabirnice: ako je slave prekinut usred prijenosa i drži
// SDA LOW, do 9 taktova SCL ga pusti, zatim STOP. Reset Mege ne resetira
// senzore ni mux, pa ovo mora ići prije svakog Wire.begin().
// Linije se nikad ne tjeraju u HIGH — LOW ili pull-up (open-drain).
static void i2c_releaseBus() {
    pinMode(SDA, INPUT_PULLUP);
    pinMode(SCL, INPUT_PULLUP);
    delayMicroseconds(10);

    for (uint8_t i = 0; i < 9 && digitalRead(SDA) == LOW; i++) {
        digitalWrite(SCL, LOW);  pinMode(SCL, OUTPUT);
        delayMicroseconds(10);
        pinMode(SCL, INPUT_PULLUP);
        delayMicroseconds(10);
    }

    // STOP: SDA LOW -> HIGH dok je SCL HIGH
    digitalWrite(SDA, LOW);  pinMode(SDA, OUTPUT);
    delayMicroseconds(10);
    pinMode(SDA, INPUT_PULLUP);
    delayMicroseconds(10);
}

static void i2c_begin() {
    i2c_releaseBus();
    Wire.begin();
    Wire.setWireTimeout(I2C_TIMEOUT_US, true);  // true = resetiraj TWI pri timeoutu
}

// Poziva se nakon I2C operacija. Ako je bio timeout, obnavlja sabirnicu i
// vraća true — rezultat te operacije treba odbaciti.
static bool i2c_recoverIfTimedOut() {
    if (!Wire.getWireTimeoutFlag()) return false;
    Wire.clearWireTimeoutFlag();
    i2c_error_count++;
    Wire.end();
    i2c_begin();
    return true;
}

void sensors_i2cAfterDisplay() {
    // U8g2 prije svakog prijenosa postavi 400 kHz i ne vraća; senzori iza
    // muxa na dužim kablovima rade na 100 kHz.
    Wire.setClock(100000);
    // Timeout tijekom crtanja obnovi odmah, da se ne pripiše idućem senzoru
    i2c_recoverIfTimedOut();
}

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
    i2c_begin();

    // Inicijalizacija BMP280 na obje mux pozicije (adresa 0x77)
    selectMuxChannel(I2C_MUX_CH_P_BIG);
    bmp_big_ok = bmpBig.begin(I2C_BMP280_ADDR);
    if (!bmp_big_ok) {
        Serial.println(F("BMP280 (velika) init neuspjeh"));
    }

    selectMuxChannel(I2C_MUX_CH_P_SMALL);
    bmp_small_ok = bmpSmall.begin(I2C_BMP280_ADDR);
    if (!bmp_small_ok) {
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
        gasSensorO2.changeAcquireMode(gasSensorO2.PASSIVITY);
        gasSensorO2.setTempCompensation(gasSensorO2.OFF);
    } else {
        Serial.println(F("O2 senzor (SEN0465) init neuspjeh"));
    }

    gas_h2_ok = gasSensorH2.begin();
    if (gas_h2_ok) {
        gasSensorH2.changeAcquireMode(gasSensorH2.PASSIVITY);
        gasSensorH2.setTempCompensation(gasSensorH2.OFF);
    } else {
        Serial.println(F("H2 senzor (SEN0473) init neuspjeh"));
    }

    gas_h2s_ok = gasSensorH2S.begin();
    if (gas_h2s_ok) {
        gasSensorH2S.changeAcquireMode(gasSensorH2S.PASSIVITY);
        gasSensorH2S.setTempCompensation(gasSensorH2S.OFF);
    } else {
        Serial.println(F("H2S senzor (SEN0467) init neuspjeh"));
    }

    gas_o3_ok = gasSensorO3.begin();
    if (gas_o3_ok) {
        gasSensorO3.changeAcquireMode(gasSensorO3.PASSIVITY);
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

// Čita jedan BMP280 iza muxa. Biblioteka pri grešci ne javlja ništa nego vraća
// uvjerljiv broj, zato: senzor mora odgovoriti na adresu, bez timeouta, i
// rezultat mora biti u fizikalno mogućem rasponu.
// Ako begin() nije uspio (pri pokretanju ili kasnije), ponavlja se čim senzor
// odgovori na adresu; uspješan begin() blokira ~100 ms, ali samo jednom.
static bool readBmpPressure(Adafruit_BMP280& sensor, bool& init_ok,
                            uint8_t channel, float& out_hpa) {
    selectMuxChannel(channel);
    Wire.beginTransmission(I2C_BMP280_ADDR);
    bool present = (Wire.endTransmission() == 0);
    if (present && !init_ok) {
        init_ok = sensor.begin(I2C_BMP280_ADDR);
    }
    float p = (present && init_ok) ? sensor.readPressure() / 100.0f : NAN;

    if (i2c_recoverIfTimedOut()) return false;
    if (!present || isnan(p) || p < P_PLAUSIBLE_MIN_HPA || p > P_PLAUSIBLE_MAX_HPA) {
        i2c_error_count++;
        return false;
    }
    out_hpa = p;
    return true;
}

void sensors_readPressure(SensorData& data) {
    float p;

    // Velika komora — mux kanal 2, BMP280 na 0x77
    if (readBmpPressure(bmpBig, bmp_big_ok, I2C_MUX_CH_P_BIG, p)) {
        data.p_big_hpa = p + P_BIG_OFFSET_HPA;
        data.p_big_ok_ms = millis();
        setCached(cached_p_big, data.p_big_hpa);
    }

    // Mala komora — mux kanal 4, BMP280 na 0x77
    if (readBmpPressure(bmpSmall, bmp_small_ok, I2C_MUX_CH_P_SMALL, p)) {
        data.p_small_hpa = p + P_SMALL_OFFSET_HPA;
        data.p_small_ok_ms = millis();
        setCached(cached_p_small, data.p_small_hpa);
    }

    disableMux();
}

// --- Plinski senzori (DFRobot SEN0465/0467/0472/0473) -----------------------
//
// Biblioteka u readGasConcentrationPPM() šalje upit, čeka delay(100) i tek onda
// čita odgovor — 4 senzora = 400 ms blokiranja loop()-a svake sekunde. Ovdje je
// isti protokol razdvojen: sensors_startGasRead() šalje upit svim senzorima
// odjednom, sensors_pollGas() pokupi odgovore kad prođe GAS_RESPONSE_DELAY_MS.
//
// Okvir (I2C registar 0x00), 9 bajtova:
//   upit:    FF 01 86 00 00 00 00 00 CS
//   odgovor: FF 86 Ch Cl tip dec 00 00 CS     koncentracija = (Ch<<8|Cl) / 10^dec
//   CS = (~(zbroj bajtova 1..7)) + 1
// Temperaturna kompenzacija je isključena (setTempCompensation OFF), pa se
// vrijednost ne korigira. Senzori su u PASSIVITY modu (odgovaraju samo na
// upit), što odgovara ovom upit/odgovor protokolu i provjeri bajta 1 = 0x86.

static constexpr uint32_t GAS_RESPONSE_DELAY_MS = 100;  // isto kao delay(100) u biblioteci
static constexpr uint8_t  GAS_CMD_GET_CONC      = 0x86;

static bool     gas_pending    = false;
static uint32_t gas_request_ms = 0;

static uint8_t gasChecksum(const uint8_t* frame) {
    uint8_t sum = 0;
    for (uint8_t i = 1; i <= 7; i++) sum += frame[i];
    return (uint8_t)(~sum + 1);
}

static bool gasSendRequest(uint8_t addr) {
    uint8_t frame[9] = {0xFF, 0x01, GAS_CMD_GET_CONC, 0, 0, 0, 0, 0, 0};
    frame[8] = gasChecksum(frame);

    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    Wire.write(frame, sizeof(frame));
    return Wire.endTransmission() == 0;
}

// Za razliku od biblioteke, odbacuje odgovor koji nije pravi okvir: senzor koji
// ne odgovori daje same nule, a nule prolaze checksum (CS od 0 je 0) i
// biblioteka ih vraća kao koncentraciju 0 — npr. lažnih 0 % O2.
static bool gasReadResponse(uint8_t addr, float& out) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)9) != 9) return false;

    uint8_t r[9];
    for (uint8_t i = 0; i < 9; i++) r[i] = Wire.read();

    if (r[0] != 0xFF || r[1] != GAS_CMD_GET_CONC) return false;
    if (gasChecksum(r) != r[8]) return false;

    float c = (float)(((uint16_t)r[2] << 8) | r[3]);
    switch (r[5]) {
        case 1: c *= 0.1f;  break;
        case 2: c *= 0.01f; break;
        default: break;
    }
    out = c;
    return true;
}

// Jedan senzor: neuspjeh (NACK, loš okvir ili timeout) ostavlja staru vrijednost.
static bool gasCollect(bool sensor_ok, uint8_t addr, float& out) {
    if (!sensor_ok) return false;
    float val;
    bool ok = gasReadResponse(addr, val);
    if (i2c_recoverIfTimedOut()) {
        selectMuxChannel(I2C_MUX_CH_GAS);  // mux je mogao biti usred prijenosa
        return false;
    }
    if (!ok) {
        i2c_error_count++;
        return false;
    }
    out = val;
    return true;
}

void sensors_startGasRead(SensorData& data) {
    // Ažuriraj status zagrijavanja
    uint32_t elapsed = (millis() - warmup_start_ms) / 1000;
    if (elapsed >= GAS_WARMUP_S) {
        data.gas_warmed_up = true;
        data.warmup_remaining_s = 0;
    } else {
        data.gas_warmed_up = false;
        data.warmup_remaining_s = GAS_WARMUP_S - elapsed;
    }

    // Upit svim senzorima na mux kanalu 7 odjednom
    selectMuxChannel(I2C_MUX_CH_GAS);
    if (gas_o2_ok)  gasSendRequest(I2C_O2_ADDR);
    if (gas_h2_ok)  gasSendRequest(I2C_H2_ADDR);
    if (gas_h2s_ok) gasSendRequest(I2C_H2S_ADDR);
    if (gas_o3_ok)  gasSendRequest(I2C_O3_ADDR);
    if (i2c_recoverIfTimedOut()) {
        // Odgovori bi bili nepouzdani — preskoči ovaj ciklus
        gas_pending = false;
        return;
    }
    disableMux();

    gas_pending    = true;
    gas_request_ms = millis();
}

void sensors_pollGas(SensorData& data) {
    if (!gas_pending) return;
    if ((millis() - gas_request_ms) < GAS_RESPONSE_DELAY_MS) return;
    gas_pending = false;

    selectMuxChannel(I2C_MUX_CH_GAS);

    float val;
    if (gasCollect(gas_o2_ok, I2C_O2_ADDR, val)) {
        data.o2_raw_pct = val;  // raw SEN0465 value, %Vol
        data.o2_ok_ms   = millis();

        float corrected = val - O2_ZERO_OFFSET_PCT;

        if (corrected < O2_LOW_CUTOFF_PCT) {
            corrected = 0.0f;
        }

        data.o2_pct = corrected; // corrected/display value
    }
    if (gasCollect(gas_h2_ok, I2C_H2_ADDR, val)) {
        data.h2_raw_ppm = val;
        // Nula senzora ovisi o temperaturi zraka u komori (vidi config.h)
        float zero = H2_ZERO_PPM;
        if (data.t_chamber_valid) {
            zero += H2_ZERO_TC_PPM_PER_C * (data.t_chamber - H2_ZERO_REF_C);
        }
        float corrected = val - zero;
        if (corrected < H2_LOW_CUTOFF_PPM) corrected = 0.0f;
        data.h2_ppm = corrected;
    }
    if (gasCollect(gas_h2s_ok, I2C_H2S_ADDR, val)) data.h2s_ppm = val;
    if (gasCollect(gas_o3_ok,  I2C_O3_ADDR,  val)) data.o3_ppm  = val;

    if (gas_aq_ok) {
        uint16_t eco2 = aqSensor.getECO2();
        if (i2c_recoverIfTimedOut()) {
            selectMuxChannel(I2C_MUX_CH_GAS);
        } else if (eco2 > 0) {
            data.eco2_ppm = (float)eco2;
        }
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
        setCached(cached_t_catalyst, t);
    }

    t = ds18b20.getTempC(addr_chamber_heater);
    data.t_chamber_heater_valid = (t != DEVICE_DISCONNECTED_C) && (t != 85.0f);
    if (data.t_chamber_heater_valid) {
        data.t_chamber_heater = t;
        setCached(cached_t_chamber_heater, t);
    }

    t = ds18b20.getTempC(addr_chamber);
    data.t_chamber_valid = (t != DEVICE_DISCONNECTED_C) && (t != 85.0f);
    if (data.t_chamber_valid) {
        data.t_chamber = t;
    }
}
