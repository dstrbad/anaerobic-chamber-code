"""Configuration for the MQTT-to-CSV data collector.

Broker, port, and output directory are env-overridable so the same code
runs both as `make collector` on the host (defaults to localhost) and
inside the docker-compose stack (compose sets MQTT_BROKER=mqtt).
"""

import os

MQTT_BROKER = os.environ.get("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "anaerobic/chamber1/bulk")
MQTT_CLIENT_ID = os.environ.get("MQTT_CLIENT_ID", "anaerobic-collector")

DATA_DIR = os.environ.get("DATA_DIR", "data")
CONTINUOUS_DIR = f"{DATA_DIR}/continuous"
RUNS_DIR = f"{DATA_DIR}/runs"

# CSV columns — order is what readers/spreadsheets see.
# Mirrors the SD card CSV produced by firmware/mega/src/logging.cpp.
CSV_COLUMNS = [
    "timestamp",
    "o2_pct", "h2_ppm", "h2s_ppm", "o3_ppm", "co2_ppm",
    "p_big_hpa", "p_small_hpa",
    "t_catalyst", "t_chamber_heater", "t_chamber",
    "catalyst_setpoint", "chamber_setpoint",
    "state", "thermal_state", "fault",
    "pump", "solenoid", "catalyst_pwm", "chamber_pwm",
    "warmup_s", "uptime_s", "purge_cycle", "purge_total",
]

# JSON key -> CSV column mapping. Keys not present in incoming JSON are
# written as empty strings (so the SP fields gracefully degrade for older
# firmware that doesn't publish them).
JSON_TO_CSV = {
    "o2": "o2_pct",
    "h2": "h2_ppm",
    "h2s": "h2s_ppm",
    "o3": "o3_ppm",
    "co2": "co2_ppm",
    "pb": "p_big_hpa",
    "ps": "p_small_hpa",
    "tc": "t_catalyst",
    "th": "t_chamber_heater",
    "ta": "t_chamber",
    "sp_cat": "catalyst_setpoint",
    "sp_chm": "chamber_setpoint",
    "st": "state",
    "ts": "thermal_state",
    "f": "fault",
    "pump": "pump",
    "sol": "solenoid",
    "cpwm": "catalyst_pwm",
    "hpwm": "chamber_pwm",
    "wu": "warmup_s",
    "up": "uptime_s",
    "pc": "purge_cycle",
    "pt": "purge_total",
}
