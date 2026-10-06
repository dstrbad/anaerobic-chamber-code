#include "display.h"
#include "config.h"
#include "heater.h"
#include <U8g2lib.h>

// SSD1306 na adresi 0x3D (potvrđeno discovery skicom)
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void display_init() {
    u8g2.setI2CAddress(I2C_DISPLAY_ADDR * 2); // U8g2 koristi 8-bitnu adresu
    u8g2.begin();
    u8g2.setFont(u8g2_font_5x7_tf);
}

// Početni ekran — prikaz svih senzora
static void renderHome(const SensorData& sensors, const SystemState& state) {
    char buf[16];

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    // Lijevi stupac: plinski senzori
    u8g2.drawStr(0, 8, "O2:");

    char buf_corr[8];
    char buf_raw[8];

    // corrected value (e.g. 0.00)
    dtostrf(sensors.o2_pct, 4, 2, buf_corr);
    u8g2.drawStr(17, 8, buf_corr);

    // separator
    u8g2.drawStr(36, 8, "|");

    // raw value (e.g. 0.60)
    dtostrf(sensors.o2_raw_pct, 4, 2, buf_raw);
    u8g2.drawStr(40, 8, buf_raw);

    // percent symbol
    u8g2.drawStr(55, 8, "%");

    u8g2.drawStr(0, 18, "H2:");
    dtostrf(sensors.h2_ppm, 5, 0, buf);
    u8g2.drawStr(20, 18, buf);

    u8g2.drawStr(0, 28, "H2S:");
    dtostrf(sensors.h2s_ppm, 4, 1, buf);
    u8g2.drawStr(24, 28, buf);

    u8g2.drawStr(0, 38, "O3:");
    dtostrf(sensors.o3_ppm, 4, 2, buf);
    u8g2.drawStr(20, 38, buf);

    u8g2.drawStr(0, 48, "CO2:");
    dtostrf(sensors.eco2_ppm, 5, 0, buf);
    u8g2.drawStr(24, 48, buf);

    // Desni stupac: tlak + temperatura
    u8g2.drawStr(68, 8, "Pb:");
    dtostrf(sensors.p_big_hpa, 6, 1, buf);
    u8g2.drawStr(88, 8, buf);

    u8g2.drawStr(68, 18, "Ps:");
    dtostrf(sensors.p_small_hpa, 6, 1, buf);
    u8g2.drawStr(88, 18, buf);

    u8g2.drawStr(68, 28, "Tc:");
    dtostrf(sensors.t_catalyst, 4, 1, buf);
    u8g2.drawStr(88, 28, buf);

    u8g2.drawStr(68, 38, "Th:");
    dtostrf(sensors.t_chamber_heater, 4, 1, buf);
    u8g2.drawStr(88, 38, buf);

    u8g2.drawStr(68, 48, "Ta:");
    dtostrf(sensors.t_chamber, 4, 1, buf);
    u8g2.drawStr(88, 48, buf);

    // Statusna traka
    u8g2.drawStr(0, 62, opsStateToString(state.ops));
    u8g2.drawStr(48, 62, thermalStateToString(state.thermal));

    if (!sensors.gas_warmed_up) {
        u8g2.drawStr(108, 62, "*WU");
    }

    if (state.fault != FaultCode::NONE) {
        u8g2.drawStr(80, 62, faultCodeToString(state.fault));
    }

    u8g2.sendBuffer();
}

// Ekran greške — prikazuje kod greške i čeka potvrdu
static void renderFault(const SystemState& state) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(10, 25, "GRESKA");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(10, 45, faultCodeToString(state.fault));
    u8g2.drawStr(10, 58, "Pritisni za potvrdu");
    u8g2.sendBuffer();
}

// Helper: iscrtaj kursor (strelicu) pored odabrane stavke
static void drawCursor(uint8_t cursor, uint8_t y0, uint8_t lineH) {
    u8g2.drawStr(0, y0 + cursor * lineH, ">");
}

// Glavni meni: Purge / Heating / Air quality / Logging
static void renderMenuRoot(const SystemState& state) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawStr(20, 8, "=== MENI ===");

    u8g2.drawStr(8, 22, "Purge");
    u8g2.drawStr(8, 34, "Grijanje");
    u8g2.drawStr(8, 46, "Kvaliteta zraka");
    u8g2.drawStr(8, 58, "Logiranje");

    drawCursor(state.menu_cursor, 22, 12);
    u8g2.sendBuffer();
}

// Purge podmeni: Mali / Veliki / Nazad
static void renderMenuPurge(const SensorData& sensors, const SystemState& state) {
    char buf[22];
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawStr(20, 8, "=== PURGE ===");

    // Ako purge traje, prikaži status umjesto menija
    if (state.ops == OpsState::PURGE_SMALL || state.ops == OpsState::PURGE_BIG) {
        const char* tip = (state.ops == OpsState::PURGE_SMALL) ? "Mali" : "Veliki";
        snprintf(buf, sizeof(buf), "%s purge", tip);
        u8g2.drawStr(8, 22, buf);

        snprintf(buf, sizeof(buf), "Ciklus: %u/%u", state.purge_cycle, state.purge_total);
        u8g2.drawStr(8, 34, buf);

        float p = (state.ops == OpsState::PURGE_SMALL) ? sensors.p_small_hpa : sensors.p_big_hpa;
        dtostrf(p, 6, 1, buf);
        u8g2.drawStr(8, 46, "P:");
        u8g2.drawStr(22, 46, buf);
        u8g2.drawStr(64, 46, "hPa");

        u8g2.drawStr(8, 58, "Dugi pritisak=STOP");
    } else {
        u8g2.drawStr(8, 22, "Pokreni mali");
        u8g2.drawStr(8, 34, "Pokreni veliki");

        // Upozorenje ako veliki purge nije moguć
        if (sensors.o2_pct > O2_BIG_PURGE_MAX_PCT && state.menu_cursor == 1) {
            u8g2.drawStr(8, 46, "! O2 previsok !");
        }

        u8g2.drawStr(8, 58, "Nazad");
        drawCursor(state.menu_cursor, 22, 12);
    }

    u8g2.sendBuffer();
}

// Edit mode setpointa — preuzima ekran, joystick gore/dolje mijenja, tipkalo sprema.
// state.editing_chamber bira između komore i katalizatora.
static void renderEditSetpoint(const SystemState& state) {
    char buf[8];
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    int sp;
    if (state.editing_chamber) {
        u8g2.drawStr(8, 8, "== KOMORA SETPOINT ==");
        sp = (int)(heater_getChamberSetpoint() + 0.5f);
    } else {
        u8g2.drawStr(2, 8, "== KATALIZATOR SP ==");
        sp = (int)(heater_getCatalystSetpoint() + 0.5f);
    }

    u8g2.setFont(u8g2_font_ncenB14_tr);
    snprintf(buf, sizeof(buf), "%dC", sp);
    u8g2.drawStr(46, 36, buf);

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(8, 50, "gore/dolje = +/- 1");
    u8g2.drawStr(8, 60, "tipkalo=sprmi  dugi=ponist");
    u8g2.sendBuffer();
}

// Grijanje podmeni: Kat toggle / Kom toggle / SP kat / SP kom / Nazad
// 5 stavki na 64px ekranu — koristimo 10px razmak između redova.
static void renderMenuHeating(const SensorData& sensors, const SystemState& state) {
    if (state.editing_setpoint) {
        renderEditSetpoint(state);
        return;
    }

    char buf[8];
    char line[24];
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawStr(14, 8, "=== GRIJANJE ===");

    bool catOn = (state.thermal == ThermalState::HEAT_CATALYST ||
                  state.thermal == ThermalState::HEAT_BOTH);
    dtostrf(sensors.t_catalyst, 4, 1, buf);
    snprintf(line, sizeof(line), "Kat %s %s SP=%d", catOn ? "ON " : "OFF",
             buf, (int)(heater_getCatalystSetpoint() + 0.5f));
    u8g2.drawStr(8, 18, line);

    bool chmOn = (state.thermal == ThermalState::HEAT_CHAMBER ||
                  state.thermal == ThermalState::HEAT_BOTH);
    dtostrf(sensors.t_chamber, 4, 1, buf);
    snprintf(line, sizeof(line), "Kom %s %s SP=%d", chmOn ? "ON " : "OFF",
             buf, (int)(heater_getChamberSetpoint() + 0.5f));
    u8g2.drawStr(8, 28, line);

    u8g2.drawStr(8, 38, "Postavi SP kat.");
    u8g2.drawStr(8, 48, "Postavi SP komore");
    u8g2.drawStr(8, 58, "Nazad");

    drawCursor(state.menu_cursor, 18, 10);
    u8g2.sendBuffer();
}

// Kvaliteta zraka — prikazuje AQI/TVOC/eCO2 dijagnostiku
static void renderMenuAirQuality(const SensorData& sensors, const SystemState& state) {
    char buf[16];
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawStr(6, 8, "=== KVALITETA ZRAKA ===");

    u8g2.drawStr(0, 22, "O2:");
    dtostrf(sensors.o2_pct, 6, 2, buf);
    u8g2.drawStr(20, 22, buf);
    u8g2.drawStr(58, 22, "%");

    u8g2.drawStr(0, 32, "H2:");
    dtostrf(sensors.h2_ppm, 6, 0, buf);
    u8g2.drawStr(20, 32, buf);
    u8g2.drawStr(58, 32, "ppm");

    u8g2.drawStr(0, 42, "H2S:");
    dtostrf(sensors.h2s_ppm, 5, 1, buf);
    u8g2.drawStr(24, 42, buf);
    u8g2.drawStr(58, 42, "ppm");

    u8g2.drawStr(0, 52, "O3:");
    dtostrf(sensors.o3_ppm, 5, 2, buf);
    u8g2.drawStr(20, 52, buf);
    u8g2.drawStr(58, 52, "ppm");

    u8g2.drawStr(0, 62, "eCO2:");
    dtostrf(sensors.eco2_ppm, 5, 0, buf);
    u8g2.drawStr(30, 62, buf);
    u8g2.drawStr(68, 62, "ppm");

    if (!sensors.gas_warmed_up) {
        u8g2.drawStr(90, 62, "*WU");
    } else {
        u8g2.drawStr(92, 62, "[OK]");
    }

    u8g2.sendBuffer();
}

// Logiranje podmeni: Aktivno ON/OFF, Nazad
static void renderMenuLogging(const SystemState& state) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tf);

    u8g2.drawStr(10, 8, "=== LOGIRANJE ===");

    char line[22];
    snprintf(line, sizeof(line), "Aktivno log: %s", state.logging_active ? "ON" : "OFF");
    u8g2.drawStr(8, 22, line);

    u8g2.drawStr(8, 34, "Nazad");

    if (state.logging_active) {
        u8g2.drawStr(8, 50, "Interval: 60s");
        u8g2.drawStr(8, 60, "Trajanje: 4h");
    } else {
        u8g2.drawStr(8, 50, "Pasivno: 1h interval");
    }

    drawCursor(state.menu_cursor, 22, 12);
    u8g2.sendBuffer();
}

void display_render(const SensorData& sensors, const SystemState& state) {
    if (state.ops == OpsState::FAULT || state.thermal == ThermalState::HEAT_FAULT) {
        renderFault(state);
        return;
    }

    switch (state.ui) {
        case UIState::HOME:
            renderHome(sensors, state);
            break;
        case UIState::MENU_ROOT:
            renderMenuRoot(state);
            break;
        case UIState::MENU_PURGE:
            renderMenuPurge(sensors, state);
            break;
        case UIState::MENU_HEATING:
            renderMenuHeating(sensors, state);
            break;
        case UIState::MENU_AIRQUALITY:
            renderMenuAirQuality(sensors, state);
            break;
        case UIState::MENU_LOGGING:
            renderMenuLogging(state);
            break;
    }
}
