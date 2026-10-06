#pragma once

// =============================================================================
// Anaerobna komora — Centralna konfiguracija
// Pinovi, I2C adrese, vremenske konstante i sigurnosni pragovi.
// Vrijednosti potvrđene hardware discovery skicom (2026-04-02).
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>

// --- Pinovi ------------------------------------------------------------------

// Aktuatori
constexpr uint8_t PIN_PUMP        = 22;   // SSR -> 230V vakuum pumpa
constexpr uint8_t PIN_SOLENOID    = 23;   // Relej -> H2 solenoidni ventil
constexpr uint8_t PIN_HEATER_CAT  = 44;   // MOSFET -> grijač katalizatora (PWM)
constexpr uint8_t PIN_HEATER_CHM  = 45;   // MOSFET -> grijač komore (PWM)

// Korisničko sučelje
constexpr uint8_t PIN_BUZZER      = 33;   // Piezo zujalica
constexpr uint8_t PIN_BUTTON      = A8;   // Analogni tipkalo
constexpr uint8_t PIN_JOY_X       = A4;   // Joystick X-os
constexpr uint8_t PIN_JOY_Y       = A5;   // Joystick Y-os

// Temperatura (1-Wire sabirnica)
constexpr uint8_t PIN_ONEWIRE     = 32;   // DS18B20 zajednička sabirnica

// SD kartica (SPI)
constexpr uint8_t PIN_SD_CS       = 53;   // DFR0229 chip select
// MOSI=51, MISO=50, SCK=52 su hardverski SPI zadane vrijednosti

// --- I2C adrese (potvrđeno discovery skicom) ---------------------------------

constexpr uint8_t I2C_MUX_ADDR       = 0x70;  // TCA9548A multiplekser
constexpr uint8_t I2C_MUX_CH_P_BIG   = 2;     // BMP280 velika komora
constexpr uint8_t I2C_MUX_CH_P_SMALL = 4;     // BMP280 prijenosna komora
constexpr uint8_t I2C_MUX_CH_GAS     = 7;     // Svi plinski senzori na mux kanalu 7
constexpr uint8_t I2C_BMP280_ADDR    = 0x77;  // BMP280 adresa (obje na 0x77, razdvojene mux-om)

constexpr uint8_t I2C_DISPLAY_ADDR   = 0x3D;  // SSD1306 OLED (direktna sabirnica, ne kroz mux)

constexpr uint8_t I2C_O2_ADDR        = 0x74;  // DFRobot SEN0465
constexpr uint8_t I2C_H2_ADDR        = 0x75;  // DFRobot SEN0473
constexpr uint8_t I2C_H2S_ADDR       = 0x76;  // DFRobot SEN0467
constexpr uint8_t I2C_O3_ADDR        = 0x77;  // DFRobot SEN0472
constexpr uint8_t I2C_AQ_ADDR        = 0x53;  // DFRobot SEN0514

// --- DS18B20 ROM adrese (potvrđeno discovery skicom) -------------------------
// NAPOMENA: Potrebno identificirati koji ROM pripada kojem sondi.
// Sonda #0 je prva dodirnuta, brzo je porasla na 26.6C.
// Dodirni svaku sondu jednu po jednu i zabilježi koji ROM reagira.
constexpr uint8_t DS18B20_ROM_0[8] = {0x28,0x7E,0x58,0x9C,0xA0,0x24,0x0B,0xE1}; // Sonda #0
constexpr uint8_t DS18B20_ROM_1[8] = {0x28,0xCB,0x57,0x8D,0xA0,0x24,0x0B,0xD5}; // Sonda #1
constexpr uint8_t DS18B20_ROM_2[8] = {0x28,0xE7,0x46,0x99,0xA0,0x24,0x0B,0x99}; // Sonda #2

// Mapiranje ROM -> funkcija (promijeni nakon identifikacije sondi)
constexpr const uint8_t* ROM_T_CATALYST       = DS18B20_ROM_2; // TODO: potvrdi
constexpr const uint8_t* ROM_T_CHAMBER_HEATER = DS18B20_ROM_1; // TODO: potvrdi
constexpr const uint8_t* ROM_T_CHAMBER        = DS18B20_ROM_0; // TODO: potvrdi

// --- Frekvencije timera ------------------------------------------------------

constexpr uint8_t TIMER1_FREQ_HZ     = 10;    // Očitanje tlaka
constexpr uint8_t TIMER3_FREQ_HZ     = 1;     // Plinovi + temperatura + serijski TX
constexpr uint8_t TIMER4_FREQ_HZ     = 20;    // Osvježavanje ekrana + unos
constexpr uint8_t TIMER5_FREQ_HZ     = 50;    // Sigurnosni ISR

// --- Parametri senzora -------------------------------------------------------

constexpr uint16_t GAS_WARMUP_S      = 600;   // 10 min zagrijavanje elektrokemijskih senzora
constexpr float    GAS_SAMPLE_HZ     = 1.0f;
constexpr float    PRESSURE_SAMPLE_HZ = 10.0f;
constexpr float    O2_ZERO_OFFSET_PCT = 0.6f;  // measured raw value in anaerobic state
constexpr float    O2_LOW_CUTOFF_PCT  = 0.05f; // values below this shown as 0

// --- Parametri pročišćavanja (mala komora) -----------------------------------

constexpr uint8_t  PURGE_SMALL_CYCLES       = 7;
constexpr float    PURGE_SMALL_DP_HPA       = 400.0f;   // Ciljni pad tlaka
constexpr float    PURGE_SMALL_FLOOR_HPA    = 550.0f;   // ~0.550 atm apsolutni min
constexpr uint32_t PURGE_SMALL_TIMEOUT_MS   = 90000;    // 60s po fazi

// --- Parametri pročišćavanja (velika komora) ---------------------------------

constexpr uint8_t  PURGE_BIG_CYCLES         = 10;
constexpr float    PURGE_BIG_DP_HPA         = 10.0f;    // Blagi pad tlaka
constexpr float    PURGE_BIG_FLOOR_HPA      = 937.0f;   // ~0.925 atm apsolutni min
constexpr uint32_t PURGE_BIG_TIMEOUT_MS     = 90000;    // 90s po fazi

// --- Parametri grijača -------------------------------------------------------

constexpr float    HEATER_CAT_SETPOINT_C    = 50.0f;    // Tvornička zadana vrijednost katalizatora
constexpr float    HEATER_CAT_SETPOINT_MIN_C = 40.0f;   // Donja granica (ispod ovog katalizator slabo radi)
constexpr float    HEATER_CAT_SETPOINT_MAX_C = 58.0f;   // 2C ispod tvrdog isklopa
constexpr float    HEATER_CHM_SETPOINT_C    = 37.0f;    // Tvornička zadana vrijednost komore
constexpr float    HEATER_CHM_SETPOINT_MIN_C = 25.0f;
constexpr float    HEATER_CHM_SETPOINT_MAX_C = 43.0f;   // 2C ispod tvrdog isklopa
constexpr float    HEATER_CAT_MAX_C         = 60.0f;    // Sigurnosni isklop
constexpr float    HEATER_CHM_MAX_C         = 45.0f;    // Sigurnosni isklop

// --- EEPROM postavke ---------------------------------------------------------
constexpr uint16_t EEPROM_ADDR_MAGIC        = 0;
constexpr uint16_t EEPROM_ADDR_CHM_SP       = 1;        // uint8_t, °C, cijeli broj
constexpr uint16_t EEPROM_ADDR_CAT_SP       = 2;        // uint8_t, °C, cijeli broj
constexpr uint8_t  EEPROM_MAGIC_VALUE       = 0xA5;

// --- Sigurnosni pragovi ------------------------------------------------------

constexpr float    O2_BIG_PURGE_MAX_PCT     = 3.0f;     // Blokada velikog purge-a iznad ovog
constexpr float    WATCHDOG_DP_MIN_HPA      = 5.0f;    // Min detektabilna promjena tlaka
constexpr uint32_t WATCHDOG_WINDOW_MS       = 1500;     // Vrijeme za detekciju promjene tlaka
constexpr float    OVERPRESSURE_ALARM_HPA   = 25.0f;    // Relativno na referentni tlak
constexpr float    OVERPRESSURE_CUTOFF_HPA  = 25.0f;   // Tvrd isklop, solenoid ugašen
constexpr uint32_t TEMP_INVALID_TIMEOUT_MS  = 5000;     // DS18B20 timeout -> grijač ugašen
constexpr uint32_t MAX_ACTUATOR_ON_MS       = 90000;    // ISR tvrd timeout za aktuatore
constexpr uint32_t FSM_LIVENESS_TIMEOUT_MS  = 750;     // ISR gasi aktuatore ako FSM stane
constexpr uint16_t WDT_TIMEOUT              = WDTO_1S;

// --- Parametri sučelja -------------------------------------------------------

constexpr uint32_t MENU_TIMEOUT_MS          = 10000;    // Povratak na početni ekran
constexpr uint32_t ABORT_PRESS_MS           = 1000;     // Dugi pritisak za prekid
constexpr uint16_t BUTTON_THRESHOLD         = 600;      // Analogni prag za tipkalo (mirovanje=0, pritisak~687)
constexpr uint16_t JOY_CENTER               = 345;      // Joystick centar (potvrđeno: X~345, Y~340)
constexpr uint16_t JOY_DEADZONE             = 80;       // Mrtva zona oko centra
constexpr uint16_t JOY_THRESHOLD_LOW        = JOY_CENTER - JOY_DEADZONE;  // ~265
constexpr uint16_t JOY_THRESHOLD_HIGH       = JOY_CENTER + JOY_DEADZONE;  // ~425

// --- Parametri logiranja -----------------------------------------------------

constexpr uint32_t LOG_PASSIVE_INTERVAL_MS  = 3600000;  // 1 sat
constexpr uint32_t LOG_ACTIVE_INTERVAL_MS   = 10000;    // 10 sekundi
constexpr uint32_t LOG_ACTIVE_DURATION_MS   = 14400000; // 4 sata

constexpr uint32_t LOG_FILE_MAX_BYTES       = 65535;    // Rotacija nakon ~64 KB
constexpr uint8_t  LOG_FILE_RETENTION       = 32;       // Maksimalan broj sačuvanih datoteka

// --- Serijska komunikacija ---------------------------------------------------

constexpr uint32_t SERIAL_BAUD_USB          = 115200;
constexpr uint32_t SERIAL_BAUD_ESP32        = 115200;

// --- Offset za tlak ---------------------------------------------------
constexpr float P_BIG_OFFSET_HPA   = 0;
constexpr float P_SMALL_OFFSET_HPA =  0;
