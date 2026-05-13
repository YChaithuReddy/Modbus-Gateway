#!/usr/bin/env python3
"""
Web UI Stress Tester - Hammers ESP32 web server endpoints to find memory leaks,
crashes, and edge cases.

Usage:
    python web_ui_stress_tester.py                          # Run all tests
    python web_ui_stress_tester.py --test flood             # Connection flood only
    python web_ui_stress_tester.py --test fuzz              # Form fuzzing only
    python web_ui_stress_tester.py --concurrent 5 --duration 120
"""

import argparse
import asyncio
import csv
import json
import os
import time
from datetime import datetime

import aiohttp
import requests

import config


class WebStressTester:
    """Stress test ESP32 web endpoints."""

    def __init__(self, device_ip=None):
        self.base_url = f"http://{device_ip or config.DEVICE_IP}:{config.DEVICE_HTTP_PORT}"
        self.session_cookie = None
        self.results = []  # (timestamp, endpoint, status, response_time_ms, error)
        self.heap_readings = []

    def login(self):
        """Authenticate and get session cookie."""
        try:
            resp = requests.post(
                f"{self.base_url}/api/login",
                json={"username": config.WEB_USERNAME, "password": config.WEB_PASSWORD},
                timeout=10
            )
            if resp.status_code == 200:
                self.session_cookie = resp.cookies.get_dict()
                print(f"[WebStress] Logged in successfully")
                return True
            print(f"[WebStress] Login failed: {resp.status_code}")
        except Exception as e:
            print(f"[WebStress] Login error: {e}")
        return False

    def get_heap(self):
        """Get current heap from system status API."""
        try:
            resp = requests.get(
                f"{self.base_url}/api/system_status",
                cookies=self.session_cookie,
                timeout=10
            )
            if resp.status_code == 200:
                data = resp.json()
                heap = data.get("free_heap", data.get("heap_free", 0))
                self.heap_readings.append((time.time(), heap))
                return heap
        except Exception:
            pass
        return None

    def _record(self, endpoint, status, elapsed_ms, error=None):
        """Record a test result."""
        self.results.append((
            datetime.now().isoformat(), endpoint, status,
            f"{elapsed_ms:.1f}", error or ""
        ))

    # === Test: Baseline Polling ===
    async def test_baseline(self, duration_sec=60, interval=5):
        """Poll system status at regular intervals, track heap."""
        print(f"[WebStress] Baseline test: {duration_sec}s at {interval}s intervals")
        start = time.time()
        async with aiohttp.ClientSession(cookies=self.session_cookie) as session:
            while time.time() - start < duration_sec:
                t0 = time.time()
                try:
                    async with session.get(
                        f"{self.base_url}/api/system_status", timeout=aiohttp.ClientTimeout(total=10)
                    ) as resp:
                        elapsed = (time.time() - t0) * 1000
                        self._record("/api/system_status", resp.status, elapsed)
                        if resp.status == 200:
                            data = await resp.json()
                            heap = data.get("free_heap", data.get("heap_free", 0))
                            self.heap_readings.append((time.time(), heap))
                except Exception as e:
                    elapsed = (time.time() - t0) * 1000
                    self._record("/api/system_status", 0, elapsed, str(e))
                await asyncio.sleep(interval)
        print(f"  Baseline done. {len(self.heap_readings)} heap readings")

    # === Test: Connection Flood ===
    async def test_connection_flood(self, concurrent=5, requests_per=20):
        """Open multiple concurrent connections and hit endpoints."""
        print(f"[WebStress] Connection flood: {concurrent} concurrent x {requests_per} requests")
        endpoints = [
            "/api/system_status",
            "/live_data",
        ]

        async def _worker(session, worker_id):
            for i in range(requests_per):
                ep = endpoints[i % len(endpoints)]
                t0 = time.time()
                try:
                    async with session.get(
                        f"{self.base_url}{ep}",
                        timeout=aiohttp.ClientTimeout(total=15)
                    ) as resp:
                        await resp.read()
                        elapsed = (time.time() - t0) * 1000
                        self._record(ep, resp.status, elapsed)
                except Exception as e:
                    elapsed = (time.time() - t0) * 1000
                    self._record(ep, 0, elapsed, str(e))
                await asyncio.sleep(0.1)

        async with aiohttp.ClientSession(cookies=self.session_cookie) as session:
            tasks = [_worker(session, i) for i in range(concurrent)]
            await asyncio.gather(*tasks)

        ok = sum(1 for r in self.results if r[2] == 200)
        fail = len(self.results) - ok
        print(f"  Flood done. OK: {ok}, Failed: {fail}")

    # === Test: Large Page Load ===
    async def test_large_page_load(self, count=10):
        """Repeatedly load the main config page (largest response)."""
        print(f"[WebStress] Large page load: GET / x {count}")
        async with aiohttp.ClientSession(cookies=self.session_cookie) as session:
            for i in range(count):
                t0 = time.time()
                try:
                    async with session.get(
                        f"{self.base_url}/",
                        timeout=aiohttp.ClientTimeout(total=30)
                    ) as resp:
                        body = await resp.read()
                        elapsed = (time.time() - t0) * 1000
                        self._record("/", resp.status, elapsed)
                        print(f"  Page {i+1}: {resp.status} ({len(body)} bytes, {elapsed:.0f}ms)")
                except Exception as e:
                    elapsed = (time.time() - t0) * 1000
                    self._record("/", 0, elapsed, str(e))
                    print(f"  Page {i+1}: ERROR ({e})")
                await asyncio.sleep(1)

    # === Test: Rapid API Polling ===
    async def test_rapid_polling(self, duration_sec=60):
        """Hit APIs as fast as possible for stress testing."""
        print(f"[WebStress] Rapid polling: {duration_sec}s")
        endpoints = ["/api/system_status", "/live_data"]
        count = 0
        errors = 0
        start = time.time()

        async with aiohttp.ClientSession(cookies=self.session_cookie) as session:
            while time.time() - start < duration_sec:
                ep = endpoints[count % len(endpoints)]
                t0 = time.time()
                try:
                    async with session.get(
                        f"{self.base_url}{ep}",
                        timeout=aiohttp.ClientTimeout(total=10)
                    ) as resp:
                        await resp.read()
                        elapsed = (time.time() - t0) * 1000
                        self._record(ep, resp.status, elapsed)
                        count += 1
                except Exception as e:
                    elapsed = (time.time() - t0) * 1000
                    self._record(ep, 0, elapsed, str(e))
                    errors += 1
                    count += 1
                    await asyncio.sleep(0.5)  # Back off on errors

        rps = count / max(time.time() - start, 1)
        print(f"  Rapid poll done. {count} requests ({rps:.1f}/s), {errors} errors")

    # === Test: Form Fuzzing ===
    async def test_form_fuzzing(self):
        """Submit sensor config forms with edge-case data."""
        print("[WebStress] Form fuzzing: testing edge cases")

        fuzz_payloads = [
            # Empty fields
            {"sensor_0_name": "", "sensor_0_unit_id": "", "sensor_0_slave_id": ""},
            # Extremely long strings
            {"sensor_0_name": "A" * 500, "sensor_0_unit_id": "B" * 500},
            # XSS payloads
            {"sensor_0_name": "<script>alert(1)</script>"},
            {"sensor_0_name": "';DROP TABLE sensors;--"},
            {"sensor_0_unit_id": "<img src=x onerror=alert(1)>"},
            # Integer overflow
            {"sensor_0_slave_id": "999999", "sensor_0_register_address": "-1"},
            {"sensor_0_slave_id": "0", "sensor_0_quantity": "65536"},
            # Invalid types
            {"sensor_0_data_type": "INVALID_TYPE", "sensor_0_byte_order": "GARBAGE"},
            {"sensor_0_sensor_type": "'; exec('rm -rf /');"},
            # Null bytes
            {"sensor_0_name": "test\x00hidden", "sensor_0_unit_id": "id\x00evil"},
            # Special characters
            {"sensor_0_name": "sensor/../../etc/passwd"},
            {"sensor_0_name": "test&param=injected"},
            # Very large numbers
            {"sensor_0_register_address": "4294967295", "sensor_0_scale_factor": "1e308"},
            # Negative values
            {"sensor_0_slave_id": "-1", "sensor_0_quantity": "-10"},
        ]

        async with aiohttp.ClientSession(cookies=self.session_cookie) as session:
            for i, payload in enumerate(fuzz_payloads):
                t0 = time.time()
                try:
                    async with session.post(
                        f"{self.base_url}/save_single_sensor",
                        data=payload,
                        timeout=aiohttp.ClientTimeout(total=10)
                    ) as resp:
                        elapsed = (time.time() - t0) * 1000
                        self._record("/save_single_sensor[fuzz]", resp.status, elapsed)
                        body = await resp.text()
                        truncated = body[:100].replace("\n", " ")
                        print(f"  Fuzz {i+1:2d}: status={resp.status} ({elapsed:.0f}ms) "
                              f"payload_keys={list(payload.keys())}")
                except Exception as e:
                    elapsed = (time.time() - t0) * 1000
                    self._record("/save_single_sensor[fuzz]", 0, elapsed, str(e))
                    print(f"  Fuzz {i+1:2d}: ERROR: {e}")
                await asyncio.sleep(0.5)

        # Check device is still alive after fuzzing
        heap = self.get_heap()
        print(f"  Fuzzing done. Heap after fuzzing: {heap}")

    # === Test: Concurrent Sessions ===
    async def test_max_sessions(self):
        """Try to exceed max session limit (3 concurrent)."""
        print("[WebStress] Max sessions test: trying 5 concurrent logins")
        sessions = []
        for i in range(5):
            try:
                resp = requests.post(
                    f"{self.base_url}/api/login",
                    json={"username": config.WEB_USERNAME, "password": config.WEB_PASSWORD},
                    timeout=10
                )
                sessions.append((i, resp.status_code, resp.cookies.get_dict()))
                print(f"  Login {i+1}: status={resp.status_code}")
            except Exception as e:
                print(f"  Login {i+1}: ERROR: {e}")
            time.sleep(0.5)

    def save_report(self, filename=None):
        """Save results to CSV."""
        if not filename:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = os.path.join(config.REPORTS_DIR, f"web_stress_{ts}.csv")
        os.makedirs(os.path.dirname(filename), exist_ok=True)
        with open(filename, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["timestamp", "endpoint", "status", "response_ms", "error"])
            writer.writerows(self.results)
        print(f"[WebStress] Report saved to {filename}")

        # Summary
        total = len(self.results)
        ok = sum(1 for r in self.results if r[2] == 200)
        errs = total - ok
        avg_ms = sum(float(r[3]) for r in self.results) / max(total, 1)
        print(f"  Total: {total} requests, OK: {ok}, Errors: {errs}, Avg: {avg_ms:.0f}ms")

        if self.heap_readings:
            first_heap = self.heap_readings[0][1]
            last_heap = self.heap_readings[-1][1]
            min_heap = min(h for _, h in self.heap_readings)
            print(f"  Heap: start={first_heap}, end={last_heap}, min={min_heap}, "
                  f"delta={last_heap - first_heap}")

        return filename


async def run_all_tests(tester, duration=60, concurrent=3):
    """Run the complete test suite."""
    print(f"\n{'='*60}")
    print(f"  ESP32 Web Stress Test Suite")
    print(f"  Target: {tester.base_url}")
    print(f"{'='*60}\n")

    heap_before = tester.get_heap()
    print(f"Initial heap: {heap_before}\n")

    # 1. Baseline
    await tester.test_baseline(duration_sec=30, interval=5)
    print()

    # 2. Connection flood
    await tester.test_connection_flood(concurrent=concurrent, requests_per=15)
    print()

    # 3. Large page loads
    await tester.test_large_page_load(count=5)
    print()

    # 4. Rapid polling
    await tester.test_rapid_polling(duration_sec=min(duration, 30))
    print()

    # 5. Form fuzzing
    await tester.test_form_fuzzing()
    print()

    # 6. Max sessions
    await tester.test_max_sessions()
    print()

    heap_after = tester.get_heap()
    print(f"\nFinal heap: {heap_after}")
    if heap_before and heap_after:
        print(f"Heap change: {heap_after - heap_before:+d} bytes")

    tester.save_report()


def main():
    parser = argparse.ArgumentParser(description="ESP32 Web UI Stress Tester")
    parser.add_argument("--ip", default=config.DEVICE_IP)
    parser.add_argument("--test", choices=["all", "baseline", "flood", "pages", "rapid", "fuzz", "sessions"],
                        default="all")
    parser.add_argument("--concurrent", type=int, default=3)
    parser.add_argument("--duration", type=int, default=60)
    args = parser.parse_args()

    tester = WebStressTester(device_ip=args.ip)
    tester.login()

    if args.test == "all":
        asyncio.run(run_all_tests(tester, args.duration, args.concurrent))
    elif args.test == "baseline":
        asyncio.run(tester.test_baseline(args.duration))
        tester.save_report()
    elif args.test == "flood":
        asyncio.run(tester.test_connection_flood(args.concurrent))
        tester.save_report()
    elif args.test == "pages":
        asyncio.run(tester.test_large_page_load(10))
        tester.save_report()
    elif args.test == "rapid":
        asyncio.run(tester.test_rapid_polling(args.duration))
        tester.save_report()
    elif args.test == "fuzz":
        asyncio.run(tester.test_form_fuzzing())
        tester.save_report()
    elif args.test == "sessions":
        asyncio.run(tester.test_max_sessions())


if __name__ == "__main__":
    main()
