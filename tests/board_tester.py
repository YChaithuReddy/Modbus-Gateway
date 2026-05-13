"""
=============================================================
  PCB Board RS485 Tester v4.0
=============================================================

  Tests RS485 on new PCBs using ONE pre-flashed ESP32.
  Sends "TEST" via USB serial → ESP32 reads sensors instantly.

  SETUP (one time):
    1. Flash ESP32 with firmware (idf.py build flash)
    2. Configure Dalian sensor (slave 1) via web UI
    3. Done - this ESP32 is your test module

  TESTING (per PCB):
    1. Place ESP32 on new PCB
    2. Connect Dalian flow meter RS485 A/B
    3. Plug USB, press Enter
    4. PASS or FAIL in ~3 seconds

  Usage:
    python board_tester.py [--port COM5]
    python board_tester.py batch [--port COM5]
"""

import serial
import serial.tools.list_ports
import sys
import os
import time
import re
import argparse
import csv
from datetime import datetime

# ============================================================
DEFAULT_PORT = "COM5"
BAUD_RATE = 115200
RESULTS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rs485_test_results.csv")

# Default test sensor: Dalian Ultrasonic Flow Meter, Slave ID 1, 9600 baud
# ESP32 firmware is pre-configured with this sensor - no changes needed per board


# Serial patterns
RE_TEST_PASS = re.compile(r"\[RS485_TEST\] PASS")
RE_TEST_FAIL = re.compile(r"\[RS485_TEST\] FAIL")
RE_TEST_SENSOR = re.compile(r"\[RS485_TEST\] Sensor (\S+) = ([\d.]+)")
RE_MODBUS_RECV = re.compile(r"\[RECV\] Received \d+ bytes")
RE_REGISTER = re.compile(r"\[DATA\] Register\[\d+\]: (0x[0-9A-Fa-f]+ \(\d+\))")
RE_MODBUS_TIMEOUT = re.compile(r"Modbus timeout|No response")
RE_CRASH = re.compile(r"Guru Meditation|stack overflow|panic_abort")


def detect_port():
    ports = serial.tools.list_ports.comports()
    esp_ports = [p for p in ports if any(k in p.description.upper()
                 for k in ["USB", "CP210", "CH340", "FTDI", "SILICON"])]
    if len(esp_ports) == 1:
        print(f"  Auto-detected: {esp_ports[0].device}")
        return esp_ports[0].device
    elif len(esp_ports) > 1:
        print("  Multiple ports:")
        for i, p in enumerate(esp_ports):
            print(f"    [{i}] {p.device} - {p.description}")
        choice = input("  Select: ").strip()
        if choice.isdigit() and int(choice) < len(esp_ports):
            return esp_ports[int(choice)].device
    return DEFAULT_PORT


def log_result(label, status, details=""):
    file_exists = os.path.exists(RESULTS_FILE) and os.path.getsize(RESULTS_FILE) > 0
    with open(RESULTS_FILE, "a", newline="") as f:
        writer = csv.writer(f)
        if not file_exists:
            writer.writerow(["timestamp", "board_label", "status", "details"])
        writer.writerow([datetime.now().strftime("%Y-%m-%d %H:%M:%S"), label, status, details])


def test_board(ser, label):
    """Send TEST command and watch for RS485_TEST result."""

    MAX_RETRIES = 3

    print()
    print("=" * 50)
    print(f"  TESTING: {label}  (up to {MAX_RETRIES} attempts)")
    print("=" * 50)

    for attempt in range(1, MAX_RETRIES + 1):
        result = _single_test(ser, label, attempt)

        if result == "PASS":
            print()
            print("  ╔═════════════════════════════════════════╗")
            print(f"  ║   PASS   {label:>12s}   RS485 OK       ║")
            print("  ╚═════════════════════════════════════════╝")
            log_result(label, "PASS", f"Attempt {attempt}")
            return True

        elif result == "FAIL" and attempt < MAX_RETRIES:
            print(f"  Retry {attempt + 1}/{MAX_RETRIES} in 2 seconds...")
            time.sleep(2)
            ser.reset_input_buffer()
            continue

        elif result == "FAIL":
            print()
            print("  ╔═════════════════════════════════════════╗")
            print(f"  ║   FAIL   {label:>12s}   RS485 ERROR    ║")
            print("  ╚═════════════════════════════════════════╝")
            print()
            print("  Check:")
            print("    1. RS485 transceiver soldering")
            print("    2. A/B connections to flow meter")
            print("    3. GPIO 16/17/18 traces")
            log_result(label, "FAIL", f"Failed all {MAX_RETRIES} attempts")
            return False

        else:  # NO RESPONSE
            if attempt < MAX_RETRIES:
                print(f"  No response - retry {attempt + 1}/{MAX_RETRIES}...")
                time.sleep(2)
                ser.reset_input_buffer()
                continue
            else:
                print()
                print("  ╔═════════════════════════════════════════╗")
                print(f"  ║   ???    {label:>12s}   NO RESPONSE    ║")
                print("  ╚═════════════════════════════════════════╝")
                print("  Is ESP32 booted? Is firmware up to date?")
                log_result(label, "UNKNOWN", "No response after retries")
                return False

    return False


def _single_test(ser, label, attempt):
    """Run one TEST attempt. Returns 'PASS', 'FAIL', or 'NONE'."""

    ser.reset_input_buffer()

    # Keep sending TEST every 2s until we get RS485_TEST response
    if attempt == 1:
        print(f"  Attempt {attempt}: sending TEST (waiting for ESP32)...")
    else:
        print(f"  Attempt {attempt}: sending TEST...")

    start = time.time()
    total_timeout = 50 if attempt == 1 else 15  # longer first time (boot), shorter for retries
    send_interval = 2
    last_send = 0
    sensors = []
    registers = []
    passed = None

    while time.time() - start < total_timeout:
        # Send TEST periodically
        now = time.time()
        if now - last_send >= send_interval:
            ser.write(b"TEST\r\n")
            ser.flush()
            last_send = now

        try:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode('utf-8', errors='replace').strip()
        except (serial.SerialException, OSError):
            continue

        if not line:
            continue

        elapsed = time.time() - start

        # Show relevant lines
        if any(kw in line for kw in ["RS485_TEST", "SEND", "RECV", "Register",
                                      "read success", "timeout", "No response"]):
            print(f"  [{elapsed:4.1f}s] {line[:130]}")

        # Crash
        if RE_CRASH.search(line):
            return "FAIL"

        # PASS
        if RE_TEST_PASS.search(line):
            passed = "PASS"

        # Capture sensor values
        m = RE_TEST_SENSOR.search(line)
        if m:
            sensors.append((m.group(1), m.group(2)))
            if passed == "PASS":
                for uid, val in sensors:
                    print(f"    {uid} = {val}")
                for r in registers:
                    print(f"    Register: {r}")
                return "PASS"

        # Capture registers
        m = RE_REGISTER.search(line)
        if m:
            registers.append(m.group(1))

        # FAIL from firmware
        if RE_TEST_FAIL.search(line):
            return "FAIL"

        # Modbus timeout
        if RE_MODBUS_TIMEOUT.search(line):
            print(f"  Modbus timeout on attempt {attempt}")
            return "FAIL"

    return "NONE"


def batch_test(port):
    """Test multiple boards."""
    print()
    print("=" * 50)
    print("  BATCH RS485 TESTER")
    print("=" * 50)
    print()
    print("  1. Place ESP32 on new PCB + RS485 + USB")
    print("  2. Enter label, press Enter → instant result")
    print("  3. Type 'q' to quit")
    print()

    results = []
    board_num = 1
    ser = None

    while True:
        label = input(f"  Board #{board_num} (or 'q'): ").strip()
        if label.lower() == 'q':
            break
        if not label:
            label = f"PCB-{board_num:03d}"

        input(f"  '{label}' ready? Press Enter...")

        # Open/reopen serial for each board (USB may reconnect)
        if ser:
            try:
                ser.close()
            except:
                pass

        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = BAUD_RATE
            ser.timeout = 1
            ser.dtr = False  # Prevent auto-reset on connect
            ser.rts = False
            ser.open()
            time.sleep(0.3)
        except serial.SerialException as e:
            print(f"\n  Cannot open {port}: {e}")
            print("  Check USB connection.")
            continue

        passed = test_board(ser, label)
        results.append((label, passed))
        board_num += 1
        print()

    if ser:
        ser.close()

    # Summary
    total = len(results)
    pass_count = sum(1 for _, p in results if p)

    print()
    print("=" * 50)
    print("  SUMMARY")
    print("=" * 50)
    print(f"  Tested: {total}  |  Passed: {pass_count}  |  Failed: {total - pass_count}")
    if total - pass_count > 0:
        print("  Failed:")
        for lbl, p in results:
            if not p:
                print(f"    - {lbl}")
    print(f"\n  Log: {RESULTS_FILE}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PCB RS485 Tester v4.0")
    parser.add_argument("command", nargs="?", default="test", choices=["test", "batch"])
    parser.add_argument("--port", "-p", default=None)
    parser.add_argument("--label", "-l", default=None)
    args = parser.parse_args()

    port = args.port or detect_port()

    print()
    print("  ┌──────────────────────────────────────┐")
    print("  │  FluxGen RS485 PCB Tester v4.0       │")
    print("  │  Send TEST → instant Modbus read     │")
    print("  └──────────────────────────────────────┘")

    if args.command == "batch":
        batch_test(port)
    else:
        label = args.label or input("  Board label: ").strip() or f"PCB-{int(time.time()) % 10000}"
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = BAUD_RATE
            ser.timeout = 1
            ser.dtr = False
            ser.rts = False
            ser.open()
            time.sleep(0.3)
        except serial.SerialException as e:
            print(f"  Cannot open {port}: {e}")
            sys.exit(1)
        test_board(ser, label)
        ser.close()
