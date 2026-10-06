#!/usr/bin/env python3
"""
Anaerobic Chamber — MQTT-to-CSV Data Collector

Subscribes to the bulk MQTT topic and writes:
  1. Continuous logs (weekly CSV files in data/continuous/)
  2. Per-run logs (triggered by purge state transitions in data/runs/)

Run: python3 collector.py
"""

import csv
import json
import os
import sys
from datetime import datetime, timedelta

import paho.mqtt.client as mqtt

import config
import templates_server

# Purge states that trigger per-run logging
PURGE_STATES = {"PURGE_S", "PURGE_B"}


class ContinuousWriter:
    """Appends every message to weekly CSV files."""

    def __init__(self, output_dir: str):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)
        self._file = None
        self._writer = None
        self._current_week_start = None

    def _get_week_start(self, dt: datetime) -> datetime:
        """Return Monday 00:00 of the week containing dt."""
        return dt - timedelta(days=dt.weekday())

    def _ensure_file(self, now: datetime):
        week_start = self._get_week_start(now).date()
        if self._current_week_start != week_start:
            if self._file:
                self._file.close()
            filename = f"{week_start.strftime('%Y%m%d')}_chamber_conditions.csv"
            filepath = os.path.join(self.output_dir, filename)
            file_exists = os.path.exists(filepath)
            self._file = open(filepath, "a", newline="")
            self._writer = csv.DictWriter(self._file, fieldnames=config.CSV_COLUMNS)
            if not file_exists:
                self._writer.writeheader()
            self._current_week_start = week_start

    def append(self, row: dict):
        now = datetime.now()
        self._ensure_file(now)
        self._writer.writerow(row)
        self._file.flush()


class RunTracker:
    """Detects purge runs and writes per-run CSV files."""

    def __init__(self, output_dir: str):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)
        self._file = None
        self._writer = None
        self.active = False

    def start_run(self, state: str, row: dict):
        now = datetime.now()
        state_label = "purge_small" if state == "PURGE_S" else "purge_big"
        filename = f"run_{now.strftime('%Y%m%d_%H%M%S')}_{state_label}.csv"
        filepath = os.path.join(self.output_dir, filename)
        self._file = open(filepath, "w", newline="")
        self._writer = csv.DictWriter(self._file, fieldnames=config.CSV_COLUMNS)
        self._writer.writeheader()
        self._writer.writerow(row)
        self._file.flush()
        self.active = True
        print(f"Run started: {filename}")

    def append(self, row: dict):
        if self._writer:
            self._writer.writerow(row)
            self._file.flush()

    def end_run(self, row: dict):
        if self._writer:
            self._writer.writerow(row)
            self._file.flush()
        if self._file:
            self._file.close()
            self._file = None
            self._writer = None
        self.active = False
        print("Run ended")


class DataCollector:
    def __init__(self):
        self.client = mqtt.Client(client_id=config.MQTT_CLIENT_ID)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.continuous = ContinuousWriter(config.CONTINUOUS_DIR)
        self.run_tracker = RunTracker(config.RUNS_DIR)
        self.prev_state = "IDLE"

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to MQTT broker at {config.MQTT_BROKER}:{config.MQTT_PORT}")
            client.subscribe(config.MQTT_TOPIC)
        else:
            print(f"MQTT connection failed with code {rc}")

    def _flatten(self, data: dict) -> dict:
        """Convert JSON message to CSV row."""
        row = {"timestamp": datetime.now().isoformat()}
        for json_key, csv_col in config.JSON_TO_CSV.items():
            row[csv_col] = data.get(json_key, "")
        return row

    def _on_message(self, client, userdata, msg):
        try:
            data = json.loads(msg.payload)
        except json.JSONDecodeError:
            return

        row = self._flatten(data)

        # Always log to continuous file
        self.continuous.append(row)

        # Track run boundaries
        state = data.get("st", "IDLE")
        if state in PURGE_STATES and self.prev_state not in PURGE_STATES:
            self.run_tracker.start_run(state, row)
        elif state not in PURGE_STATES and self.prev_state in PURGE_STATES:
            self.run_tracker.end_run(row)
        elif self.run_tracker.active:
            self.run_tracker.append(row)

        self.prev_state = state

    def run(self):
        print(f"Connecting to {config.MQTT_BROKER}:{config.MQTT_PORT}...")
        self.client.connect(config.MQTT_BROKER, config.MQTT_PORT, keepalive=60)
        try:
            self.client.loop_forever()
        except KeyboardInterrupt:
            print("\nShutting down...")
            self.client.disconnect()


if __name__ == "__main__":
    templates_server.start()
    collector = DataCollector()
    collector.run()
