#!/usr/bin/env python3
"""
Serial Monitor & Logger - Connects to ESP32 serial port, parses structured
log output, tracks heap/MQTT/errors, logs to CSV for trend analysis.

Usage:
    python serial_monitor.py                    # Default COM3
    python serial_monitor.py --port COM5        # Custom port
    python serial_monitor.py --duration 3600    # Run for 1 hour
"""

import argparse
import csv
import os
import re
import sys
import threading
import time
from collections import deque
from datetime import datetime

import serial

import config

# === Regex patterns matching ESP32 serial output ===

# System Status Monitor block (main.c lines 5078-5118)
RE_HEAP = re.compile(r"Free Heap:\s*(\d+)\s+Min Free:\s*(\d+)")
RE_MQTT_STATUS = re.compile(r"MQTT:\s*(CONNECTED|DISCONNECTED|CONNECTING|OFFLINE)")
RE_SENSORS = re.compile(r"Sensors:\s*(\d+)")
RE_WEB_STATUS = re.compile(r"Web:\s*(RUNNING|STOPPED)")
RE_MESSAGES = re.compile(r"Messages:\s*(\d+)")

# Heartbeat JSON (main.c lines 943-957)
RE_HEARTBEAT = re.compile(r'\{"type":"heartbeat".*?\}')

# Error patterns
RE_ESP_ERROR = re.compile(r"^E \((\d+)\) (\w+): (.*)$")
RE_CRASH = re.compile(r"(Guru Meditation|assert failed|abort\(\)|Backtrace:)")
RE_WATCHDOG = re.compile(r"(Task watchdog|Watchdog|wdt reset)")
RE_REBOOT = re.compile(r"(rst:0x|ets \w+ \d{4}|configsip:)")
RE_SD_ERROR = re.compile(r"(sdmmc_.*error|errno: 5|SD card.*fail|I/O error)")
RE_MQTT_EVENT = re.compile(r"(MQTT event |MQTT_EVENT_)(MQTT_EVENT_\w+|\w+)")
RE_TELEMETRY_SENT = re.compile(r"(Message published successfully|Telemetry sent|Telemetry sent to MQTT)")
RE_TELEMETRY_FAIL = re.compile(r"(Failed publishing|Telemetry.*fail)")
RE_SD_SAVED = re.compile(r"(SD card.*saved|Cached.*SD|saved to SD)")
RE_SD_REPLAY = re.compile(r"(Replay.*message|replaying.*SD|SD replay)")
RE_HEAP_WARN = re.compile(r"(Low heap|heap.*warning|HEAP_WARNING)")
RE_OOM = re.compile(r"(out of memory|malloc.*fail|heap exhausted)")
RE_WIFI_CONNECTED = re.compile(r"(Got IP:|STA_GOT_IP|WiFi connected|wifi.*connected)", re.IGNORECASE)
RE_WIFI_DISCONNECTED = re.compile(r"(STA_DISCONNECTED|WiFi disconnected|wifi.*disconnect)", re.IGNORECASE)


class SerialMonitor:
    """Real-time serial log parser with metric tracking."""

    def __init__(self, port=None, baud=None, csv_path=None):
        self.port = port or config.SERIAL_PORT
        self.baud = baud or config.SERIAL_BAUD
        self.running = False
        self._serial = None
        self._thread = None

        # Metrics
        self.heap_free = 0
        self.heap_min = 999999
        self.heap_history = deque(maxlen=2000)  # (timestamp, free, min)
        self.mqtt_status = "UNKNOWN"
        self.mqtt_connected_count = 0
        self.mqtt_disconnected_count = 0
        self.telemetry_sent = 0
        self.telemetry_failed = 0
        self.error_count = 0
        self.crash_count = 0
        self.reboot_count = 0
        self.sd_errors = 0
        self.sd_saves = 0
        self.sd_replays = 0
        self.sensor_count = 0
        self.wifi_status = "UNKNOWN"
        self.web_status = "UNKNOWN"
        self.uptime_sec = 0
        self.start_time = None
        self.last_status_time = None

        # Error log
        self.errors = deque(maxlen=500)
        self.raw_lines = deque(maxlen=1000)

        # CSV logging
        if csv_path is None:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            csv_path = os.path.join(config.REPORTS_DIR, f"serial_log_{ts}.csv")
        self.csv_path = csv_path
        self._csv_file = None
        self._csv_writer = None

        # Callbacks for orchestrator
        self._pattern_waiters = []  # [(pattern, event, match_result)]

    def start(self):
        """Start monitoring in background thread."""
        os.makedirs(os.path.dirname(self.csv_path), exist_ok=True)
        self._csv_file = open(self.csv_path, "w", newline="", buffering=1)
        self._csv_writer = csv.writer(self._csv_file)
        self._csv_writer.writerow([
            "timestamp", "elapsed_sec", "heap_free", "heap_min",
            "mqtt_status", "telemetry_sent", "telemetry_failed",
            "error_count", "sd_errors", "sd_saves", "sensor_count",
            "web_status", "event"
        ])

        self.running = True
        self.start_time = time.time()
        self._thread = threading.Thread(target=self._read_loop, daemon=True)
        self._thread.start()
        print(f"[SerialMonitor] Started on {self.port} @ {self.baud} baud")
        print(f"[SerialMonitor] Logging to {self.csv_path}")

    def stop(self):
        """Stop monitoring."""
        self.running = False
        if self._thread:
            self._thread.join(timeout=3)
        if self._serial and self._serial.is_open:
            self._serial.close()
        if self._csv_file:
            self._csv_file.close()
        print(f"[SerialMonitor] Stopped. {self.error_count} errors, "
              f"{self.crash_count} crashes, {self.reboot_count} reboots")

    def _read_loop(self):
        """Main serial reading loop."""
        try:
            self._serial = serial.Serial(
                self.port, self.baud, timeout=1,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
        except serial.SerialException as e:
            print(f"[SerialMonitor] ERROR: Cannot open {self.port}: {e}")
            self.running = False
            return

        while self.running:
            try:
                if self._serial.in_waiting > 0:
                    line = self._serial.readline().decode("utf-8", errors="replace").strip()
                    if line:
                        self._process_line(line)
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                print("[SerialMonitor] Serial connection lost, reconnecting...")
                time.sleep(2)
                try:
                    self._serial.close()
                    self._serial.open()
                except Exception:
                    pass

    def _process_line(self, line):
        """Parse a single serial log line."""
        self.raw_lines.append((time.time(), line))
        event = ""

        # --- Heap ---
        m = RE_HEAP.search(line)
        if m:
            self.heap_free = int(m.group(1))
            heap_min_val = int(m.group(2))
            if heap_min_val < self.heap_min:
                self.heap_min = heap_min_val
            self.heap_history.append((time.time(), self.heap_free, heap_min_val))
            self.last_status_time = time.time()
            event = "STATUS"

            # Heap warnings
            if self.heap_free < config.HEAP_EMERGENCY:
                event = "HEAP_EMERGENCY"
                self._log_error(f"EMERGENCY: Heap at {self.heap_free} bytes!")
            elif self.heap_free < config.HEAP_CRITICAL:
                event = "HEAP_CRITICAL"
                self._log_error(f"CRITICAL: Heap at {self.heap_free} bytes")
            elif self.heap_free < config.HEAP_WARNING:
                event = "HEAP_WARNING"

        # --- MQTT status ---
        m = RE_MQTT_STATUS.search(line)
        if m:
            new_status = m.group(1)
            # Normalize OFFLINE to DISCONNECTED
            if new_status == "OFFLINE":
                new_status = "DISCONNECTED"
            if new_status != self.mqtt_status:
                if new_status == "CONNECTED":
                    self.mqtt_connected_count += 1
                elif new_status == "DISCONNECTED":
                    self.mqtt_disconnected_count += 1
                event = f"MQTT_{new_status}"
            self.mqtt_status = new_status

        m = RE_MQTT_EVENT.search(line)
        if m:
            evt = m.group(2)
            if "CONNECTED" in evt and "DISCONNECTED" not in evt:
                self.mqtt_status = "CONNECTED"
                self.mqtt_connected_count += 1
                event = "MQTT_CONNECTED"
            elif "DISCONNECTED" in evt:
                self.mqtt_status = "DISCONNECTED"
                self.mqtt_disconnected_count += 1
                event = "MQTT_DISCONNECTED"

        # --- Sensors ---
        m = RE_SENSORS.search(line)
        if m:
            self.sensor_count = int(m.group(1))

        # --- Web status ---
        m = RE_WEB_STATUS.search(line)
        if m:
            self.web_status = m.group(1)

        # --- Telemetry ---
        if RE_TELEMETRY_SENT.search(line):
            self.telemetry_sent += 1
            event = event or "TELEMETRY_OK"
        if RE_TELEMETRY_FAIL.search(line):
            self.telemetry_failed += 1
            event = event or "TELEMETRY_FAIL"
            self._log_error(f"Telemetry failure: {line}")

        # --- SD Card ---
        if RE_SD_ERROR.search(line):
            self.sd_errors += 1
            event = event or "SD_ERROR"
            self._log_error(f"SD error: {line}")
        if RE_SD_SAVED.search(line):
            self.sd_saves += 1
            event = event or "SD_SAVE"
        if RE_SD_REPLAY.search(line):
            self.sd_replays += 1
            event = event or "SD_REPLAY"

        # --- Crashes & Reboots ---
        if RE_CRASH.search(line):
            self.crash_count += 1
            event = "CRASH"
            self._log_error(f"CRASH DETECTED: {line}")
        if RE_WATCHDOG.search(line):
            self.crash_count += 1
            event = "WATCHDOG"
            self._log_error(f"WATCHDOG RESET: {line}")
        if RE_REBOOT.search(line):
            self.reboot_count += 1
            event = "REBOOT"
            self.heap_min = 999999  # Reset after reboot

        # --- General errors ---
        m = RE_ESP_ERROR.search(line)
        if m:
            self.error_count += 1
            self._log_error(f"[{m.group(2)}] {m.group(3)}")

        # --- WiFi ---
        if RE_WIFI_CONNECTED.search(line):
            self.wifi_status = "CONNECTED"
            event = event or "WIFI_CONNECTED"
        if RE_WIFI_DISCONNECTED.search(line):
            self.wifi_status = "DISCONNECTED"
            event = event or "WIFI_DISCONNECTED"

        if RE_OOM.search(line):
            event = "OUT_OF_MEMORY"
            self._log_error(f"OOM: {line}")

        if RE_HEAP_WARN.search(line):
            event = event or "HEAP_WARNING"

        # --- Write CSV row on status updates or events ---
        if event:
            self._write_csv_row(event)

        # --- Check pattern waiters ---
        for waiter in self._pattern_waiters[:]:
            if waiter[0].search(line):
                waiter[2].append(line)
                waiter[1].set()

    def _log_error(self, msg):
        """Add to error log."""
        self.errors.append((datetime.now().isoformat(), msg))

    def _write_csv_row(self, event=""):
        """Write a CSV row with current metrics."""
        if not self._csv_writer:
            return
        elapsed = time.time() - self.start_time if self.start_time else 0
        self._csv_writer.writerow([
            datetime.now().isoformat(),
            f"{elapsed:.1f}",
            self.heap_free,
            self.heap_min,
            self.mqtt_status,
            self.telemetry_sent,
            self.telemetry_failed,
            self.error_count,
            self.sd_errors,
            self.sd_saves,
            self.sensor_count,
            self.web_status,
            event
        ])

    # === Public API for orchestrator ===

    def get_status(self):
        """Get current device status snapshot."""
        return {
            "heap_free": self.heap_free,
            "heap_min": self.heap_min,
            "wifi_status": self.wifi_status,
            "mqtt_status": self.mqtt_status,
            "mqtt_connects": self.mqtt_connected_count,
            "mqtt_disconnects": self.mqtt_disconnected_count,
            "telemetry_sent": self.telemetry_sent,
            "telemetry_failed": self.telemetry_failed,
            "error_count": self.error_count,
            "crash_count": self.crash_count,
            "reboot_count": self.reboot_count,
            "sd_errors": self.sd_errors,
            "sd_saves": self.sd_saves,
            "sd_replays": self.sd_replays,
            "sensor_count": self.sensor_count,
            "web_status": self.web_status,
            "elapsed_sec": time.time() - self.start_time if self.start_time else 0,
        }

    def get_heap_trend(self):
        """Return heap history as list of (timestamp, free, min)."""
        return list(self.heap_history)

    def check_heap_leak(self, window_minutes=30, max_decline_bytes=5000):
        """Check if heap has been declining over the given window."""
        if len(self.heap_history) < 10:
            return False, "Not enough data"
        cutoff = time.time() - (window_minutes * 60)
        recent = [(t, f, m) for t, f, m in self.heap_history if t > cutoff]
        if len(recent) < 5:
            return False, "Not enough recent data"
        first_avg = sum(f for _, f, _ in recent[:3]) / 3
        last_avg = sum(f for _, f, _ in recent[-3:]) / 3
        decline = first_avg - last_avg
        if decline > max_decline_bytes:
            return True, f"Heap declined by {decline:.0f} bytes over {window_minutes}min"
        return False, f"Heap stable (delta: {decline:.0f} bytes)"

    def wait_for_pattern(self, pattern_str, timeout=60):
        """Block until a regex pattern appears in serial output."""
        pattern = re.compile(pattern_str)
        event = threading.Event()
        matches = []
        waiter = (pattern, event, matches)
        self._pattern_waiters.append(waiter)
        found = event.wait(timeout=timeout)
        self._pattern_waiters.remove(waiter)
        return found, matches


def main():
    parser = argparse.ArgumentParser(description="ESP32 Serial Monitor & Logger")
    parser.add_argument("--port", default=config.SERIAL_PORT)
    parser.add_argument("--baud", type=int, default=config.SERIAL_BAUD)
    parser.add_argument("--duration", type=int, default=0, help="Run for N seconds (0=forever)")
    args = parser.parse_args()

    monitor = SerialMonitor(port=args.port, baud=args.baud)
    monitor.start()

    try:
        start = time.time()
        while monitor.running:
            time.sleep(10)
            s = monitor.get_status()
            elapsed = s["elapsed_sec"]
            mins = int(elapsed // 60)
            secs = int(elapsed % 60)
            print(f"\r[{mins:3d}m{secs:02d}s] Heap: {s['heap_free']:,} "
                  f"(min: {s['heap_min']:,}) | MQTT: {s['mqtt_status']} | "
                  f"Tx: {s['telemetry_sent']} | Err: {s['error_count']} | "
                  f"SD: {s['sd_saves']}saved/{s['sd_errors']}err | "
                  f"Crashes: {s['crash_count']}", end="", flush=True)

            if args.duration > 0 and (time.time() - start) >= args.duration:
                break

            # Periodic heap leak check
            if elapsed > 300 and int(elapsed) % 300 < 11:
                leak, msg = monitor.check_heap_leak()
                if leak:
                    print(f"\n  *** HEAP LEAK WARNING: {msg} ***")

    except KeyboardInterrupt:
        print("\n\nStopping...")
    finally:
        monitor.stop()
        print(f"\nReport saved to: {monitor.csv_path}")
        print(f"\nSummary:")
        for k, v in monitor.get_status().items():
            print(f"  {k}: {v}")
        if monitor.errors:
            print(f"\nLast 10 errors:")
            for ts, msg in list(monitor.errors)[-10:]:
                print(f"  [{ts}] {msg}")


if __name__ == "__main__":
    main()
