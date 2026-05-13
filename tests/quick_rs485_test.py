"""
Quick RS485 Board Tester
========================
Tests RS485 Modbus communication on new PCB boards.
Connect to ModbusIoT-Config WiFi first, then run this script.

Usage: python quick_rs485_test.py
"""

import urllib.request
import json
import sys
import time

GATEWAY_IP = "192.168.4.1"
SLAVE_ID = 1
FUNCTION_CODE = 3        # Read holding registers
START_REGISTER = 0
REGISTER_COUNT = 2
BAUD_RATE = 9600

def test_rs485():
    url = (f"http://{GATEWAY_IP}/test_rs485?"
           f"slave_id={SLAVE_ID}&function_code={FUNCTION_CODE}"
           f"&start_register={START_REGISTER}&register_count={REGISTER_COUNT}"
           f"&baud_rate={BAUD_RATE}")

    print("=" * 50)
    print("  RS485 BOARD TESTER - Dalian Flow Meter")
    print("=" * 50)
    print(f"  Target: Slave {SLAVE_ID}, Registers {START_REGISTER}-{START_REGISTER + REGISTER_COUNT - 1}")
    print(f"  Baud: {BAUD_RATE}")
    print()

    try:
        print("Sending Modbus request...")
        req = urllib.request.Request(url, method='GET')
        req.add_header('Connection', 'close')
        response = urllib.request.urlopen(req, timeout=10)
        data = response.read().decode('utf-8')

        try:
            result = json.loads(data)
        except json.JSONDecodeError:
            # Response might be HTML, check for success/fail keywords
            if "success" in data.lower() or "register" in data.lower():
                print("\n  [PASS] RS485 WORKING - Got response from flow meter")
                print(f"  Response: {data[:200]}")
                return True
            elif "timeout" in data.lower() or "error" in data.lower() or "fail" in data.lower():
                print("\n  [FAIL] RS485 ERROR - No response from flow meter")
                print(f"  Response: {data[:200]}")
                return False
            else:
                print(f"\n  [?] Unexpected response: {data[:200]}")
                return False

        # JSON response
        if result.get("success") or result.get("status") == "ok":
            print("\n  =============================================")
            print("  |     [PASS]  RS485 WORKING!               |")
            print("  =============================================")
            if "registers" in result:
                for i, val in enumerate(result["registers"]):
                    print(f"  Register[{i}]: {val}")
            elif "value" in result:
                print(f"  Value: {result['value']}")
            if "raw" in result:
                print(f"  Raw: {result['raw']}")
            print()
            return True
        else:
            print("\n  =============================================")
            print("  |     [FAIL]  RS485 NOT WORKING!           |")
            print("  =============================================")
            if "error" in result:
                print(f"  Error: {result['error']}")
            if "message" in result:
                print(f"  Message: {result['message']}")
            print()
            return False

    except urllib.error.URLError as e:
        print(f"\n  [FAIL] Cannot connect to gateway at {GATEWAY_IP}")
        print(f"  Error: {e}")
        print(f"  -> Make sure you're connected to 'ModbusIoT-Config' WiFi")
        return False
    except Exception as e:
        print(f"\n  [FAIL] Unexpected error: {e}")
        return False


if __name__ == "__main__":
    board_num = input("Enter board number/label (or press Enter to skip): ").strip()

    passed = test_rs485()

    # Log result
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    status = "PASS" if passed else "FAIL"
    label = board_num if board_num else "unknown"

    with open("rs485_test_log.csv", "a") as f:
        # Create header if file is new
        import os
        if os.path.getsize("rs485_test_log.csv") == 0 if os.path.exists("rs485_test_log.csv") else True:
            f.write("timestamp,board_label,status\n")
        f.write(f"{timestamp},{label},{status}\n")

    print(f"Result logged to rs485_test_log.csv")
    print()

    if not passed:
        print("Troubleshooting:")
        print("  1. Check RS485 A/B wiring (TX=GPIO17, RX=GPIO16, RTS=GPIO18)")
        print("  2. Check flow meter power and slave ID = 1")
        print("  3. Check baud rate matches (default 9600)")
        print("  4. Check RS485 termination resistor")
        sys.exit(1)
