#include "logging.h"
#include "config.h"
#include "heater.h"

#include <SPI.h>
#include <SD.h>

// Logiranje na SD karticu — kružna povijest po datotekama.
//
// Imenovanje: LOGNNNN.CSV (8.3, sekvencijalni indeks). Pri svakom paljenju
// sustav skenira SD karticu, pronalazi najveći postojeći indeks i kreira
// novi (index + 1). Datoteka rotira kad pređe LOG_FILE_MAX_BYTES, najstarije
// se brišu kad ih je više od LOG_FILE_RETENTION.
//
// Učestalost zapisa:
//   - aktivno (zadano UKLJUČENO): LOG_ACTIVE_INTERVAL_MS po retku (~10s)
//   - pasivno (kad korisnik isključi): LOG_PASSIVE_INTERVAL_MS (~1h)
// Korisnik upravlja preko OLED menija ili MQTT komande "log".
//
// Vremenska oznaka u CSV-u je millis(). Pri restartu Mege kreće od 0, pa
// svaka datoteka u biti predstavlja jednu sesiju (web UI to uvažava).

namespace {

bool     sd_available    = false;
uint16_t current_index   = 0;
uint32_t current_size    = 0;
uint32_t last_log_ms     = 0;
File     current_log_file;

void format_filename(char* out, size_t n, uint16_t idx) {
    snprintf_P(out, n, PSTR("LOG%04u.CSV"), idx);
}

uint16_t scan_highest_index() {
    File root = SD.open("/");
    if (!root) return 0;
    uint16_t highest = 0;
    while (true) {
        File f = root.openNextFile();
        if (!f) break;
        const char* name = f.name();
        if (name && strncmp_P(name, PSTR("LOG"), 3) == 0) {
            uint16_t v = (uint16_t)atoi(name + 3);
            if (v > highest) highest = v;
        }
        f.close();
    }
    root.close();
    return highest;
}

void prune_old_files() {
    if (current_index <= LOG_FILE_RETENTION) return;
    uint16_t cutoff = current_index - LOG_FILE_RETENTION;
    char name[16];
    for (uint16_t i = 1; i <= cutoff; ++i) {
        format_filename(name, sizeof(name), i);
        if (SD.exists(name)) SD.remove(name);
    }
}

void write_header(File& f) {
    f.print(F("ms,o2,h2,h2s,o3,co2,pb,ps,tc,th,ta,sp_cat,sp_chm,"
              "st,ts,f,pump,sol,cpwm,hpwm,wu,up,pc,pt\n"));
}

bool start_new_file() {
    if (current_log_file) {
        current_log_file.close();
    }

    current_index += 1;
    char name[16];
    format_filename(name, sizeof(name), current_index);
    current_log_file = SD.open(name, FILE_WRITE);
    if (!current_log_file) return false;

    write_header(current_log_file);
    current_size = current_log_file.size();
    prune_old_files();
    return true;
}

void write_row(const SensorData& s, const SystemState& st) {
    if (!current_log_file) return;

    current_log_file.print(millis());           current_log_file.print(',');
    current_log_file.print(s.o2_pct, 2);        current_log_file.print(',');
    current_log_file.print(s.h2_ppm, 0);        current_log_file.print(',');
    current_log_file.print(s.h2s_ppm, 2);       current_log_file.print(',');
    current_log_file.print(s.o3_ppm, 2);        current_log_file.print(',');
    current_log_file.print(s.eco2_ppm, 0);      current_log_file.print(',');
    current_log_file.print(s.p_big_hpa, 1);     current_log_file.print(',');
    current_log_file.print(s.p_small_hpa, 1);   current_log_file.print(',');
    current_log_file.print(s.t_catalyst, 1);    current_log_file.print(',');
    current_log_file.print(s.t_chamber_heater, 1); current_log_file.print(',');
    current_log_file.print(s.t_chamber, 1);     current_log_file.print(',');
    current_log_file.print((int)(heater_getCatalystSetpoint() + 0.5f)); current_log_file.print(',');
    current_log_file.print((int)(heater_getChamberSetpoint() + 0.5f));  current_log_file.print(',');
    current_log_file.print(opsStateToString(st.ops));         current_log_file.print(',');
    current_log_file.print(thermalStateToString(st.thermal)); current_log_file.print(',');
    current_log_file.print(faultCodeToString(st.fault));      current_log_file.print(',');
    current_log_file.print(st.pump_on ? 1 : 0); current_log_file.print(',');
    current_log_file.print(st.solenoid_on ? 1 : 0); current_log_file.print(',');
    current_log_file.print(st.catalyst_pwm);    current_log_file.print(',');
    current_log_file.print(st.chamber_pwm);     current_log_file.print(',');
    current_log_file.print(s.warmup_remaining_s); current_log_file.print(',');
    current_log_file.print(st.uptime_s);        current_log_file.print(',');
    current_log_file.print(st.purge_cycle);     current_log_file.print(',');
    current_log_file.print(st.purge_total);     current_log_file.print('\n');

    current_size = (uint32_t)current_log_file.size();

    static uint8_t flush_counter = 0;
    if (++flush_counter >= 10) {
        current_log_file.flush();
        flush_counter = 0;
    }
}

}  // namespace

void logging_init() {
    if (!SD.begin(PIN_SD_CS)) {
        Serial.println(F("SD kartica init neuspjeh — logiranje onemogućeno"));
        sd_available = false;
        return;
    }
    sd_available = true;
    current_index = scan_highest_index();
    Serial.print(F("SD: zadnji indeks = ")); Serial.println(current_index);
    if (!start_new_file()) {
        Serial.println(F("SD: kreiranje datoteke neuspješno"));
        sd_available = false;
        return;
    }
    Serial.print(F("SD: nova sesija = LOG"));
    Serial.print(current_index);
    Serial.println(F(".CSV"));
}

void logging_tick(const SensorData& sensors, const SystemState& state) {
    if (!sd_available) return;

    const uint32_t now = millis();

    const uint32_t interval = state.logging_active
        ? LOG_ACTIVE_INTERVAL_MS
        : LOG_PASSIVE_INTERVAL_MS;

    // Prvo pokretanje: zapiši red odmah da datoteka ne ostane prazna.
    if (last_log_ms != 0 && (now - last_log_ms) < interval) return;
    last_log_ms = now;

    write_row(sensors, state);

    if (current_size >= LOG_FILE_MAX_BYTES) {
        if (!start_new_file()) {
            Serial.println(F("SD: rotacija neuspješna, logiranje stoji"));
            sd_available = false;
        }
    }
}
