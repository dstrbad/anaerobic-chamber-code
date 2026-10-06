/**
 * Hardware Discovery Sketch — Anaerobic Chamber
 *
 * Flash this to the Arduino Mega before the main firmware to discover
 * and verify all connected hardware. Prints results to Serial Monitor (115200 baud).
 *
 * What it does:
 *   1. Scans I2C bus (direct + each TCA9548A mux channel) — finds display, gas sensors
 *   2. Enumerates DS18B20 ROM IDs on 1-Wire bus — needed for sensor-to-probe mapping
 *   3. Reads both BMP280 sensors through the mux — raw pressure/temp values
 *   4. Continuously prints analog inputs — button (A8), joystick X (A4), Y (A5)
 *   5. Tests actuator pins — brief LOW pulse to verify wiring (actuators stay OFF)
 *
 * Build:
 *   pio run -e discovery -t upload
 *   pio device monitor -e discovery
 */

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP280.h>

// --- Pin definitions (must match config.h) ---
#define PIN_ONEWIRE     32
#define PIN_BUTTON      A8
#define PIN_JOY_X       A4
#define PIN_JOY_Y       A5
#define PIN_PUMP        22
#define PIN_SOLENOID    23
#define PIN_HEATER_CAT  44
#define PIN_HEATER_CHM  45
#define PIN_BUZZER      33
#define PIN_SD_CS       53

#define I2C_MUX_ADDR    0x70

// --- Globals ---
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature ds18b20(&oneWire);
Adafruit_BMP280 bmp;

// =============================================================================
// I2C Scanner
// =============================================================================

static void selectMuxChannel(uint8_t channel) {
    Wire.beginTransmission(I2C_MUX_ADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

static void disableMux() {
    Wire.beginTransmission(I2C_MUX_ADDR);
    Wire.write(0);
    Wire.endTransmission();
}

static void scanI2C(const char* label) {
    Serial.print(F("  Scanning "));
    Serial.print(label);
    Serial.println(F("..."));

    uint8_t count = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        if (addr == I2C_MUX_ADDR) continue; // Skip the mux itself
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("    Found device at 0x"));
            if (addr < 16) Serial.print('0');
            Serial.print(addr, HEX);

            // Identify known devices
            switch (addr) {
                case 0x3C: Serial.print(F("  <- SSD1306 display (addr variant A)")); break;
                case 0x3D: Serial.print(F("  <- SSD1306 display (addr variant B)")); break;
                case 0x53: Serial.print(F("  <- SEN0514 Air Quality")); break;
                case 0x74: Serial.print(F("  <- SEN0465 O2")); break;
                case 0x75: Serial.print(F("  <- SEN0473 H2")); break;
                case 0x76: Serial.print(F("  <- SEN0467 H2S (or BMP280 default)")); break;
                case 0x77: Serial.print(F("  <- SEN0472 O3 (or BMP280 alt)")); break;
            }
            Serial.println();
            count++;
        }
    }
    if (count == 0) {
        Serial.println(F("    No devices found"));
    } else {
        Serial.print(F("    Total: "));
        Serial.println(count);
    }
}

static void runI2CScan() {
    Serial.println(F("\n========================================"));
    Serial.println(F("  I2C BUS SCAN"));
    Serial.println(F("========================================"));

    // Check if mux is present
    Wire.beginTransmission(I2C_MUX_ADDR);
    bool muxPresent = (Wire.endTransmission() == 0);

    Serial.print(F("  TCA9548A mux at 0x70: "));
    Serial.println(muxPresent ? F("FOUND") : F("NOT FOUND"));

    // Scan with mux disabled (direct bus devices: display, gas sensors)
    if (muxPresent) disableMux();
    scanI2C("direct bus (mux disabled)");

    // Scan each mux channel
    if (muxPresent) {
        for (uint8_t ch = 0; ch < 8; ch++) {
            selectMuxChannel(ch);
            char label[32];
            snprintf(label, sizeof(label), "mux channel %d", ch);
            scanI2C(label);
        }
        disableMux();
    }
}

// =============================================================================
// DS18B20 ROM ID Discovery
// =============================================================================

static void runDS18B20Scan() {
    Serial.println(F("\n========================================"));
    Serial.println(F("  DS18B20 1-WIRE SCAN (pin D32)"));
    Serial.println(F("========================================"));

    ds18b20.begin();
    uint8_t count = ds18b20.getDeviceCount();
    Serial.print(F("  Devices found: "));
    Serial.println(count);

    if (count == 0) {
        Serial.println(F("  Check wiring! Ensure 4.7k pull-up on data line."));
        return;
    }

    DeviceAddress addr;
    for (uint8_t i = 0; i < count; i++) {
        if (ds18b20.getAddress(addr, i)) {
            Serial.print(F("  Sensor #"));
            Serial.print(i);
            Serial.print(F("  ROM: "));
            for (uint8_t j = 0; j < 8; j++) {
                if (addr[j] < 16) Serial.print('0');
                Serial.print(addr[j], HEX);
                if (j < 7) Serial.print(':');
            }

            // Read temperature to help identify which probe is which
            ds18b20.requestTemperaturesByAddress(addr);
            float temp = ds18b20.getTempC(addr);
            Serial.print(F("  Temp: "));
            Serial.print(temp, 2);
            Serial.print(F(" C"));

            Serial.println();
        }
    }

    Serial.println();
    Serial.println(F("  To identify probes: heat one at a time (touch it) and note"));
    Serial.println(F("  which ROM ID shows a temperature increase."));
    Serial.println(F("  Map these ROM IDs in sensors.cpp for production firmware."));
}

// =============================================================================
// BMP280 Pressure Sensors (via mux)
// =============================================================================

static void runBMP280Test() {
    Serial.println(F("\n========================================"));
    Serial.println(F("  BMP280 PRESSURE SENSORS (via TCA9548A)"));
    Serial.println(F("========================================"));

    // Big chamber (mux channel 2)
    selectMuxChannel(2);
    if (bmp.begin()) {
        float p = bmp.readPressure() / 100.0f;
        float t = bmp.readTemperature();
        Serial.print(F("  Mux ch2 (P_big):   P = "));
        Serial.print(p, 2);
        Serial.print(F(" hPa,  T = "));
        Serial.print(t, 2);
        Serial.println(F(" C"));
    } else {
        Serial.println(F("  Mux ch2 (P_big):   NOT FOUND"));
    }

    // Small chamber (mux channel 4)
    selectMuxChannel(4);
    if (bmp.begin()) {
        float p = bmp.readPressure() / 100.0f;
        float t = bmp.readTemperature();
        Serial.print(F("  Mux ch4 (P_small): P = "));
        Serial.print(p, 2);
        Serial.print(F(" hPa,  T = "));
        Serial.print(t, 2);
        Serial.println(F(" C"));
    } else {
        Serial.println(F("  Mux ch4 (P_small): NOT FOUND"));
    }

    disableMux();

    Serial.println();
    Serial.println(F("  Compare these readings to a known reference (weather station,"));
    Serial.println(F("  phone barometer) to determine calibration offsets."));
    Serial.println(F("  Old code used: ch2 offset = -50.9 hPa, ch4 offset = +15.9 hPa"));
}

// =============================================================================
// Actuator Pin Verification
// =============================================================================

static void runActuatorCheck() {
    Serial.println(F("\n========================================"));
    Serial.println(F("  ACTUATOR PIN CHECK"));
    Serial.println(F("========================================"));
    Serial.println(F("  All pins set to OUTPUT LOW (safe state)."));

    uint8_t pins[] = {PIN_PUMP, PIN_SOLENOID, PIN_HEATER_CAT, PIN_HEATER_CHM, PIN_BUZZER};
    const char* names[] = {"Pump (D22)", "Solenoid (D23)", "Heater Cat (D44)", "Heater Chm (D45)", "Buzzer (D33)"};

    for (uint8_t i = 0; i < 5; i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
        Serial.print(F("  "));
        Serial.print(names[i]);
        Serial.println(F(" -> LOW (OFF)"));
    }

    Serial.println();
    Serial.println(F("  Buzzer test (short beep)..."));
    tone(PIN_BUZZER, 440, 200);
    delay(300);
    noTone(PIN_BUZZER);
    Serial.println(F("  Did you hear a beep? If not, check buzzer wiring on D33."));
}

// =============================================================================
// Analog Input Monitor
// =============================================================================

static void printAnalogInputs() {
    uint16_t btn = analogRead(PIN_BUTTON);
    uint16_t jx  = analogRead(PIN_JOY_X);
    uint16_t jy  = analogRead(PIN_JOY_Y);

    Serial.print(F("  Button(A8)="));
    Serial.print(btn);
    Serial.print(F("  JoyX(A4)="));
    Serial.print(jx);
    Serial.print(F("  JoyY(A5)="));
    Serial.print(jy);

    // Interpret
    Serial.print(F("  | btn:"));
    Serial.print(btn > 700 ? "PRESSED" : btn > 500 ? "partial" : "open");
    Serial.print(F("  joyX:"));
    Serial.print(jx < 400 ? "LEFT" : jx > 600 ? "RIGHT" : "center");
    Serial.print(F("  joyY:"));
    Serial.print(jy < 400 ? "UP" : jy > 600 ? "DOWN" : "center");

    Serial.println();
}

// =============================================================================
// DS18B20 Live Monitor
// =============================================================================

static void printDS18B20Live() {
    uint8_t count = ds18b20.getDeviceCount();
    if (count == 0) return;

    ds18b20.requestTemperatures();
    Serial.print(F("  Temps: "));
    for (uint8_t i = 0; i < count; i++) {
        float t = ds18b20.getTempCByIndex(i);
        Serial.print(F("#"));
        Serial.print(i);
        Serial.print(F("="));
        Serial.print(t, 1);
        Serial.print(F("C  "));
    }
    Serial.println();
}

// =============================================================================
// Main
// =============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) {}
    delay(1000);

    Serial.println(F("\n\n"));
    Serial.println(F("╔══════════════════════════════════════════╗"));
    Serial.println(F("║  ANAEROBIC CHAMBER — HARDWARE DISCOVERY  ║"));
    Serial.println(F("╚══════════════════════════════════════════╝"));
    Serial.println(F("  Scanning all connected hardware..."));

    Wire.begin();

    // One-time scans
    runI2CScan();
    runDS18B20Scan();
    runBMP280Test();
    runActuatorCheck();

    Serial.println(F("\n========================================"));
    Serial.println(F("  LIVE MONITOR (repeating every 1s)"));
    Serial.println(F("  Press button, move joystick, touch"));
    Serial.println(F("  DS18B20 probes to identify them."));
    Serial.println(F("========================================\n"));
}

void loop() {
    printAnalogInputs();
    printDS18B20Live();
    Serial.println();
    delay(1000);
}
