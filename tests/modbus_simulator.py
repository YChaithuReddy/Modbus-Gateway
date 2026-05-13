#!/usr/bin/env python3
"""
Modbus RTU Slave Simulator - Acts as virtual sensors on RS485 bus so the ESP32
can read test data without physical sensors.

Requires: USB-to-RS485 adapter on a separate COM port (e.g., COM4)
connected to the same RS485 bus as the ESP32.

Usage:
    python modbus_simulator.py                           # Default sensors
    python modbus_simulator.py --port COM4 --baud 9600
    python modbus_simulator.py --scenario edge_values
"""

import argparse
import math
import struct
import sys
import threading
import time

import config

try:
    from pymodbus.server import StartSerialServer
    from pymodbus.datastore import (
        ModbusSequentialDataBlock,
        ModbusServerContext,
    )
    from pymodbus.datastore import ModbusDeviceContext as ModbusSlaveContext
    from pymodbus import ModbusDeviceIdentification
except ImportError:
    print("ERROR: pymodbus not installed. Run: pip install pymodbus[serial]")
    sys.exit(1)


def float32_to_registers(value, byte_order="CDAB"):
    """Convert float to two 16-bit Modbus registers.

    CDAB = word-swap (Aquadax/Opruss): reg[0]=low_word, reg[1]=high_word
    ABCD = big-endian: reg[0]=high_word, reg[1]=low_word
    DCBA = byte-swap + word-swap (Opruss TDS): each byte swapped then word-swapped
    """
    packed = struct.pack(">f", value)  # Big-endian float
    high_word = struct.unpack(">H", packed[0:2])[0]
    low_word = struct.unpack(">H", packed[2:4])[0]

    if byte_order == "ABCD":
        return [high_word, low_word]
    elif byte_order == "CDAB":
        return [low_word, high_word]
    elif byte_order == "DCBA":
        # Byte-swap each word, then word-swap
        hi_swapped = ((high_word & 0xFF) << 8) | ((high_word >> 8) & 0xFF)
        lo_swapped = ((low_word & 0xFF) << 8) | ((low_word >> 8) & 0xFF)
        return [lo_swapped, hi_swapped]
    else:
        return [high_word, low_word]


def uint16_value(value):
    """Single UINT16 register."""
    return [int(value) & 0xFFFF]


class SensorProfile:
    """Defines a virtual sensor's register layout."""

    def __init__(self, slave_id, name, registers, update_fn=None):
        self.slave_id = slave_id
        self.name = name
        self.registers = registers  # dict of addr -> [reg_values]
        self.update_fn = update_fn  # Called periodically to update values
        self.mode = "normal"  # normal, timeout, error, edge


# === Pre-built sensor profiles matching real hardware ===

def create_flow_meter(slave_id=1, initial_totaliser=1000.0):
    """Generic flow meter: UINT32 totaliser at register 0x1010."""
    totaliser = [initial_totaliser]

    def update(profile, elapsed):
        if profile.mode == "normal":
            totaliser[0] += 0.5  # Simulate flow
        # INT32+FLOAT32 format matching commented-out Post_Data_on_UART
        int_part = int(totaliser[0])
        dec_part = totaliser[0] - int_part
        regs = {}
        # Registers 0x08-0x09: integer part as UINT32
        regs[0x08] = [(int_part >> 16) & 0xFFFF, int_part & 0xFFFF]
        # Registers 0x0A-0x0B: decimal part as FLOAT32
        dec_regs = float32_to_registers(dec_part, "ABCD")
        regs[0x0A] = dec_regs
        return regs

    return SensorProfile(slave_id, f"FlowMeter_{slave_id}",
                         {0x08: [0, 0], 0x0A: [0, 0]}, update)


def create_aquadax_quality(slave_id=1, base_addr=1280):
    """Aquadax quality sensor: 5x FLOAT32 CDAB with byte-swap at addr 1280."""
    values = {"COD": 45.2, "BOD": 15.8, "TSS": 32.5, "pH": 7.14, "Temp": 25.6}

    def update(profile, elapsed):
        if profile.mode == "normal":
            # Slight variations
            import random
            v = {k: v + random.uniform(-0.5, 0.5) for k, v in values.items()}
        elif profile.mode == "edge":
            v = {"COD": 0.0, "BOD": 999.99, "TSS": -1.0, "pH": 14.0, "Temp": 0.0}
        else:
            v = values

        regs = {}
        offsets = [0, 2, 4, 6, 10]  # COD, BOD, TSS, pH, Temp (skip 8-9)
        for i, (name, offset) in enumerate(zip(v.keys(), offsets)):
            # Aquadax uses byte-swap + word-swap
            packed = struct.pack(">f", v[name])
            hi = struct.unpack(">H", packed[0:2])[0]
            lo = struct.unpack(">H", packed[2:4])[0]
            # Byte-swap each register, then word-swap
            hi_s = ((hi & 0xFF) << 8) | ((hi >> 8) & 0xFF)
            lo_s = ((lo & 0xFF) << 8) | ((lo >> 8) & 0xFF)
            regs[base_addr + offset] = [lo_s, hi_s]  # word-swap
        # Reserved registers 8-9
        regs[base_addr + 8] = [0, 0]
        return regs

    return SensorProfile(slave_id, f"Aquadax_{slave_id}",
                         {base_addr + i: [0] for i in range(12)}, update)


def create_opruss_ace(slave_id=1, base_addr=0):
    """Opruss Ace: COD(0-1), BOD(6-7), TSS(20-21) as FLOAT32 CDAB."""
    values = {"COD": 52.3, "BOD": 18.7, "TSS": 28.1}

    def update(profile, elapsed):
        if profile.mode == "normal":
            import random
            v = {k: val + random.uniform(-1.0, 1.0) for k, val in values.items()}
        else:
            v = values
        regs = {}
        offsets = {"COD": 0, "BOD": 6, "TSS": 20}
        for name, off in offsets.items():
            r = float32_to_registers(v[name], "CDAB")
            regs[base_addr + off] = r
        return regs

    return SensorProfile(slave_id, f"OprussAce_{slave_id}",
                         {base_addr + i: [0] for i in range(22)}, update)


def create_aster(slave_id_ph=1, slave_id_tds=2):
    """Aster: pH at reg 6 offset 2 (UINT16×0.01), TDS at reg 0 offset 1 (UINT16)."""
    ph_value = [720]   # 7.20 pH (raw UINT16, ×0.01 = 7.20)
    tds_value = [350]  # 350 ppm

    def update_ph(profile, elapsed):
        if profile.mode == "normal":
            import random
            ph_value[0] = max(0, min(1400, ph_value[0] + random.randint(-5, 5)))
        regs = {6: [0, 0, 0, 0]}
        regs[6][2] = ph_value[0]  # pH at offset 2
        return {6: regs[6]}

    def update_tds(profile, elapsed):
        if profile.mode == "normal":
            import random
            tds_value[0] = max(0, min(9999, tds_value[0] + random.randint(-10, 10)))
        regs = {0: [0, 0, 0, 0]}
        regs[0][1] = tds_value[0]  # TDS at offset 1
        return {0: regs[0]}

    ph_sensor = SensorProfile(slave_id_ph, f"Aster_pH_{slave_id_ph}",
                              {6: [0, 0, 0, 0]}, update_ph)
    tds_sensor = SensorProfile(slave_id_tds, f"Aster_TDS_{slave_id_tds}",
                               {0: [0, 0, 0, 0]}, update_tds)
    return [ph_sensor, tds_sensor]


class ModbusSimulator:
    """Manages multiple virtual Modbus slaves."""

    def __init__(self, port=None, baud=None):
        self.port = port or config.MODBUS_SIMULATOR_PORT
        self.baud = baud or config.MODBUS_BAUD
        self.profiles = {}  # slave_id -> SensorProfile
        self.server_thread = None
        self.running = False
        self._update_thread = None

    def add_profile(self, profile):
        """Add a sensor profile."""
        if isinstance(profile, list):
            for p in profile:
                self.profiles[p.slave_id] = p
        else:
            self.profiles[profile.slave_id] = profile
        print(f"[ModbusSim] Added: {profile.name if not isinstance(profile, list) else [p.name for p in profile]}")

    def set_mode(self, slave_id, mode):
        """Set sensor mode: normal, timeout, edge, error."""
        if slave_id in self.profiles:
            self.profiles[slave_id].mode = mode
            print(f"[ModbusSim] Slave {slave_id} mode: {mode}")

    def start(self):
        """Start the Modbus RTU slave server."""
        # Build data stores for each slave
        slaves = {}
        for sid, profile in self.profiles.items():
            # Create a holding register block (function code 3)
            # and input register block (function code 4)
            # Covering addresses 0-2000
            hr_block = ModbusSequentialDataBlock(0, [0] * 2000)
            ir_block = ModbusSequentialDataBlock(0, [0] * 2000)
            slaves[sid] = ModbusSlaveContext(
                di=ModbusSequentialDataBlock(0, [0] * 100),
                co=ModbusSequentialDataBlock(0, [0] * 100),
                hr=hr_block,
                ir=ir_block,
            )

        context = ModbusServerContext(slaves=slaves, single=False)
        self._context = context

        # Start update thread
        self.running = True
        self._update_thread = threading.Thread(target=self._update_loop, daemon=True)
        self._update_thread.start()

        # Start server in background thread
        identity = ModbusDeviceIdentification()
        identity.VendorName = "ESP32TestHarness"
        identity.ProductName = "VirtualSensors"

        print(f"[ModbusSim] Starting RTU server on {self.port} @ {self.baud} baud")
        print(f"[ModbusSim] Slaves: {list(self.profiles.keys())}")

        self.server_thread = threading.Thread(
            target=self._run_server,
            args=(context, identity),
            daemon=True
        )
        self.server_thread.start()

    def _run_server(self, context, identity):
        """Run the Modbus serial server."""
        try:
            StartSerialServer(
                context=context,
                identity=identity,
                port=self.port,
                baudrate=self.baud,
                parity="N",
                stopbits=1,
                bytesize=8,
                timeout=1,
            )
        except Exception as e:
            print(f"[ModbusSim] Server error: {e}")
            self.running = False

    def _update_loop(self):
        """Periodically update register values."""
        start = time.time()
        while self.running:
            elapsed = time.time() - start
            for sid, profile in self.profiles.items():
                if profile.mode == "timeout":
                    continue  # Don't update = slave won't respond

                if profile.update_fn:
                    try:
                        new_regs = profile.update_fn(profile, elapsed)
                        if new_regs and sid in self._context:
                            store = self._context[sid]
                            for addr, values in new_regs.items():
                                for i, val in enumerate(values):
                                    # Write to both holding (3) and input (4) registers
                                    store.setValues(3, addr + i, [val])
                                    store.setValues(4, addr + i, [val])
                    except Exception as e:
                        print(f"[ModbusSim] Update error slave {sid}: {e}")

            time.sleep(1)  # Update every second

    def stop(self):
        """Stop the simulator."""
        self.running = False
        print("[ModbusSim] Stopped")


def main():
    parser = argparse.ArgumentParser(description="Modbus RTU Slave Simulator")
    parser.add_argument("--port", default=config.MODBUS_SIMULATOR_PORT)
    parser.add_argument("--baud", type=int, default=config.MODBUS_BAUD)
    parser.add_argument("--scenario", default="default",
                        choices=["default", "edge_values", "multi_sensor", "aster_only"])
    args = parser.parse_args()

    sim = ModbusSimulator(port=args.port, baud=args.baud)

    if args.scenario == "default":
        sim.add_profile(create_aquadax_quality(slave_id=1))
    elif args.scenario == "edge_values":
        aq = create_aquadax_quality(slave_id=1)
        aq.mode = "edge"
        sim.add_profile(aq)
    elif args.scenario == "multi_sensor":
        sim.add_profile(create_aquadax_quality(slave_id=1))
        sim.add_profile(create_opruss_ace(slave_id=3))
        sim.add_profile(create_aster(slave_id_ph=5, slave_id_tds=6))
    elif args.scenario == "aster_only":
        sim.add_profile(create_aster(slave_id_ph=1, slave_id_tds=2))

    sim.start()
    print("\nModbus simulator running. Press Ctrl+C to stop.")
    print("Commands: type 'timeout <slave_id>' or 'normal <slave_id>' to change modes\n")

    try:
        while sim.running:
            try:
                cmd = input("> ").strip().lower()
                if not cmd:
                    continue
                parts = cmd.split()
                if parts[0] in ("timeout", "normal", "edge", "error") and len(parts) >= 2:
                    sim.set_mode(int(parts[1]), parts[0])
                elif parts[0] == "status":
                    for sid, p in sim.profiles.items():
                        print(f"  Slave {sid}: {p.name} (mode: {p.mode})")
                elif parts[0] in ("quit", "exit"):
                    break
                else:
                    print("Commands: timeout <id>, normal <id>, edge <id>, status, quit")
            except EOFError:
                time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        sim.stop()


if __name__ == "__main__":
    main()
