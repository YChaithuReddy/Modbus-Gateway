#!/usr/bin/env python3
"""
=============================================================================
  ESP32 Modbus IoT Gateway - FULL AUTOMATED TEST RUNNER
=============================================================================

ONE COMMAND TO TEST EVERYTHING:
    python full_test_runner.py

What it does:
    1. Flashes firmware to ESP32
    2. Waits for boot and WiFi/MQTT connection
    3. Runs ALL tests over configurable duration (default 2 hours)
    4. Generates a final ISSUES REPORT with everything found

Tests included:
    - Heap stability monitoring (continuous)
    - MQTT connection tracking (continuous)
    - Crash/reboot detection (continuous)
    - SD card health monitoring (continuous)
    - Web UI stress (connection flood, rapid polling)
    - Web form fuzzing (XSS, injection, overflow)
    - Network disruption (MQTT block, flapping)
    - Telemetry verification (timing, frequency)
    - Long-duration soak with periodic checks

Requirements:
    pip install -r requirements.txt
    ESP-IDF environment activated (for flashing)
    Run as Administrator (for network fault tests)
"""

import argparse
import asyncio
import csv
import ctypes
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections import defaultdict
from datetime import datetime, timedelta

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(__file__))

import config
from serial_monitor import SerialMonitor

# Try importing optional components
try:
    from web_ui_stress_tester import WebStressTester
    HAS_WEB_TESTER = True
except ImportError:
    HAS_WEB_TESTER = False

try:
    from network_fault_injector import NetworkFaultInjector, block_mqtt, unblock_mqtt, is_admin
    HAS_NETWORK = True
except ImportError:
    HAS_NETWORK = False


# ============================================================================
#  CONFIGURATION - EDIT THESE FOR YOUR SETUP
# ============================================================================

class TestConfig:
    # Duration
    TOTAL_TEST_HOURS = 2              # Total test duration in hours

    # Flash settings
    FLASH_ENABLED = True              # Set False to skip flashing
    FLASH_PORT = config.SERIAL_PORT   # COM port for flashing
    PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    # Serial
    SERIAL_PORT = config.SERIAL_PORT
    SERIAL_BAUD = config.SERIAL_BAUD

    # Device network
    DEVICE_IP = config.DEVICE_IP

    # Test toggles - disable any test you don't want
    TEST_WEB_STRESS = True
    TEST_WEB_FUZZ = True
    TEST_NETWORK_DISRUPTION = True    # Requires Administrator
    TEST_NETWORK_FLAP = True          # Requires Administrator

    # Thresholds for PASS/FAIL
    HEAP_MIN_ACCEPTABLE = 20000       # Below this = CRITICAL issue
    HEAP_LEAK_MAX_KB = 10             # Max acceptable decline over test
    MAX_CRASHES = 0                   # Any crash = issue
    MAX_REBOOTS = 0                   # Any unexpected reboot = issue
    MAX_SD_ERRORS = 5                 # Occasional SD error OK
    MAX_TELEMETRY_FAIL_PERCENT = 10   # Max % of failed telemetry


# ============================================================================
#  ISSUE TRACKER
# ============================================================================

class Issue:
    CRITICAL = "CRITICAL"
    HIGH = "HIGH"
    MEDIUM = "MEDIUM"
    LOW = "LOW"
    INFO = "INFO"

class IssueTracker:
    """Collects all issues found during testing."""

    def __init__(self):
        self.issues = []  # (severity, category, title, details, timestamp)

    def add(self, severity, category, title, details=""):
        self.issues.append((
            severity, category, title, details,
            datetime.now().strftime("%H:%M:%S")
        ))
        icon = {"CRITICAL": "!!!", "HIGH": "!!", "MEDIUM": "!", "LOW": "~", "INFO": "i"}
        print(f"  [{icon.get(severity, '?')}] {severity}: {title}")
        if details:
            print(f"       {details[:120]}")

    def get_by_severity(self, severity):
        return [i for i in self.issues if i[0] == severity]

    def get_by_category(self, category):
        return [i for i in self.issues if i[1] == category]

    def summary(self):
        counts = defaultdict(int)
        for sev, *_ in self.issues:
            counts[sev] += 1
        return dict(counts)


# ============================================================================
#  TEST PHASES
# ============================================================================

def _build_flash_command(project_dir, port):
    """Build the flash command with ESP-IDF environment activation.

    Uses PowerShell to activate ESP-IDF (same as the 'espidf' alias),
    then runs idf.py flash. Must use PowerShell because ESP-IDF v5.5
    rejects MSys/Mingw (Git Bash).
    """
    # Find ESP-IDF export.ps1
    idf_paths = [
        os.path.join(os.path.expanduser("~"), "esp", "v5.5.1", "esp-idf"),
        os.path.join(os.path.expanduser("~"), "esp", "esp-idf"),
    ]
    for idf_path in idf_paths:
        export_ps1 = os.path.join(idf_path, "export.ps1")
        if os.path.exists(export_ps1):
            # PowerShell command: activate ESP-IDF, cd to project, flash
            # Must clear MSYSTEM/MSYS to avoid MSys/Mingw rejection by ESP-IDF
            ps_script = (
                f'$env:MSYSTEM = $null; $env:MSYS = $null; '
                f'$ErrorActionPreference = "Continue"; '
                f'Set-Location "{idf_path}"; '
                f'. .\\export.ps1; '
                f'Set-Location "{project_dir}"; '
                f'idf.py -p {port} flash'
            )
            return [
                "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-Command", ps_script
            ]
    return None


def phase_flash(cfg, issues):
    """Phase 0: Flash firmware to ESP32."""
    if not cfg.FLASH_ENABLED:
        print("\n[SKIP] Flashing disabled in config")
        return True

    print("\n" + "="*60)
    print("  PHASE 0: FLASHING FIRMWARE")
    print("="*60)

    # Find ESP-IDF environment and build flash command
    flash_cmd = _build_flash_command(cfg.PROJECT_DIR, cfg.FLASH_PORT)

    if flash_cmd is None:
        # Last fallback: assume idf.py is already in PATH
        try:
            result = subprocess.run(
                "idf.py --version", shell=True, capture_output=True, text=True, timeout=10
            )
            if result.returncode != 0:
                raise FileNotFoundError
            flash_cmd = f"idf.py -p {cfg.FLASH_PORT} flash"
        except (FileNotFoundError, subprocess.TimeoutExpired):
            issues.add(Issue.HIGH, "FLASH", "ESP-IDF not found",
                       "Cannot find ESP-IDF. Install it or run from ESP-IDF terminal.")
            print("  WARNING: Cannot flash. Continuing with existing firmware...")
            return True  # Continue anyway

    print(f"  Project: {cfg.PROJECT_DIR}")
    print(f"  Port: {cfg.FLASH_PORT}")
    print(f"  Running: idf.py -p {cfg.FLASH_PORT} flash")

    try:
        use_shell = isinstance(flash_cmd, str)
        result = subprocess.run(
            flash_cmd,
            shell=use_shell, cwd=cfg.PROJECT_DIR,
            capture_output=True, text=True, timeout=300
        )
        if result.returncode != 0:
            stderr = result.stderr + result.stdout  # Some errors go to stdout
            # Check for specific errors
            if "app partitions are too small" in stderr:
                issues.add(Issue.CRITICAL, "FLASH", "Binary too large for OTA partition",
                           stderr[-200:])
                return False
            if "serial port" in stderr.lower() or "could not open" in stderr.lower():
                issues.add(Issue.CRITICAL, "FLASH",
                           f"Cannot open {cfg.FLASH_PORT} - is ESP32 plugged in?",
                           stderr[-200:])
                return False
            issues.add(Issue.CRITICAL, "FLASH", "Flash failed",
                       stderr[-300:])
            return False
        print("  Flash successful!")
        return True
    except subprocess.TimeoutExpired:
        issues.add(Issue.CRITICAL, "FLASH", "Flash timed out after 5 minutes")
        return False
    except Exception as e:
        issues.add(Issue.HIGH, "FLASH", f"Flash error: {e}")
        return True  # Continue anyway


def phase_boot_verify(monitor, issues, timeout=90):
    """Phase 1: Wait for ESP32 to boot, connect WiFi and MQTT."""
    print("\n" + "="*60)
    print("  PHASE 1: BOOT VERIFICATION")
    print("="*60)

    # Wait for boot
    print("  Waiting for ESP32 boot...")
    found, _ = monitor.wait_for_pattern(r"app_start|SYSTEM STATUS MONITOR", timeout=timeout)
    if not found:
        issues.add(Issue.CRITICAL, "BOOT", "ESP32 did not boot within timeout",
                   f"No boot message detected in {timeout}s")
        return False
    print("  Boot detected!")

    # Wait for WiFi
    print("  Waiting for WiFi connection...")
    found, _ = monitor.wait_for_pattern(r"WiFi connected|STA_GOT_IP|Got IP:|got ip:", timeout=60)
    if not found:
        issues.add(Issue.HIGH, "BOOT", "WiFi did not connect within 60s")
    else:
        print("  WiFi connected!")

    # Wait for MQTT
    print("  Waiting for MQTT connection...")
    found, _ = monitor.wait_for_pattern(r"MQTT_EVENT_CONNECTED|MQTT:\s+CONNECTED", timeout=120)
    if not found:
        issues.add(Issue.HIGH, "BOOT", "MQTT did not connect within 120s")
    else:
        print("  MQTT connected!")

    # Wait for first status block
    print("  Waiting for first status report...")
    found, _ = monitor.wait_for_pattern(r"Free Heap:", timeout=60)
    if found:
        heap = monitor.heap_free
        print(f"  Initial heap: {heap:,} bytes")
        if heap < 40000:
            issues.add(Issue.MEDIUM, "BOOT", f"Low initial heap: {heap:,} bytes",
                       "Expected >40,000 bytes after boot with web server")

    time.sleep(5)  # Let things stabilize
    return True


def phase_baseline(monitor, issues, duration_min=10):
    """Phase 2: Baseline monitoring - just observe for stability."""
    print("\n" + "="*60)
    print(f"  PHASE 2: BASELINE STABILITY ({duration_min} min)")
    print("="*60)

    initial_heap = monitor.heap_free
    initial_errors = monitor.error_count
    initial_crashes = monitor.crash_count

    end_time = time.time() + (duration_min * 60)
    while time.time() < end_time:
        time.sleep(30)
        s = monitor.get_status()
        elapsed = s['elapsed_sec']
        print(f"  [{int(elapsed//60)}m] Heap: {s['heap_free']:,} | "
              f"MQTT: {s['mqtt_status']} | Tx: {s['telemetry_sent']} | "
              f"Err: {s['error_count']}")

    # Check results
    final_heap = monitor.heap_free
    heap_drop = initial_heap - final_heap
    new_errors = monitor.error_count - initial_errors
    new_crashes = monitor.crash_count - initial_crashes

    if new_crashes > 0:
        issues.add(Issue.CRITICAL, "STABILITY",
                   f"{new_crashes} crash(es) during baseline",
                   "Device crashed during normal operation with no stress")

    if heap_drop > 5000:
        issues.add(Issue.MEDIUM, "MEMORY",
                   f"Heap dropped {heap_drop:,} bytes during baseline",
                   f"From {initial_heap:,} to {final_heap:,}")

    if monitor.mqtt_status != "CONNECTED":
        issues.add(Issue.HIGH, "MQTT", "MQTT not connected after baseline period")

    if monitor.telemetry_sent == 0 and duration_min >= 6:
        issues.add(Issue.HIGH, "TELEMETRY", "No telemetry sent during baseline",
                   f"Expected at least 1 message in {duration_min} minutes")

    if new_errors > 10:
        issues.add(Issue.MEDIUM, "ERRORS", f"{new_errors} errors during baseline")

    print(f"  Baseline done: heap_drop={heap_drop:,}, errors={new_errors}, crashes={new_crashes}")


def phase_web_stress(monitor, issues, cfg):
    """Phase 3: Web UI stress testing."""
    if not cfg.TEST_WEB_STRESS or not HAS_WEB_TESTER:
        print("\n[SKIP] Web stress test")
        return

    print("\n" + "="*60)
    print("  PHASE 3: WEB UI STRESS TEST")
    print("="*60)

    tester = WebStressTester(device_ip=cfg.DEVICE_IP)
    logged_in = tester.login()
    if not logged_in:
        issues.add(Issue.HIGH, "WEB", "Cannot login to web UI",
                   f"Failed to authenticate at http://{cfg.DEVICE_IP}")
        return

    heap_before = monitor.heap_free
    crashes_before = monitor.crash_count

    # Test 1: Connection flood (3 concurrent)
    print("\n  --- Connection Flood (3 concurrent) ---")
    try:
        asyncio.run(tester.test_connection_flood(concurrent=3, requests_per=20))
    except Exception as e:
        issues.add(Issue.MEDIUM, "WEB", f"Connection flood error: {e}")

    time.sleep(5)
    heap_after_flood = monitor.heap_free
    if monitor.crash_count > crashes_before:
        issues.add(Issue.CRITICAL, "WEB", "Crash during web connection flood")

    # Test 2: Connection flood (5 concurrent - near max)
    print("\n  --- Connection Flood (5 concurrent) ---")
    try:
        asyncio.run(tester.test_connection_flood(concurrent=5, requests_per=10))
    except Exception as e:
        issues.add(Issue.MEDIUM, "WEB", f"Heavy flood error: {e}")

    time.sleep(5)
    if monitor.crash_count > crashes_before:
        issues.add(Issue.CRITICAL, "WEB", "Crash during heavy web flood")

    # Test 3: Rapid polling
    print("\n  --- Rapid API Polling (30s) ---")
    try:
        asyncio.run(tester.test_rapid_polling(duration_sec=30))
    except Exception as e:
        issues.add(Issue.MEDIUM, "WEB", f"Rapid polling error: {e}")

    time.sleep(5)

    # Test 4: Large page loads
    print("\n  --- Large Page Load (5x) ---")
    try:
        asyncio.run(tester.test_large_page_load(count=5))
    except Exception as e:
        issues.add(Issue.MEDIUM, "WEB", f"Large page load error: {e}")

    # Analyze web results
    time.sleep(10)  # Let heap recover
    heap_after = monitor.heap_free
    heap_recovered = heap_after > (heap_before - 10000)

    total_requests = len(tester.results)
    failed = sum(1 for r in tester.results if r[2] != 200)
    fail_pct = (failed / max(total_requests, 1)) * 100

    if fail_pct > 30:
        issues.add(Issue.HIGH, "WEB",
                   f"{fail_pct:.0f}% of web requests failed ({failed}/{total_requests})")
    elif fail_pct > 10:
        issues.add(Issue.MEDIUM, "WEB",
                   f"{fail_pct:.0f}% of web requests failed ({failed}/{total_requests})")

    if not heap_recovered:
        issues.add(Issue.HIGH, "WEB",
                   f"Heap did not recover after web stress",
                   f"Before: {heap_before:,}, After: {heap_after:,}")

    tester.save_report()
    print(f"  Web stress done: {total_requests} requests, {failed} failed, "
          f"heap {heap_before:,} -> {heap_after:,}")


def phase_web_fuzz(monitor, issues, cfg):
    """Phase 4: Web form fuzzing (XSS, injection, etc.)."""
    if not cfg.TEST_WEB_FUZZ or not HAS_WEB_TESTER:
        print("\n[SKIP] Web fuzz test")
        return

    print("\n" + "="*60)
    print("  PHASE 4: WEB FORM FUZZING")
    print("="*60)

    tester = WebStressTester(device_ip=cfg.DEVICE_IP)
    tester.login()

    crashes_before = monitor.crash_count
    reboots_before = monitor.reboot_count

    try:
        asyncio.run(tester.test_form_fuzzing())
    except Exception as e:
        issues.add(Issue.MEDIUM, "SECURITY", f"Fuzz test error: {e}")

    time.sleep(5)

    if monitor.crash_count > crashes_before:
        issues.add(Issue.CRITICAL, "SECURITY",
                   "Crash during form fuzzing!",
                   "Malformed input caused a device crash - potential security vulnerability")

    if monitor.reboot_count > reboots_before:
        issues.add(Issue.CRITICAL, "SECURITY",
                   "Reboot during form fuzzing!",
                   "Malformed input caused device reboot")

    # Check for max sessions
    print("\n  --- Max Sessions Test ---")
    try:
        asyncio.run(tester.test_max_sessions())
    except Exception as e:
        pass

    print("  Fuzz testing done.")


def phase_network_disruption(monitor, issues, cfg):
    """Phase 5: Network disruption tests."""
    if not cfg.TEST_NETWORK_DISRUPTION or not HAS_NETWORK:
        print("\n[SKIP] Network disruption test")
        return

    if not is_admin():
        issues.add(Issue.LOW, "NETWORK", "Network tests skipped - not running as Administrator")
        print("\n[SKIP] Network disruption (requires Administrator)")
        return

    print("\n" + "="*60)
    print("  PHASE 5: NETWORK DISRUPTION")
    print("="*60)

    injector = NetworkFaultInjector()
    crashes_before = monitor.crash_count
    reboots_before = monitor.reboot_count
    sd_saves_before = monitor.sd_saves

    try:
        # Test 1: Block MQTT for 3 minutes
        print("\n  --- Block MQTT for 3 minutes ---")
        injector.block()

        # Wait and check for SD caching
        time.sleep(180)

        if monitor.sd_saves > sd_saves_before:
            print(f"  SD caching active: {monitor.sd_saves - sd_saves_before} saves")
        else:
            issues.add(Issue.MEDIUM, "NETWORK",
                       "No SD card caching detected during network outage",
                       "Expected SD card saves when MQTT is unavailable")

        injector.unblock()
        print("  Network restored. Waiting for MQTT reconnect...")

        # Wait for reconnection
        found, _ = monitor.wait_for_pattern(r"MQTT_EVENT_CONNECTED|MQTT:\s+CONNECTED", timeout=120)
        if not found:
            issues.add(Issue.HIGH, "NETWORK",
                       "MQTT did not reconnect within 2 minutes after network restore")
        else:
            print("  MQTT reconnected!")

        # Wait for SD replay
        time.sleep(30)
        if monitor.sd_replays > 0:
            print(f"  SD replay detected: {monitor.sd_replays} replays")

        # Check for crashes during disruption
        if monitor.crash_count > crashes_before:
            issues.add(Issue.CRITICAL, "NETWORK",
                       "Crash during network disruption")

        if monitor.reboot_count > reboots_before:
            issues.add(Issue.HIGH, "NETWORK",
                       "Unexpected reboot during network disruption",
                       "Device should continue running with SD caching when network is down")

        time.sleep(30)  # Recovery period

        # Test 2: Network flapping (if enabled)
        if cfg.TEST_NETWORK_FLAP:
            print("\n  --- Network Flapping (90s on / 30s off x 3) ---")
            crashes_before2 = monitor.crash_count

            for cycle in range(3):
                print(f"  Flap cycle {cycle+1}/3")
                injector.block()
                time.sleep(30)
                injector.unblock()
                time.sleep(90)

            if monitor.crash_count > crashes_before2:
                issues.add(Issue.CRITICAL, "NETWORK",
                           "Crash during network flapping")

            # Check heap didn't leak during flapping
            if monitor.heap_free < cfg.HEAP_MIN_ACCEPTABLE:
                issues.add(Issue.HIGH, "NETWORK",
                           f"Heap critically low after network flapping: {monitor.heap_free:,}")

    except Exception as e:
        issues.add(Issue.MEDIUM, "NETWORK", f"Network test error: {e}")
    finally:
        injector.cleanup()

    print("  Network disruption tests done.")


def phase_soak(monitor, issues, cfg, duration_min=60):
    """Phase 6: Long-duration soak test."""
    print("\n" + "="*60)
    print(f"  PHASE 6: SOAK TEST ({duration_min} min)")
    print("="*60)

    soak_start = time.time()
    heap_at_start = monitor.heap_free
    crashes_at_start = monitor.crash_count
    reboots_at_start = monitor.reboot_count
    errors_at_start = monitor.error_count
    telemetry_at_start = monitor.telemetry_sent
    telemetry_fail_at_start = monitor.telemetry_failed

    check_interval = 60  # Print status every 60 seconds
    end_time = soak_start + (duration_min * 60)

    while time.time() < end_time:
        time.sleep(check_interval)
        s = monitor.get_status()
        elapsed_min = (time.time() - soak_start) / 60
        remaining_min = (end_time - time.time()) / 60

        print(f"  [{int(elapsed_min)}m/{duration_min}m] "
              f"Heap: {s['heap_free']:,} (min:{s['heap_min']:,}) | "
              f"MQTT: {s['mqtt_status']} | "
              f"Tx: {s['telemetry_sent']} | "
              f"Err: {s['error_count']} | "
              f"SD: {s['sd_saves']}/{s['sd_errors']} | "
              f"[{remaining_min:.0f}m left]")

        # Real-time issue detection
        if s['heap_free'] < cfg.HEAP_MIN_ACCEPTABLE:
            issues.add(Issue.CRITICAL, "SOAK",
                       f"Heap critically low during soak: {s['heap_free']:,}")

        if s['crash_count'] > crashes_at_start:
            issues.add(Issue.CRITICAL, "SOAK",
                       f"Crash detected at {int(elapsed_min)}min into soak")
            crashes_at_start = s['crash_count']  # Don't re-report

        if s['reboot_count'] > reboots_at_start:
            issues.add(Issue.CRITICAL, "SOAK",
                       f"Unexpected reboot at {int(elapsed_min)}min into soak")
            reboots_at_start = s['reboot_count']

    # Soak analysis
    heap_at_end = monitor.heap_free
    heap_decline = heap_at_start - heap_at_end
    total_errors = monitor.error_count - errors_at_start
    total_tx = monitor.telemetry_sent - telemetry_at_start
    total_tx_fail = monitor.telemetry_failed - telemetry_fail_at_start

    # Heap leak detection
    leak_detected, leak_msg = monitor.check_heap_leak(
        window_minutes=max(duration_min - 5, 10),
        max_decline_bytes=cfg.HEAP_LEAK_MAX_KB * 1024
    )
    if leak_detected:
        issues.add(Issue.HIGH, "MEMORY", f"Heap leak detected during soak", leak_msg)

    # Telemetry reliability
    if total_tx > 0:
        fail_pct = (total_tx_fail / (total_tx + total_tx_fail)) * 100
        if fail_pct > cfg.MAX_TELEMETRY_FAIL_PERCENT:
            issues.add(Issue.HIGH, "TELEMETRY",
                       f"{fail_pct:.1f}% telemetry failure rate",
                       f"{total_tx_fail} failed out of {total_tx + total_tx_fail}")
    elif duration_min >= 10:
        issues.add(Issue.HIGH, "TELEMETRY",
                   "No telemetry sent during entire soak period")

    # SD card health
    if monitor.sd_errors > cfg.MAX_SD_ERRORS:
        issues.add(Issue.MEDIUM, "SD_CARD",
                   f"{monitor.sd_errors} SD card errors during test",
                   "Possible SD card degradation or loose connection")

    print(f"\n  Soak done: {duration_min}min, heap {heap_at_start:,}->{heap_at_end:,} "
          f"(delta:{heap_decline:+,}), tx:{total_tx}, errors:{total_errors}")


# ============================================================================
#  REPORT GENERATOR
# ============================================================================

def generate_final_report(issues, monitor, start_time, cfg):
    """Generate the final comprehensive issues report."""
    duration = time.time() - start_time
    duration_str = str(timedelta(seconds=int(duration)))
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_path = os.path.join(config.REPORTS_DIR, f"FULL_TEST_REPORT_{ts}.txt")
    os.makedirs(config.REPORTS_DIR, exist_ok=True)

    status = monitor.get_status() if monitor else {}
    summary = issues.summary()
    total_issues = len(issues.issues)

    # Determine overall verdict
    if summary.get(Issue.CRITICAL, 0) > 0:
        verdict = "FAILED - CRITICAL ISSUES FOUND"
    elif summary.get(Issue.HIGH, 0) > 0:
        verdict = "FAILED - HIGH PRIORITY ISSUES"
    elif summary.get(Issue.MEDIUM, 0) > 0:
        verdict = "PASSED WITH WARNINGS"
    else:
        verdict = "PASSED"

    lines = []
    lines.append("=" * 70)
    lines.append("  ESP32 MODBUS IOT GATEWAY - FULL TEST REPORT")
    lines.append("=" * 70)
    lines.append(f"  Date:     {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"  Duration: {duration_str}")
    lines.append(f"  Verdict:  {verdict}")
    lines.append(f"  Issues:   {total_issues} total "
                 f"({summary.get(Issue.CRITICAL,0)} critical, "
                 f"{summary.get(Issue.HIGH,0)} high, "
                 f"{summary.get(Issue.MEDIUM,0)} medium, "
                 f"{summary.get(Issue.LOW,0)} low)")
    lines.append("=" * 70)

    # Device final status
    lines.append("\n  DEVICE FINAL STATUS")
    lines.append("  " + "-" * 40)
    if status:
        lines.append(f"  Heap Free:        {status.get('heap_free', 'N/A'):,} bytes")
        lines.append(f"  Heap Min:         {status.get('heap_min', 'N/A'):,} bytes")
        lines.append(f"  MQTT Status:      {status.get('mqtt_status', 'N/A')}")
        lines.append(f"  MQTT Connects:    {status.get('mqtt_connects', 0)}")
        lines.append(f"  MQTT Disconnects: {status.get('mqtt_disconnects', 0)}")
        lines.append(f"  Telemetry Sent:   {status.get('telemetry_sent', 0)}")
        lines.append(f"  Telemetry Failed: {status.get('telemetry_failed', 0)}")
        lines.append(f"  Crashes:          {status.get('crash_count', 0)}")
        lines.append(f"  Reboots:          {status.get('reboot_count', 0)}")
        lines.append(f"  SD Saves:         {status.get('sd_saves', 0)}")
        lines.append(f"  SD Errors:        {status.get('sd_errors', 0)}")
        lines.append(f"  SD Replays:       {status.get('sd_replays', 0)}")
        lines.append(f"  Error Count:      {status.get('error_count', 0)}")

    # Heap trend
    if monitor:
        leak, leak_msg = monitor.check_heap_leak(window_minutes=60)
        lines.append(f"\n  HEAP LEAK CHECK: {'LEAK DETECTED' if leak else 'OK'}")
        lines.append(f"  {leak_msg}")

    # Issues by severity
    for severity in [Issue.CRITICAL, Issue.HIGH, Issue.MEDIUM, Issue.LOW, Issue.INFO]:
        sev_issues = issues.get_by_severity(severity)
        if sev_issues:
            lines.append(f"\n  {severity} ISSUES ({len(sev_issues)})")
            lines.append("  " + "-" * 50)
            for sev, cat, title, details, ts_str in sev_issues:
                lines.append(f"  [{ts_str}] [{cat}] {title}")
                if details:
                    lines.append(f"           {details[:100]}")

    # Issues by category
    categories = set(i[1] for i in issues.issues)
    if categories:
        lines.append(f"\n  ISSUES BY CATEGORY")
        lines.append("  " + "-" * 50)
        for cat in sorted(categories):
            cat_issues = issues.get_by_category(cat)
            lines.append(f"  {cat}: {len(cat_issues)} issues")

    # Recent errors from serial monitor
    if monitor and monitor.errors:
        lines.append(f"\n  RECENT DEVICE ERRORS (last 20)")
        lines.append("  " + "-" * 50)
        for ts_str, msg in list(monitor.errors)[-20:]:
            lines.append(f"  [{ts_str}] {msg[:90]}")

    # Recommendations
    lines.append(f"\n  RECOMMENDATIONS")
    lines.append("  " + "-" * 50)
    if summary.get(Issue.CRITICAL, 0) > 0:
        lines.append("  1. Fix all CRITICAL issues before deploying to production")
    if any(i[1] == "MEMORY" for i in issues.issues):
        lines.append("  2. Investigate heap memory trends - possible memory leak")
    if any(i[1] == "NETWORK" for i in issues.issues):
        lines.append("  3. Review MQTT reconnection and SD caching logic")
    if any(i[1] == "SECURITY" for i in issues.issues):
        lines.append("  4. Review input validation in web form handlers")
    if any(i[1] == "WEB" for i in issues.issues):
        lines.append("  5. Review web server socket/memory management")
    if not issues.issues:
        lines.append("  No issues found! Device appears stable.")

    lines.append(f"\n  Serial log CSV: {monitor.csv_path if monitor else 'N/A'}")
    lines.append(f"  Report saved to: {report_path}")
    lines.append("\n" + "=" * 70)

    report_text = "\n".join(lines)

    # Save to file
    with open(report_path, "w") as f:
        f.write(report_text)

    # Print to console
    print("\n\n" + report_text)

    return report_path


# ============================================================================
#  MAIN RUNNER
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="ESP32 Full Automated Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Example: python full_test_runner.py --hours 4 --skip-flash"
    )
    parser.add_argument("--hours", type=float, default=2,
                        help="Total test duration in hours (default: 2)")
    parser.add_argument("--skip-flash", action="store_true",
                        help="Skip firmware flashing")
    parser.add_argument("--skip-web", action="store_true",
                        help="Skip web UI tests")
    parser.add_argument("--skip-network", action="store_true",
                        help="Skip network disruption tests")
    parser.add_argument("--port", default=None,
                        help="Serial port (default: from config.py)")
    parser.add_argument("--ip", default=None,
                        help="Device IP (default: from config.py)")
    args = parser.parse_args()

    # Apply CLI args to config
    cfg = TestConfig()
    cfg.TOTAL_TEST_HOURS = args.hours
    if args.skip_flash:
        cfg.FLASH_ENABLED = False
    if args.skip_web:
        cfg.TEST_WEB_STRESS = False
        cfg.TEST_WEB_FUZZ = False
    if args.skip_network:
        cfg.TEST_NETWORK_DISRUPTION = False
        cfg.TEST_NETWORK_FLAP = False
    if args.port:
        cfg.SERIAL_PORT = args.port
        cfg.FLASH_PORT = args.port
    if args.ip:
        cfg.DEVICE_IP = args.ip

    total_minutes = int(cfg.TOTAL_TEST_HOURS * 60)

    print("\n" + "#" * 70)
    print("#" + " " * 68 + "#")
    print("#   ESP32 MODBUS IOT GATEWAY - FULL AUTOMATED TEST RUNNER" + " " * 12 + "#")
    print("#" + " " * 68 + "#")
    print("#" * 70)
    print(f"\n  Duration:  {cfg.TOTAL_TEST_HOURS} hours ({total_minutes} minutes)")
    print(f"  Port:      {cfg.SERIAL_PORT}")
    print(f"  Device IP: {cfg.DEVICE_IP}")
    print(f"  Flash:     {'Yes' if cfg.FLASH_ENABLED else 'Skip'}")
    print(f"  Web tests: {'Yes' if cfg.TEST_WEB_STRESS else 'Skip'}")
    print(f"  Net tests: {'Yes' if cfg.TEST_NETWORK_DISRUPTION else 'Skip'}")
    if cfg.TEST_NETWORK_DISRUPTION and HAS_NETWORK and not is_admin():
        print(f"  WARNING:   Not running as Admin - network tests will be skipped")
    print()

    issues = IssueTracker()
    monitor = None
    start_time = time.time()

    try:
        # Phase 0: Flash
        if not phase_flash(cfg, issues):
            generate_final_report(issues, None, start_time, cfg)
            return

        # Start serial monitor
        time.sleep(3)  # Wait for serial port to be available after flash
        monitor = SerialMonitor(port=cfg.SERIAL_PORT, baud=cfg.SERIAL_BAUD)
        monitor.start()

        # Phase 1: Boot verification
        if not phase_boot_verify(monitor, issues):
            generate_final_report(issues, monitor, start_time, cfg)
            return

        # Phase 2: Baseline (10% of total time, min 5 min, max 15 min)
        baseline_min = max(5, min(15, int(total_minutes * 0.1)))
        phase_baseline(monitor, issues, duration_min=baseline_min)

        # Phase 3: Web stress
        phase_web_stress(monitor, issues, cfg)

        # Phase 4: Web fuzzing
        phase_web_fuzz(monitor, issues, cfg)

        # Phase 5: Network disruption
        phase_network_disruption(monitor, issues, cfg)

        # Phase 6: Soak (remaining time)
        elapsed = (time.time() - start_time) / 60
        soak_min = max(10, int(total_minutes - elapsed - 2))
        phase_soak(monitor, issues, cfg, duration_min=soak_min)

    except KeyboardInterrupt:
        print("\n\n  TEST INTERRUPTED BY USER")
        issues.add(Issue.INFO, "SYSTEM", "Test interrupted by user")
    except Exception as e:
        issues.add(Issue.HIGH, "SYSTEM", f"Test runner error: {e}")
    finally:
        # Generate final report
        report_path = generate_final_report(issues, monitor, start_time, cfg)

        # Cleanup
        if monitor:
            monitor.stop()

        print(f"\n  Test complete! Report: {report_path}")


if __name__ == "__main__":
    main()
