# Anaerobic Chamber Control System

Firmware, data collector and live dashboard for an experimental anaerobic chamber used in biology research. An Arduino Mega 2560 reads the gas sensors (O2, H2, H2S, O3, CO2), pressure and temperature, runs the purge and heating cycles, and enforces every safety interlock. An ESP32 forwards the data over MQTT to a Python collector that writes CSV files and to a browser dashboard with live Plotly charts.

[Hrvatska verzija](README.hr.md)

> **Safety note.** The chamber has no pressure relief valves. Software is the last line of defence before the hardware thermal cutoffs. Read [Safety model](#safety-model) before touching any actuator logic.

## How the pieces fit together

```
                        Serial (JSON, 1 Hz)              MQTT (WiFi)
┌──────────────────┐      115200 baud       ┌─────────┐            ┌──────────────────┐
│                  │ ◄────────────────────►  │         │ ◄────────► │ Mosquitto broker │
│  Arduino Mega    │    TX1/RX1 ↔ RX1/TX1   │  ESP32  │            │   (TCP + WS)     │
│  2560            │                         │         │            └────────┬─────────┘
│                  │                         └─────────┘                     │
│  - FSM control   │                                                ┌───────┴───────┐
│  - Sensor reads  │                                                │               │
│  - Safety layers │                                          ┌─────┴─────┐   ┌─────┴──────┐
│  - OLED display  │                                          │  Python   │   │    Web     │
│  - Actuators     │                                          │ collector │   │ dashboard  │
│  - SD logging    │                                          │  (CSV)    │   │ (Plotly)   │
└──────────────────┘                                          └───────────┘   └────────────┘
```

Data flows from sensors to the Mega, over serial JSON to the ESP32, over MQTT to the broker, and from there to both the collector and the dashboard. Commands go the other way: dashboard, MQTT, ESP32, serial, Mega, where the safety module validates them before anything moves.

## Design decisions

**Mega plus ESP32 rather than ESP32 alone.** The Mega does the real-time control with deterministic timing. The ESP32 WiFi stack has background tasks (reconnects, DHCP, TLS) that can stall for hundreds of milliseconds, which is unacceptable when the pressure watchdog has to cut a pump within a second or two. With the split, a WiFi dropout never affects chamber safety; the Mega keeps running with the ESP32 unplugged.

**Three orthogonal state machines instead of one.** Operations (purge cycles), thermal control (heaters) and the user interface (display and menus) are independent. One combined FSM would need well over a hundred states. With three machines of four to six states each, "purging while heating" is just `OpsState::PURGE_SMALL` together with `ThermalState::HEAT_CATALYST`.

**Timer-driven sampling.** Hardware timers on the ATmega2560 fix the sampling rates regardless of main loop load: pressure at 10 Hz (Timer1), gas sensors at 1 Hz (Timer3), display at 20 Hz (Timer4). The ISRs only set flags; the actual I2C reads happen in the main loop when the flag is seen. ISRs stay under a microsecond and reads land at precise intervals.

**Four safety layers.** A single safety module in the main loop can fail along with the loop (I2C bus lock-up, infinite loop). Four independent layers mean no single fault can leave an actuator on. Details below.

**Line-delimited JSON over serial.** At 1 Hz and roughly 300 bytes per frame, bandwidth is not a concern. JSON is self-describing, so new sensor fields can be added without coordinating firmware versions, corrupt frames fail to parse and are dropped, and anyone can debug the link with a serial monitor.

**A separate Python collector.** MQTT keeps no history; `retain` stores only the last message per topic. If the browser tab is closed, the data is gone. The collector runs as a background service and logs every message to CSV independently of the dashboard.

**Plotly.js, plain HTML and JS.** Plotly handles time axes, zoom, hover and multiple y-axes natively and supports efficient live updates with `extendTraces()`. The dashboard is a single HTML page loaded from a CDN, so the firmware engineer maintaining it does not need a frontend build chain.

## Hardware

### Controllers and communication

| Component | Role | Interface |
|-----------|------|-----------|
| Arduino Mega 2560 | Main controller (FSMs, sensors, actuators, display) | USB |
| Arduino Nano ESP32 | WiFi/MQTT bridge | Serial1 to Mega Serial1, 115200 baud |
| TCA9548A | I2C multiplexer for the BMP280 sensors and gas sensors | I2C 0x70 |

### Sensors

| Channel | Device | Interface | Address / bus | Units |
|---------|--------|-----------|---------------|-------|
| O2 | DFRobot SEN0465 | I2C via mux ch7 | 0x74 | % vol |
| H2 | DFRobot SEN0473 | I2C via mux ch7 | 0x75 | ppm (0 to 1000) |
| H2S | DFRobot SEN0467 | I2C via mux ch7 | 0x76 | ppm |
| O3 | DFRobot SEN0472 | I2C via mux ch7 | 0x77 | ppm |
| Air quality / eCO2 | DFRobot SEN0514 (ENS160) | I2C via mux ch7 | 0x53 | AQI, ppm |
| P_big (main chamber) | BMP280 | I2C via mux ch2 | 0x77 | hPa |
| P_small (transfer chamber) | BMP280 | I2C via mux ch4 | 0x77 | hPa |
| T_catalyst, T_heater, T_chamber | DS18B20 x3 | 1-Wire on D32 | ROM IDs in `config.h` | °C |

### Actuators

| Channel | Device | Pin | Control |
|---------|--------|-----|---------|
| Vacuum pump | SSR to 230 V pump | D22 | On/off |
| Solenoid valve | Relay to H2 valve | D23 | On/off |
| Catalyst heater | IRLZ44N MOSFET | D44 | PWM |
| Chamber heater | IRLZ44N MOSFET | D45 | PWM |

### UI and storage

| Component | Pins | Notes |
|-----------|------|-------|
| OLED 128x64 SSD1306 | I2C 0x3D (direct bus) | SparkFun Qwiic |
| Joystick | A4, A5 | Analog |
| Push button | A8 | Analog |
| Buzzer | D33 | Software tone |
| MicroSD (DFR0229) | SPI, CS on D53 | CSV logging |

## Safety model

Any one of the four layers is enough to stop an actuator from running away. If you change actuator control logic you need to understand all four.

**Layer 0, hardware thermal cutoffs.** Physical thermal switches on both heaters cut power with no software involved.

**Layer 1, watchdog timer.** The ATmega2560 WDT is set to 1 s and reset at the end of every `loop()`. If the loop hangs for any reason the MCU hard-resets, all GPIO returns to its default low state, and every actuator turns off.

**Layer 2, Timer5 safety ISR at 50 Hz.** Runs independently of the main loop. It cannot read sensors (no I2C in an ISR) but checks the cached values the main loop writes and drives actuator pins directly through port registers:

- Hard thermal limits: catalyst above 60 °C or chamber above 45 °C forces the heater pin low.
- Hard overpressure limit forces the solenoid pin low.
- Actuator timeouts: each actuator records when it was switched on. The main loop must call `refreshTimeout()` every iteration to prove it still wants it on. If the timestamp goes stale (for example 90 s for the pump) the ISR turns it off. This is the FSM liveness guard.
- FSM heartbeat: each FSM stamps a heartbeat every iteration. If either FSM has not ticked in 750 ms, all actuators go off.

**Layer 3, the `safety.cpp` module in the main loop.** The only layer with full context. It sets fault codes, drives FSM transitions and logs:

- Pressure response watchdog: pump or valve on for more than 1.5 s with no measurable pressure change raises a fault.
- Overpressure: P above reference plus 50 hPa raises an alarm; above 100 hPa closes the valve and raises a fault.
- Overtemperature: catalyst above 60 °C or heater above 45 °C turns the heater off and raises a thermal fault.
- DS18B20 timeout: an invalid reading for more than 5 s while a heater is active turns it off.
- O2 gate: a large-chamber purge is blocked if O2 is above 3 % (explosion risk with H2 over a palladium catalyst).
- Global abort: a long button press of about one second turns off pump, valve and both heaters at once.

### Fault matrix

| Fault | L0 hardware | L1 WDT | L2 ISR (50 Hz) | L3 loop |
|-------|-------------|--------|----------------|---------|
| Main loop hangs | | MCU reset after 1 s, all off | Actuators off | Dead |
| FSM stuck in a state | | Reset if loop also stalls | Caught within 750 ms | |
| Sensor returns bad data | | | Checks cached values | Detects, raises fault |
| Bug leaves actuator on | | | Timeout turns it off | Should have caught it first |
| Heater runaway | Thermal cutoff | | Heater pin forced low | Off plus fault |
| Total MCU failure | Thermal cutoff | | | |

## Getting started

You need the [PlatformIO CLI](https://platformio.org/install/cli) or the VS Code extension, a [Mosquitto](https://mosquitto.org/download/) broker (or Docker), Python 3.8 or newer, and the two boards on USB.

### 1. Map the hardware

Flash the discovery sketch first. It scans the I2C bus and every mux channel, lists DS18B20 ROM IDs, reads both BMP280s and prints analog inputs so you can confirm wiring before running the real firmware.

```bash
make discovery
```

Copy the DS18B20 ROM IDs it prints into `firmware/mega/src/config.h` and map them to probes by warming each one in turn.

### 2. Flash the Mega

```bash
make flash-mega
make monitor-mega
```

### 3. Configure and flash the ESP32

```bash
cp firmware/esp32/src/secrets.example.h firmware/esp32/src/secrets.h
# edit secrets.h: WiFi SSID, password, broker IP
make flash-esp32
make monitor-esp32
```

`secrets.h` is ignored by git. Never put credentials in `config.h`.

### 4. Start the broker, collector and dashboard

The quickest route is Docker, which brings up nginx on 8080, Mosquitto on 1883 and 9001, and the collector with its templates API on 8000:

```bash
make docker-up
```

Then open <http://localhost:8080>. Append `?demo` to the URL to see the dashboard with mock data and no hardware.

Without Docker: add `listener 9001` with `protocol websockets` and `allow_anonymous true` to your `mosquitto.conf`, then

```bash
make collector-install && make collector
make web
make mqtt-sub        # watch the raw topics
make mqtt-test       # publish one fake frame
```

CSV files land in `collector/data/continuous/` (weekly) and `collector/data/runs/` (one file per purge cycle at full 1 Hz).

## Repository layout

```
platformio.ini              PlatformIO envs: mega, esp32, discovery
Makefile                    Build, flash, monitor, Docker and MQTT helpers
docker-compose.yml          web + mqtt + collector dev stack
docker/mosquitto.conf
firmware/
  mega/src/                 Arduino Mega 2560 firmware
    main.cpp                setup()/loop(), wires the modules together
    config.h                Pins, I2C addresses, timing, safety thresholds
    timers.*                Timer1/3/4 sampling flags, Timer5 safety ISR
    fsm.*                   Operations FSM (INIT, IDLE, PURGE_SMALL, PURGE_BIG, FAULT)
    safety.*                Layer 3 watchdogs and interlocks
    sensors.*               BMP280, DS18B20, DFRobot gas sensors
    actuators.*             Pump, valve, heaters, timeout tracking
    purge.*                 Purge cycle sub-states
    heater.*                Thermal FSM, bang-bang control
    display.*, input.*      OLED pages, joystick and button
    logging.*               SD card CSV
    serial_comm.*, serial_rx.*  JSON to and from the ESP32
  esp32/src/                WiFi/MQTT bridge (FreeRTOS tasks)
    config.h                Non-secret settings; includes secrets.h
    secrets.example.h       Template for WiFi and broker credentials
helpers/
  hardware_discovery.cpp    Bring-up sketch (env: discovery)
  relay_test.cpp            Minimal relay pulse test
collector/                  Python MQTT-to-CSV collector + templates HTTP API
web/                        Dashboard: index.html, css/, js/ (no build step)
```

## Configuration

Mega: everything is in `firmware/mega/src/config.h`. Defaults worth knowing: 7 small-purge cycles at 400 hPa drop each, 7 large-purge cycles at 50 hPa each, catalyst setpoint 50 °C (cutoff 60 °C), chamber setpoint 37 °C (cutoff 45 °C), 5 s DS18B20 timeout, 1.5 s pressure watchdog window, 600 s gas sensor warm-up.

ESP32: credentials and broker address in `secrets.h`; port, topic prefix (default `anaerobic/chamber1/`) and client ID in `config.h`.

Collector: `MQTT_BROKER`, `MQTT_PORT`, `MQTT_TOPIC`, `DATA_DIR`, `TEMPLATES_PATH`, `TEMPLATES_PORT` are read from the environment, with localhost defaults. Docker Compose sets `MQTT_BROKER=mqtt`.

## MQTT topics

All topics sit under the configurable prefix, default `anaerobic/chamber1/`.

| Topic | Type | Retained | Meaning |
|-------|------|----------|---------|
| `sensor/o2`, `sensor/h2`, `sensor/h2s`, `sensor/o3`, `sensor/co2` | float | yes | Gas readings |
| `sensor/pressure_big`, `sensor/pressure_small` | float | yes | hPa |
| `sensor/temp_catalyst`, `sensor/temp_heater`, `sensor/temp_chamber` | float | yes | °C |
| `status/state` | string | yes | `INIT`, `IDLE`, `PURGE_S`, `PURGE_B`, `FAULT` |
| `status/thermal` | string | yes | `OFF`, `CAT`, `CHM`, `BOTH`, `FAULT` |
| `status/fault` | string | yes | `NONE`, `NO_DP`, `OVERP`, `OT_CAT`, `OT_CHM`, `TS_CAT`, `TS_CHM`, `O2_HI`, `ISR`, `ABORT` |
| `status/purge_progress` | JSON | yes | `{"current": 3, "total": 7}` |
| `status/uptime`, `status/warmup` | int | yes | Seconds |
| `actuator/pump`, `actuator/solenoid` | int | yes | 0 or 1 |
| `actuator/catalyst_pwm`, `actuator/chamber_pwm` | int | yes | 0 to 255 |
| `command/purge` | string | no | `small` or `big` |
| `command/heat` | JSON | no | `{"target": "catalyst", "sp": 50}` |
| `command/abort` | any | no | Emergency stop |
| `bulk` | JSON | yes | Full frame, same as the serial JSON |

## Data logging

SD card on the Mega: files are named `LOG0000.CSV`, `LOG0001.CSV` and so on, and a new file starts when the current one passes 64 KB. Active mode (default on) writes a row every 10 s for four hours; passive mode writes one row per hour. An SD failure shows a warning on the display and does not stop the system.

Collector: every frame appended to a weekly CSV (Monday to Sunday), plus a separate full-resolution file per purge run opened on the transition into a purge state and closed when the state returns to IDLE or FAULT.

CSV header (shared by SD and collector):

```
timestamp,o2_pct,h2_ppm,h2s_ppm,o3_ppm,co2_ppm,p_big_hpa,p_small_hpa,t_catalyst,t_chamber_heater,t_chamber,catalyst_setpoint,chamber_setpoint,state,thermal_state,fault,pump,solenoid,catalyst_pwm,chamber_pwm,warmup_s,uptime_s,purge_cycle,purge_total
```

## Status and limitations

This is a working prototype built for one chamber in one lab, not a product. The Mosquitto config allows anonymous access and the templates API has open CORS; both are fine on a workstation and wrong for anything reachable from outside. Hardware details (sensor addresses, ROM IDs, pin assignments) are specific to this build and will need to be re-mapped with the discovery sketch on different hardware. Firmware comments are partly in Croatian.

## License

MIT, see [LICENSE](LICENSE).
