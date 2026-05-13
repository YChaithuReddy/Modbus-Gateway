#!/usr/bin/env python3
"""
Test Orchestrator - Runs automated test scenarios by coordinating all
test components (serial monitor, network fault injector, web stress, etc.)

Usage:
    python orchestrator.py scenarios/basic_stability.yaml
    python orchestrator.py scenarios/network_disruption.yaml
    python orchestrator.py scenarios/full_soak.yaml
"""

import argparse
import json
import os
import sys
import time
from datetime import datetime

import yaml

import config
from serial_monitor import SerialMonitor
from network_fault_injector import NetworkFaultInjector
from web_ui_stress_tester import WebStressTester


class TestResult:
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    WARN = "WARN"


class TestOrchestrator:
    """Coordinates test components and executes scenarios."""

    def __init__(self):
        self.serial_monitor = None
        self.network_injector = None
        self.web_tester = None
        self.results = []  # (timestamp, step_name, result, details)
        self.start_time = None
        self.scenario_name = ""

    def setup(self, enable_serial=True, enable_network=False):
        """Initialize test components."""
        if enable_serial:
            self.serial_monitor = SerialMonitor()
            self.serial_monitor.start()

        if enable_network:
            self.network_injector = NetworkFaultInjector()

        self.web_tester = WebStressTester()
        self.start_time = time.time()

    def teardown(self):
        """Clean up all components."""
        if self.serial_monitor:
            self.serial_monitor.stop()
        if self.network_injector:
            self.network_injector.cleanup()
        print("\n[Orchestrator] All components stopped.")

    def run_scenario(self, scenario_path):
        """Load and execute a YAML test scenario."""
        with open(scenario_path) as f:
            scenario = yaml.safe_load(f)

        self.scenario_name = scenario.get("name", os.path.basename(scenario_path))
        duration = scenario.get("duration_minutes", 30) * 60
        steps = scenario.get("steps", [])
        enable_network = any(s.get("action", "").startswith("block") or
                            s.get("action", "").startswith("unblock") or
                            s.get("action") == "flap_network"
                            for s in steps)

        print(f"\n{'='*60}")
        print(f"  Test Scenario: {self.scenario_name}")
        print(f"  Duration: {scenario.get('duration_minutes', 30)} minutes")
        print(f"  Steps: {len(steps)}")
        print(f"{'='*60}\n")

        self.setup(enable_serial=True, enable_network=enable_network)

        # Sort steps by 'at' time (minutes)
        steps.sort(key=lambda s: s.get("at", 0))

        try:
            for step in steps:
                at_min = step.get("at", 0)
                at_sec = at_min * 60
                action = step.get("action", "")
                params = step.get("params", {})

                # Wait until scheduled time
                elapsed = time.time() - self.start_time
                wait = at_sec - elapsed
                if wait > 0:
                    print(f"\n  [Waiting {wait:.0f}s until t={at_min}min for: {action}]")
                    time.sleep(wait)

                print(f"\n--- [{at_min}min] {action} ---")
                self._execute_step(action, params)

            # Wait for remaining duration
            elapsed = time.time() - self.start_time
            remaining = duration - elapsed
            if remaining > 0:
                print(f"\n  [Soak: waiting {remaining:.0f}s until scenario ends]")
                # Print status every 30 seconds during soak
                while time.time() - self.start_time < duration:
                    time.sleep(30)
                    if self.serial_monitor:
                        s = self.serial_monitor.get_status()
                        print(f"  [Soak] Heap: {s['heap_free']:,} | MQTT: {s['mqtt_status']} | "
                              f"Err: {s['error_count']} | Crashes: {s['crash_count']}")

        except KeyboardInterrupt:
            print("\n\n  Scenario interrupted by user.")
        finally:
            self._generate_report()
            self.teardown()

    def _execute_step(self, action, params):
        """Execute a single test step."""
        try:
            if action == "wait":
                secs = params.get("seconds", 10)
                print(f"  Waiting {secs}s...")
                time.sleep(secs)
                self._record(action, TestResult.PASS, f"Waited {secs}s")

            elif action == "assert_heap_above":
                threshold = params.get("threshold", config.HEAP_WARNING)
                if self.serial_monitor:
                    heap = self.serial_monitor.heap_free
                    if heap > threshold:
                        self._record(action, TestResult.PASS,
                                     f"Heap {heap:,} > {threshold:,}")
                    else:
                        self._record(action, TestResult.FAIL,
                                     f"Heap {heap:,} <= {threshold:,}")

            elif action == "check_heap_trend":
                max_decline = params.get("max_decline_kb", 5) * 1024
                window = params.get("window_minutes", 30)
                if self.serial_monitor:
                    leak, msg = self.serial_monitor.check_heap_leak(window, max_decline)
                    if leak:
                        self._record(action, TestResult.FAIL, msg)
                    else:
                        self._record(action, TestResult.PASS, msg)

            elif action == "assert_no_crashes":
                if self.serial_monitor:
                    crashes = self.serial_monitor.crash_count
                    if crashes == 0:
                        self._record(action, TestResult.PASS, "No crashes")
                    else:
                        self._record(action, TestResult.FAIL, f"{crashes} crashes detected")

            elif action == "assert_no_reboots":
                if self.serial_monitor:
                    reboots = self.serial_monitor.reboot_count
                    if reboots == 0:
                        self._record(action, TestResult.PASS, "No reboots")
                    else:
                        self._record(action, TestResult.FAIL, f"{reboots} reboots detected")

            elif action == "assert_mqtt_connected":
                if self.serial_monitor:
                    status = self.serial_monitor.mqtt_status
                    if status == "CONNECTED":
                        self._record(action, TestResult.PASS, "MQTT connected")
                    else:
                        self._record(action, TestResult.FAIL, f"MQTT status: {status}")

            elif action == "assert_serial_pattern":
                pattern = params.get("pattern", "")
                timeout = params.get("timeout", 60)
                if self.serial_monitor:
                    found, matches = self.serial_monitor.wait_for_pattern(pattern, timeout)
                    if found:
                        self._record(action, TestResult.PASS,
                                     f"Pattern '{pattern}' found: {matches[0][:80]}")
                    else:
                        self._record(action, TestResult.FAIL,
                                     f"Pattern '{pattern}' not found within {timeout}s")

            elif action == "block_network":
                duration_sec = params.get("duration_sec", 60)
                if self.network_injector:
                    self.network_injector.block(duration_sec)
                    self._record(action, TestResult.PASS, f"Blocked for {duration_sec}s")

            elif action == "unblock_network":
                if self.network_injector:
                    self.network_injector.unblock()
                    self._record(action, TestResult.PASS, "Network unblocked")

            elif action == "flap_network":
                on_sec = params.get("on_seconds", 120)
                off_sec = params.get("off_seconds", 30)
                cycles = params.get("cycles", 3)
                if self.network_injector:
                    from network_fault_injector import flap_network
                    flap_network(on_sec, off_sec, cycles)
                    self._record(action, TestResult.PASS,
                                 f"Flapped {cycles} cycles ({on_sec}s/{off_sec}s)")

            elif action == "web_stress":
                concurrent = params.get("concurrent", 3)
                duration = params.get("duration_sec", 60)
                import asyncio
                asyncio.run(self.web_tester.test_connection_flood(concurrent))
                self._record(action, TestResult.PASS,
                             f"Web stress: {concurrent} concurrent")

            elif action == "web_fuzz":
                import asyncio
                self.web_tester.login()
                asyncio.run(self.web_tester.test_form_fuzzing())
                self._record(action, TestResult.PASS, "Form fuzzing complete")

            elif action == "print_status":
                if self.serial_monitor:
                    s = self.serial_monitor.get_status()
                    for k, v in s.items():
                        print(f"    {k}: {v}")
                    self._record(action, TestResult.PASS, json.dumps(s))

            else:
                self._record(action, TestResult.SKIP, f"Unknown action: {action}")

        except Exception as e:
            self._record(action, TestResult.FAIL, f"Exception: {e}")

    def _record(self, step, result, details):
        """Record a test result."""
        elapsed = time.time() - self.start_time if self.start_time else 0
        entry = (datetime.now().isoformat(), f"{elapsed:.0f}s", step, result, details)
        self.results.append(entry)
        icon = {"PASS": "OK", "FAIL": "FAIL", "SKIP": "SKIP", "WARN": "WARN"}[result]
        print(f"  [{icon}] {step}: {details}")

    def _generate_report(self):
        """Generate summary report."""
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_path = os.path.join(config.REPORTS_DIR, f"test_report_{ts}.txt")
        os.makedirs(config.REPORTS_DIR, exist_ok=True)

        total = len(self.results)
        passed = sum(1 for r in self.results if r[3] == TestResult.PASS)
        failed = sum(1 for r in self.results if r[3] == TestResult.FAIL)
        duration = time.time() - self.start_time if self.start_time else 0

        lines = [
            f"{'='*60}",
            f"  TEST REPORT: {self.scenario_name}",
            f"  Date: {datetime.now().isoformat()}",
            f"  Duration: {duration/60:.1f} minutes",
            f"  Results: {passed} PASS / {failed} FAIL / {total} total",
            f"{'='*60}",
            "",
        ]

        # Serial monitor summary
        if self.serial_monitor:
            s = self.serial_monitor.get_status()
            lines.append("DEVICE STATUS:")
            for k, v in s.items():
                lines.append(f"  {k}: {v}")
            lines.append("")

            # Heap leak check
            leak, msg = self.serial_monitor.check_heap_leak()
            lines.append(f"HEAP LEAK CHECK: {'LEAK DETECTED' if leak else 'OK'} - {msg}")
            lines.append("")

        # Step results
        lines.append("STEP RESULTS:")
        for ts_str, elapsed, step, result, details in self.results:
            lines.append(f"  [{elapsed:>6s}] {result:4s} {step}: {details}")
        lines.append("")

        # Errors
        if self.serial_monitor and self.serial_monitor.errors:
            lines.append(f"ERRORS ({len(self.serial_monitor.errors)}):")
            for ts_str, msg in list(self.serial_monitor.errors)[-20:]:
                lines.append(f"  [{ts_str}] {msg}")

        report_text = "\n".join(lines)
        with open(report_path, "w") as f:
            f.write(report_text)

        print(f"\n{report_text}")
        print(f"\nReport saved to: {report_path}")
        if self.serial_monitor:
            print(f"Serial CSV: {self.serial_monitor.csv_path}")


def main():
    parser = argparse.ArgumentParser(description="Test Orchestrator")
    parser.add_argument("scenario", help="Path to YAML scenario file")
    args = parser.parse_args()

    if not os.path.exists(args.scenario):
        print(f"ERROR: Scenario file not found: {args.scenario}")
        sys.exit(1)

    orchestrator = TestOrchestrator()
    orchestrator.run_scenario(args.scenario)


if __name__ == "__main__":
    main()
