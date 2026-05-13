#!/usr/bin/env python3
"""
Network Fault Injector - Simulates network disruptions for the ESP32 by
blocking MQTT port 8883 via Windows Firewall or disabling network interfaces.

REQUIRES: Run as Administrator (elevated PowerShell/CMD)

Usage:
    python network_fault_injector.py block 600       # Block for 10 minutes
    python network_fault_injector.py flap 120 30     # 2min on, 30sec off, repeat
    python network_fault_injector.py unblock          # Remove all blocks
"""

import argparse
import ctypes
import subprocess
import sys
import time

FIREWALL_RULE_NAME = "ESP32_TEST_BLOCK_MQTT"


def is_admin():
    """Check if running with Administrator privileges."""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def run_cmd(cmd, check=True):
    """Run a shell command and return output."""
    result = subprocess.run(
        cmd, shell=True, capture_output=True, text=True, timeout=15
    )
    if check and result.returncode != 0:
        print(f"  Command failed: {cmd}")
        print(f"  stderr: {result.stderr.strip()}")
    return result


def block_mqtt():
    """Block outbound MQTT traffic (port 8883) via Windows Firewall."""
    # Remove existing rule first (idempotent)
    run_cmd(
        f'netsh advfirewall firewall delete rule name="{FIREWALL_RULE_NAME}"',
        check=False
    )
    result = run_cmd(
        f'netsh advfirewall firewall add rule name="{FIREWALL_RULE_NAME}" '
        f'dir=out action=block protocol=TCP remoteport=8883'
    )
    if result.returncode == 0:
        print("[NetworkFault] BLOCKED: Outbound port 8883 (MQTT/TLS)")
        return True
    return False


def unblock_mqtt():
    """Remove the MQTT block firewall rule."""
    result = run_cmd(
        f'netsh advfirewall firewall delete rule name="{FIREWALL_RULE_NAME}"',
        check=False
    )
    print("[NetworkFault] UNBLOCKED: Firewall rule removed")
    return True


def block_all_traffic(interface_name="Wi-Fi"):
    """Disable a network interface entirely."""
    result = run_cmd(f'netsh interface set interface "{interface_name}" disable')
    if result.returncode == 0:
        print(f"[NetworkFault] DISABLED interface: {interface_name}")
        return True
    return False


def unblock_all_traffic(interface_name="Wi-Fi"):
    """Re-enable a network interface."""
    result = run_cmd(f'netsh interface set interface "{interface_name}" enable')
    if result.returncode == 0:
        print(f"[NetworkFault] ENABLED interface: {interface_name}")
        return True
    return False


def is_device_reachable(ip, timeout=3):
    """Ping the ESP32 to check reachability."""
    result = run_cmd(f"ping -n 1 -w {timeout*1000} {ip}", check=False)
    return result.returncode == 0


def block_for_duration(seconds, method="firewall"):
    """Block network for a specified duration then unblock.

    Args:
        seconds: How long to block
        method: "firewall" (block MQTT only) or "interface" (block everything)
    """
    print(f"[NetworkFault] Blocking for {seconds} seconds (method: {method})")

    if method == "firewall":
        block_mqtt()
    else:
        block_all_traffic()

    try:
        for remaining in range(seconds, 0, -1):
            print(f"\r  Block active: {remaining}s remaining...", end="", flush=True)
            time.sleep(1)
        print()
    except KeyboardInterrupt:
        print("\n  Interrupted!")
    finally:
        if method == "firewall":
            unblock_mqtt()
        else:
            unblock_all_traffic()
        print("[NetworkFault] Network restored")


def flap_network(on_seconds, off_seconds, cycles=5, method="firewall"):
    """Repeatedly block/unblock network to simulate flapping.

    Args:
        on_seconds: Duration of normal connectivity
        off_seconds: Duration of each outage
        cycles: Number of flap cycles
        method: "firewall" or "interface"
    """
    print(f"[NetworkFault] Flapping: {on_seconds}s on / {off_seconds}s off x {cycles} cycles")
    block_fn = block_mqtt if method == "firewall" else block_all_traffic
    unblock_fn = unblock_mqtt if method == "firewall" else unblock_all_traffic

    try:
        for cycle in range(1, cycles + 1):
            print(f"\n  --- Cycle {cycle}/{cycles} ---")

            # Network ON
            unblock_fn()
            print(f"  Network ON for {on_seconds}s")
            time.sleep(on_seconds)

            # Network OFF
            block_fn()
            print(f"  Network OFF for {off_seconds}s")
            time.sleep(off_seconds)

        unblock_fn()
        print(f"\n[NetworkFault] Flapping complete. Network restored.")
    except KeyboardInterrupt:
        unblock_fn()
        print(f"\n[NetworkFault] Interrupted. Network restored.")


class NetworkFaultInjector:
    """API for orchestrator to control network faults."""

    def __init__(self, method="firewall"):
        self.method = method
        self.is_blocked = False

    def block(self, duration_sec=None):
        """Block network. If duration specified, auto-unblock after."""
        if self.method == "firewall":
            block_mqtt()
        else:
            block_all_traffic()
        self.is_blocked = True

        if duration_sec:
            def _auto_unblock():
                time.sleep(duration_sec)
                self.unblock()
            import threading
            t = threading.Thread(target=_auto_unblock, daemon=True)
            t.start()

    def unblock(self):
        """Unblock network."""
        if self.method == "firewall":
            unblock_mqtt()
        else:
            unblock_all_traffic()
        self.is_blocked = False

    def cleanup(self):
        """Ensure everything is unblocked on exit."""
        unblock_mqtt()
        print("[NetworkFault] Cleanup done")


def main():
    if not is_admin():
        print("ERROR: This script must be run as Administrator!")
        print("Right-click your terminal and select 'Run as administrator'")
        sys.exit(1)

    parser = argparse.ArgumentParser(description="Network Fault Injector")
    sub = parser.add_subparsers(dest="action")

    p_block = sub.add_parser("block", help="Block network for N seconds")
    p_block.add_argument("seconds", type=int)
    p_block.add_argument("--method", default="firewall", choices=["firewall", "interface"])

    p_flap = sub.add_parser("flap", help="Flap network on/off")
    p_flap.add_argument("on_seconds", type=int)
    p_flap.add_argument("off_seconds", type=int)
    p_flap.add_argument("--cycles", type=int, default=5)
    p_flap.add_argument("--method", default="firewall", choices=["firewall", "interface"])

    sub.add_parser("unblock", help="Remove all network blocks")

    p_ping = sub.add_parser("ping", help="Check if ESP32 is reachable")
    p_ping.add_argument("--ip", default="192.168.4.1")

    args = parser.parse_args()

    if args.action == "block":
        block_for_duration(args.seconds, args.method)
    elif args.action == "flap":
        flap_network(args.on_seconds, args.off_seconds, args.cycles, args.method)
    elif args.action == "unblock":
        unblock_mqtt()
        unblock_all_traffic()
    elif args.action == "ping":
        reachable = is_device_reachable(args.ip)
        print(f"ESP32 at {args.ip}: {'REACHABLE' if reachable else 'UNREACHABLE'}")
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
