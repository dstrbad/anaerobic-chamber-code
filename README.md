# Anaerobic Chamber Control System

Firmware, data collector and live dashboard for an experimental anaerobic chamber used in biology research. An Arduino Mega 2560 reads the gas sensors (O2, H2, H2S, O3, CO2), pressure and temperature, runs the purge and heating cycles, and enforces every safety interlock. An ESP32 forwards the data over MQTT to a Python collector that writes CSV files and a history database, and to a browser dashboard with live Plotly charts.

This repository accompanies a HardwareX publication; see [Citation](#citation). The exact version described in the paper is tagged `hardwarex-v1.0`.

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

**Timer-driven sampling.** Hardware timers on the ATmega2560 fix the sampling rates regardless of main loop load: pressure at 10 Hz (Timer1), gas sensors, temperatures and telemetry at 1 Hz (Timer3), UI tick at 20 Hz (Timer4, the OLED redraws at 5 Hz or immediately after input). Timer5 runs the 50 Hz safety ISR and also generates the heater PWM. The flag ISRs only set flags; the actual I2C reads happen in the main loop when the flag is seen. Nothing in the main loop blocks for long: gas sensors are queried and their answers collected about 100 ms later on a later pass, instead of waiting inside the library.

**Four safety layers.** A single safety module in the main loop can fail along with the loop (I2C bus lock-up, infinite loop). Four independent layers mean no single fault can leave an actuator on. Details below.

**Line-delimited JSON over serial.** At 1 Hz and roughly 300 bytes per frame, bandwidth is not a concern. JSON is self-describing, so new sensor fields can be added without coordinating firmware versions, corrupt frames fail to parse and are dropped, and anyone can debug the link with a serial monitor.

**A separate Python collector.** MQTT keeps no history; `retain` stores only the last message per topic. If the browser tab is closed, the data is gone. The collector runs as a background service and logs every message to CSV and to a SQLite history database independently of the dashboard. The dashboard loads its history from the collector, so it opens with the last hours or days already drawn.

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

**Layer 1, watchdog timer.** The ATmega2560 WDT runs with an 8 s timeout during `setup()` and 2 s in the main loop, and is reset at the end of every `loop()`. If the loop hangs for any reason the MCU hard-resets. On reset every pin goes high-impedance and the gate pull-downs hold the actuators off; `setup()` then drives all actuator pins low first. A WDT reset cannot turn into a reset loop: the first instructions after reset (`.init3`) save the reset cause and disable the watchdog.

**Layer 2, Timer5 safety ISR at 50 Hz.** Runs independently of the main loop. It cannot read sensors (no I2C in an ISR) but checks the cached values the main loop writes and drives actuator pins directly through port registers:

- Hard thermal limits: catalyst above 65 °C or chamber heater above 60 °C forces the heater pin low. Timer5 also generates the heater PWM, so the ISR first disconnects the PWM from the pin and then pulls it low.
- Hard overpressure limit (same per-chamber limits as Layer 3) forces the solenoid pin low.
- Actuator timeouts: each actuator records when it was switched on. The main loop must refresh it every iteration to prove it still wants it on. If the timestamp goes stale (90 s) the ISR turns it off.
- FSM heartbeat: each FSM stamps a heartbeat every iteration. If either FSM has not ticked in 750 ms, all actuators go off.

**Layer 3, the `safety.cpp` module in the main loop.** The only layer with full context. It sets fault codes and drives FSM transitions. In the `FAULT` state the pump and valve are forced off on every pass, whatever raised the fault.

- Pressure response watchdog, per chamber: while the pump or valve runs, the purged chamber must change by at least 5 hPa per 1.5 s (small) or 2 hPa per 3 s (big), otherwise `NO_DP`. The other chamber must stay within 3 hPa over the same window; if it moves, a valve is open on the wrong chamber and `X_CHM` stops the pump and valve.
- Overpressure: closes the valve and raises `OVERP`. Always above 1043 hPa absolute (1013.25 + 30), which weather never reaches, so a leaking valve or heating cannot over-inflate the gloves. During a purge the purged chamber is held to its own pressure at purge start + 25 hPa (never closer than 5 hPa to its own refill target, never above 1043).
- Pressure-sensor loss: if the pump or valve runs and either pressure sensor has given no valid reading for 0.5 s, both stop (`PS_LOST`). Failed reads keep the last good value rather than a garbage one.
- Purge phase timeout: an evacuation or refill phase that has not reached its target within 90 s stops the purge (`NO_DP`).
- Overtemperature: catalyst above 65 °C or chamber heater above 60 °C turns the heater off and raises a thermal fault (`OT_CAT`, `OT_CHM`). The chamber heater is glued to the bottom aluminium panel, so its controller never targets more than 55 °C there.
- DS18B20 timeout: an invalid reading for more than 5 s while a heater is active turns it off (`TS_CAT`, `TS_CHM`).
- Timer5 self-check: if the safety timer is stopped or reconfigured, or its ISR stops running for 100 ms, everything turns off (`TMR5`).
- O2 gate: a large-chamber purge is blocked unless the O2 reading is trustworthy and low: the gas sensors must have finished warming up, the O2 sensor must have given a valid reading within the last 5 s (`o2v` = 1 in the frame), and O2 must be at most 3 % (explosion risk with H2 over a palladium catalyst). A broken or missing O2 sensor therefore blocks the big purge instead of reading as 0 %.
- Emergency stop: a long button press (about 1 s) or the dashboard Abort button. During a purge it stops the pump and valve and keeps the heaters on (a long press returns to IDLE, a dashboard abort latches `ABORT`); outside a purge it turns everything off and latches `ABORT`.

All faults latch and are cleared only by a button press on the device; remote commands cannot clear them.

**Stall resistance.** The I2C bus has a 25 ms timeout and is clocked free (9 SCL pulses) at start-up and after any timeout, so a glitched sensor cannot hang the Mega. The ESP32 runs its tasks under a 30 s task watchdog, never blocks on USB serial output, and restarts itself if WiFi or MQTT has been down for 2 minutes. Both boards report why they last reset: the Mega sends `rst` (MCUSR), `boots` and `i2c` (I2C error count) in every frame, the ESP32 publishes `status/esp_reset`.

### Fault matrix

| Fault | L0 hardware | L1 WDT | L2 ISR (50 Hz) | L3 loop |
|-------|-------------|--------|----------------|---------|
| Main loop hangs | | MCU reset after 2 s, all off | Actuators off | Dead |
| FSM stuck in a state | | Reset if loop also stalls | Caught within 750 ms | |
| Sensor returns bad data | | | Checks cached values | Detects, raises fault |
| Bug leaves actuator on | | | Timeout turns it off | Should have caught it first |
| Heater runaway | Thermal cutoff | | Heater pin forced low | Off plus fault |
| Total MCU failure | Thermal cutoff | | | |

## Heating control

Both heaters are driven with real PWM at 50 Hz (Timer5) and follow a ramped target. The heater sensors lag behind the heating elements, so stepping straight to the final temperature overshoots by several degrees. The ramp lets the sensor keep up. Ramps only limit rising targets; lowering a setpoint or switching a heater off acts immediately.

- **Catalyst** (single loop, the sensor sits on the catalyst). The target rises from the current temperature towards the setpoint (4 °C/min, then 2 °C/min for the last 5 °C), and a PI controller sets the PWM. At setpoint + 1 °C the heater is off regardless. Setpoint range 40–58 °C, cutoff 65 °C.
- **Chamber** (cascade, because the heater is a panel under the chamber and the setpoint is the air temperature). An outer PI loop compares the chamber air with the setpoint every 10 s and chooses a panel temperature, learning how much warmer the panel has to be. An inner PI loop drives the panel there. The panel target is capped at 55 °C (cutoff 60 °C) and ramps at 2 °C/min, then 0.6 °C/min for the last 5 °C below the cap. The learned offset is stored in EEPROM, so restarts begin from the last good value. If the panel target is stuck at 55 °C while the chamber is still below its setpoint, `th_lim` is 1 and the OLED shows `H55!`: the setpoint is not reachable with the allowed panel temperature.

The targets are published as `tc_sp` and `th_sp` for tuning. Gains and ramp rates are at the top of the catalyst and chamber sections in `heater.cpp`.

## Sensor corrections

- **O2:** a fixed zero offset (`O2_ZERO_OFFSET_PCT`, 0.6 %) is subtracted; values below 0.05 % are shown as 0. The uncorrected value is shown next to it on the OLED.
- **H2:** the SEN0473 zero is not 0 and rises with temperature. The firmware subtracts zero(T) = `H2_ZERO_PPM` + `H2_ZERO_TC_PPM_PER_C` × (chamber air temperature − `H2_ZERO_REF_C`) and publishes the uncorrected reading as `h2r`. To calibrate, warm the chamber without H2 and read a cold and a warm point from `h2r` and `ta` (formula in `config.h`). The DFRobot library's own H2 temperature compensation is not used: as written it returns 0 below 40 °C.
- **Pressure:** each BMP280 is read with its own factory calibration. `P_BIG_OFFSET_HPA` and `P_SMALL_OFFSET_HPA` correct the remaining offset against a reference. A reading only counts if the sensor answered, no I2C timeout occurred and the value is between 300 and 1200 hPa.

## Getting started

You need the [PlatformIO CLI](https://platformio.org/install/cli) or the VS Code extension, a [Mosquitto](https://mosquitto.org/download/) broker (or Docker), Python 3.10 or newer (only when running the collector without Docker), and the two boards on USB.

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

The quickest route is Docker, which brings up nginx on 8080, Mosquitto on 1883 and 9001, and the collector with its templates and history API on 8000:

```bash
make docker-up
```

After changing collector or dashboard code, rebuild the images with `docker compose up -d --build`; both images copy their code at build time.

Then open <http://localhost:8080>. Append `?demo` to the URL to see the dashboard with mock data and no hardware.

Without Docker: add `listener 9001` with `protocol websockets` and `allow_anonymous true` to your `mosquitto.conf`, then

```bash
make collector-install && make collector
make web
make mqtt-sub        # watch the raw topics
make mqtt-test       # publish one fake frame
```

CSV files land in `collector/data/continuous/` (weekly) and `collector/data/runs/` (one file per purge cycle at full 1 Hz); the history database is `collector/data/history.sqlite3`.

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
    timers.*                Timer1/3/4 sampling flags, Timer5 safety ISR + heater PWM
    fsm.*                   Operations FSM (INIT, IDLE, PURGE_SMALL, PURGE_BIG, FAULT)
    safety.*                Layer 3 watchdogs and interlocks
    sensors.*               BMP280, DS18B20, DFRobot gas sensors
    actuators.*             Pump, valve, heaters, timeout tracking
    purge.*                 Purge cycle sub-states
    heater.*                Thermal control: catalyst ramp + PI, chamber cascade PI
    display.*, input.*      OLED pages, joystick and button
    logging.*               SD card CSV
    serial_comm.*, serial_rx.*  JSON to and from the ESP32
  esp32/src/                WiFi/MQTT bridge (FreeRTOS tasks)
    config.h                Non-secret settings; includes secrets.h
    secrets.example.h       Template for WiFi and broker credentials
helpers/
  hardware_discovery.cpp    Bring-up sketch (env: discovery)
  relay_test.cpp            Minimal relay pulse test
collector/                  Python MQTT-to-CSV/SQLite collector + templates/history HTTP API
  collector.py              MQTT subscriber, weekly and per-run CSVs
  storage.py                SQLite history store, downsampling, retention
  templates_server.py       HTTP API on :8000 (/templates, /history)
web/                        Dashboard: index.html, css/, js/ (no build step)
```

## Configuration

Mega: everything is in `firmware/mega/src/config.h`. Defaults worth knowing: 7 small-purge cycles at 400 hPa drop each, 10 large-purge cycles between the start pressure −5 hPa and +10 hPa, catalyst setpoint 50 °C (cutoff 65 °C), chamber setpoint 37 °C (heater panel at most 55 °C, cutoff 60 °C), 5 s DS18B20 timeout, pressure watchdog 5 hPa per 1.5 s (small) and 2 hPa per 3 s (big) with 3 hPa cross-chamber limit, 600 s gas sensor warm-up.

ESP32: credentials and broker address in `secrets.h`; port, topic prefix (default `anaerobic/chamber1/`) and client ID in `config.h`.

Collector: `MQTT_BROKER`, `MQTT_PORT`, `MQTT_TOPIC`, `DATA_DIR`, `HISTORY_DB`, `DATA_LIMIT_GB`, `TEMPLATES_PATH`, `TEMPLATES_PORT` are read from the environment, with localhost defaults. Docker Compose sets `MQTT_BROKER=mqtt`. `TEMPLATES_PATH` defaults to the Docker path `/data/templates.json`; set it when running on the host.

## MQTT topics

All topics sit under the configurable prefix, default `anaerobic/chamber1/`.

| Topic | Type | Retained | Meaning |
|-------|------|----------|---------|
| `sensor/o2`, `sensor/h2`, `sensor/h2s`, `sensor/o3`, `sensor/co2` | float | yes | Gas readings |
| `sensor/pressure_big`, `sensor/pressure_small` | float | yes | hPa |
| `sensor/temp_catalyst`, `sensor/temp_heater`, `sensor/temp_chamber` | float | yes | °C |
| `status/state` | string | yes | `INIT`, `IDLE`, `PURGE_S`, `PURGE_B`, `FAULT` |
| `status/thermal` | string | yes | `OFF`, `CAT`, `CHM`, `BOTH`, `FAULT` |
| `status/fault` | string | yes | `NONE`, `NO_DP`, `OVERP`, `OT_CAT`, `OT_CHM`, `TS_CAT`, `TS_CHM`, `O2_HI`, `ISR`, `ABORT`, `TMR5`, `PS_LOST`, `X_CHM` |
| `status/purge_progress` | JSON | yes | `{"current": 3, "total": 7}` |
| `status/uptime`, `status/warmup` | int | yes | Seconds |
| `status/esp_reset` | JSON | yes | ESP32 reset cause on each MQTT connect, `{"reason": "TASK_WDT", "up_s": 12}` |
| `actuator/pump`, `actuator/solenoid` | int | yes | 0 or 1 |
| `actuator/catalyst_pwm`, `actuator/chamber_pwm` | int | yes | 0 to 255 |
| `command/<verb>` | JSON | no | Command to the Mega, see below |
| `bulk` | JSON | yes | Full frame, same as the serial JSON (also carries `h2r`, `tc_sp`, `th_sp`, `th_lim`, `rst`, `boots`, `i2c`) |

Commands are JSON objects with a `cmd` field. The ESP32 forwards any payload on `command/#` that starts with `{` and is at most 240 bytes; the topic suffix is only a label. The Mega (`serial_rx.cpp`) is the only validator, and there is no acknowledgement: the next 1 Hz frame shows the result.

| Command | Effect |
|---------|--------|
| `{"cmd":"set_chm_sp","v":37}` | Chamber setpoint, clamped to 25–43 °C, saved to EEPROM |
| `{"cmd":"set_cat_sp","v":50}` | Catalyst setpoint, clamped to 40–58 °C, saved to EEPROM |
| `{"cmd":"heater_cat","v":true}` | Catalyst heater on/off |
| `{"cmd":"heater_chm","v":true}` | Chamber heater on/off |
| `{"cmd":"log","v":true}` | Active SD logging on/off |
| `{"cmd":"purge","v":"small"}` | Start a purge (`small` or `big`), only from IDLE; big needs O2 ≤ 3 % |
| `{"cmd":"abort"}` | Emergency stop, same as a long button press |
| `{"cmd":"apply","fields":{...}}` | Several settings at once (`chm_sp`, `cat_sp`, `heater_cat`, `heater_chm`, `log`); never starts a purge |

A thermal fault cannot be cleared remotely; heater commands are ignored until it is acknowledged on the device.

## Data logging

SD card on the Mega: files are named `LOG0001.CSV`, `LOG0002.CSV` and so on. Every boot starts a new file, a new file also starts when the current one passes 64 KB, and only the newest 32 are kept. Active logging (default on, switchable from the OLED menu or the dashboard) writes a row every 10 s; with it off, one row per hour. If the SD card fails at start-up, logging is disabled and everything else keeps running. The Mega has no real-time clock, so the time column is milliseconds since boot (`ms`); the dashboard's "Import CSV" anchors it to the file's modification time.

Collector: every frame appended to a weekly CSV (Monday to Sunday), plus a separate full-resolution file per purge run opened on the transition into a purge state and closed when the state returns to IDLE or FAULT.

History database: every frame is also inserted into `collector/data/history.sqlite3` with an epoch-millisecond timestamp. When the dashboard opens it calls `GET :8000/history?days=N` and starts with the last N days already drawn (toolbar "History" selector: 2 h, 8 h, 1, 2, 3, 7, 14 or 30 days, 1 day by default). The server downsamples the window into buckets: 2 h keeps every recorded second (7200 points), 8 h gets 9000 points (3x the 1-day resolution), 1 day and longer about 3000 points. Each bucket carries a mean, plus min/max bands for O2, both pressures and the three temperatures so short spikes stay visible. The Range Apply button fetches any older window the same way. Full 1 Hz data stays in the CSVs. On first start the collector imports the existing weekly CSVs into the database once. The toolbar "Scale" selector switches the charts between auto-fitted axes and fixed sensor ranges (O2 0–30 %, other gases 0–1000 ppm, pressure 0–1100 hPa, temperature 0–70 °C).

Retention: when a new week starts and the weekly CSVs plus the database exceed `DATA_LIMIT_GB` (default 32), the oldest week is deleted, both the CSV and its database rows. At about 26 MB per day that is roughly three years of data. Per-run CSVs are never deleted.

The two CSV formats differ. The SD card uses the short frame keys:

```
ms,o2,h2,h2s,o3,co2,pb,ps,tc,th,ta,sp_cat,sp_chm,st,ts,f,pump,sol,cpwm,hpwm,wu,up,pc,pt,rst,boots,i2c,th_sp,th_lim,tc_sp,h2r
```

The collector writes ISO timestamps (server clock) and long column names:

```
timestamp,o2_pct,h2_ppm,h2s_ppm,o3_ppm,co2_ppm,p_big_hpa,p_small_hpa,t_catalyst,t_chamber_heater,t_chamber,catalyst_setpoint,chamber_setpoint,state,thermal_state,fault,pump,solenoid,catalyst_pwm,chamber_pwm,warmup_s,uptime_s,purge_cycle,purge_total
```

## Status and limitations

This is a working prototype built for one chamber in one lab, not a product. The Mosquitto config allows anonymous access and the HTTP API has open CORS; both are fine on a workstation and wrong for anything reachable from outside. Hardware details (sensor addresses, ROM IDs, pin assignments) are specific to this build and will need to be re-mapped with the discovery sketch on different hardware. Firmware comments are partly in Croatian.

Values that depend on the individual build and should be checked on each chamber (marked `TODO` in `config.h`): the pressure offsets, the H2 zero calibration, and the mapping of the three DS18B20 ROM IDs to their probes. Controller gains and ramp rates are starting values tuned on one rig.

With a 55 °C panel limit the chamber reaches about 37 °C in a 20 °C lab, with little margin. Exposed aluminium at 55 °C can burn skin after roughly 10 s of contact; guard or label the heated bottom panel.

## Citation

If you use this hardware or software, please cite the HardwareX article:

> [Authors]. [Title]. *HardwareX* [volume] ([year]) [article number]. https://doi.org/[DOI]

```bibtex
@article{anaerobic_chamber_hardwarex,
  author  = {[Authors]},
  title   = {[Title]},
  journal = {HardwareX},
  volume  = {[volume]},
  pages   = {[article number]},
  year    = {[year]},
  doi     = {[DOI]}
}
```

The source code of the version described in the article is tagged `hardwarex-v1.0` and archived at https://doi.org/0.5281/zenodo.23394703.

## License

MIT, see [LICENSE](LICENSE).
