# Anaerobic Chamber Control System — Build & Deploy
#
# Prerequisites: PlatformIO CLI (pio), Python 3, Mosquitto
#
# Usage:
#   make build          Build both Mega and ESP32 firmware
#   make flash-mega     Build and upload Mega firmware
#   make flash-esp32    Build and upload ESP32 firmware
#   make monitor-mega   Open serial monitor for Mega (USB)
#   make monitor-esp32  Open serial monitor for ESP32
#   make collector      Run the Python MQTT-to-CSV collector
#   make web            Serve the web dashboard on port 8080
#   make docker-up      Run dashboard + Mosquitto in Docker (8080, 1883, 9001)
#   make docker-down    Stop the Docker stack
#   make discovery      Flash hardware discovery sketch and open monitor
#   make clean          Clean all PlatformIO build artifacts

.PHONY: build build-mega build-esp32 flash-mega flash-esp32 \
        monitor-mega monitor-esp32 discovery collector web clean \
        mqtt-sub mqtt-test docker-build docker-up docker-down docker-logs

# --- Firmware -----------------------------------------------------------------

build: build-mega build-esp32

build-mega:
	pio run -e mega

build-esp32:
	pio run -e esp32

flash-mega:
	pio run -e mega -t upload

flash-esp32:
	pio run -e esp32 -t upload

monitor-mega:
	pio device monitor -e mega

monitor-esp32:
	pio device monitor -e esp32

# Flash and immediately open monitor
flash-monitor-mega: flash-mega monitor-mega
flash-monitor-esp32: flash-esp32 monitor-esp32

# --- Hardware Discovery (flash first to map all hardware) ---------------------

discovery:
	pio run -e discovery -t upload && pio device monitor -e discovery

clean:
	pio run -t clean

# --- Python Collector ---------------------------------------------------------

collector-install:
	pip install -r collector/requirements.txt

collector:
	cd collector && python3 collector.py

# --- Web Dashboard ------------------------------------------------------------

WEB_PORT ?= 8080

web:
	cd web && python3 -m http.server $(WEB_PORT)

# --- Docker (web dashboard + MQTT broker) -------------------------------------

# Bring up the full dev stack: nginx-served dashboard on :8080 and Mosquitto
# with WebSockets (:9001) and native MQTT (:1883).
docker-up:
	docker compose up -d --build

docker-down:
	docker compose down

docker-build:
	docker compose build

docker-logs:
	docker compose logs -f

# --- MQTT Debugging -----------------------------------------------------------

# Subscribe to all chamber topics (requires mosquitto_sub)
mqtt-sub:
	mosquitto_sub -t 'anaerobic/#' -v

# Publish a fake data frame for testing the collector and dashboard
mqtt-test:
	mosquitto_pub -t 'anaerobic/chamber1/bulk' -m \
	  '{"o2":"0.12","h2":"415","h2s":"0.02","o3":"0.01","co2":"820","pb":"1013.2","ps":"1012.8","tc":"48.5","th":"36.2","ta":"35.1","st":"IDLE","ts":"CAT","f":"NONE","pc":0,"pt":7,"pump":0,"sol":0,"cpwm":180,"hpwm":120,"wu":0,"up":54321}'
