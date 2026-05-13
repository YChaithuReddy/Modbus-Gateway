#!/usr/bin/env python3
"""
MQTT Message Verifier - Subscribes to Azure IoT Hub and validates every
telemetry message from the ESP32 device.

Checks: JSON validity, schema, timestamps, ordering, duplicates, gaps.

Usage:
    python mqtt_message_verifier.py                     # Monitor and validate
    python mqtt_message_verifier.py --duration 3600     # Run for 1 hour
"""

import argparse
import asyncio
import csv
import hashlib
import json
import os
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone

import config

try:
    from azure.eventhub.aio import EventHubConsumerClient
except ImportError:
    print("ERROR: azure-eventhub not installed. Run: pip install azure-eventhub")
    sys.exit(1)


class MQTTMessageVerifier:
    """Validates MQTT telemetry messages from ESP32."""

    def __init__(self, connection_string=None, device_id=None):
        self.connection_string = connection_string or config.IOT_HUB_CONNECTION_STRING
        self.device_id = device_id or config.EXPECTED_DEVICE_ID
        self.running = False

        # Counters
        self.total_messages = 0
        self.valid_messages = 0
        self.invalid_messages = 0

        # Per unit_id tracking
        self.unit_timestamps = defaultdict(list)  # unit_id -> [timestamps]
        self.unit_message_count = defaultdict(int)
        self.unit_last_seen = {}  # unit_id -> time.time()

        # Issue tracking
        self.issues = []  # (timestamp, severity, unit_id, description)
        self.duplicate_hashes = set()
        self.duplicates = 0
        self.out_of_order = 0
        self.schema_errors = 0
        self.timestamp_errors = 0

        # Raw messages for debugging
        self.recent_messages = []  # Last 100 messages

    async def start(self, duration_sec=0):
        """Start consuming messages from IoT Hub."""
        if not self.connection_string:
            print("ERROR: Set IOT_HUB_CONNECTION_STRING environment variable")
            return

        print(f"[MQTTVerifier] Connecting to IoT Hub...")
        print(f"[MQTTVerifier] Watching for device: {self.device_id}")
        self.running = True
        start_time = time.time()

        client = EventHubConsumerClient.from_connection_string(
            self.connection_string,
            consumer_group="$Default"
        )

        async def on_event(partition_context, event):
            if not self.running:
                return

            device = event.system_properties.get(b"iothub-connection-device-id", b"").decode()
            if self.device_id and device != self.device_id:
                return

            enqueued = event.system_properties.get(b"iothub-enqueuedtime")
            body_str = event.body_as_str()
            self._validate_message(body_str, device, enqueued)

            if duration_sec > 0 and (time.time() - start_time) >= duration_sec:
                self.running = False

        try:
            async with client:
                await client.receive(
                    on_event=on_event,
                    starting_position="-1"  # Latest messages only
                )
        except asyncio.CancelledError:
            pass
        except KeyboardInterrupt:
            pass
        finally:
            self.running = False

    def _validate_message(self, body_str, device_id, enqueued_time):
        """Validate a single message."""
        self.total_messages += 1
        now = datetime.now(timezone.utc)
        errors = []

        # 1. JSON parse
        try:
            payload = json.loads(body_str)
        except json.JSONDecodeError as e:
            self.invalid_messages += 1
            self.schema_errors += 1
            self._add_issue("ERROR", "", f"Invalid JSON: {e}")
            return

        # Handle batch array
        messages = payload if isinstance(payload, list) else [payload]

        for msg in messages:
            unit_id = msg.get("unit_id", "MISSING")
            msg_errors = []

            # 2. Schema: unit_id must exist
            if "unit_id" not in msg:
                msg_errors.append("Missing unit_id field")
                self.schema_errors += 1

            # 3. Schema: created_on must exist and be valid ISO8601
            created_on = msg.get("created_on")
            if not created_on:
                msg_errors.append("Missing created_on field")
                self.timestamp_errors += 1
            else:
                try:
                    ts = datetime.fromisoformat(created_on.replace("Z", "+00:00"))

                    # Check not in future (allow 5 min clock skew)
                    if ts > now + __import__("datetime").timedelta(minutes=5):
                        msg_errors.append(f"Timestamp in future: {created_on}")
                        self.timestamp_errors += 1

                    # Check not too old (unless SD replay - allow 24h)
                    age_hours = (now - ts).total_seconds() / 3600
                    if age_hours > 24:
                        msg_errors.append(f"Timestamp too old ({age_hours:.1f}h): {created_on}")
                        self.timestamp_errors += 1

                    # Ordering check per unit_id
                    if unit_id != "MISSING":
                        prev_timestamps = self.unit_timestamps[unit_id]
                        if prev_timestamps and ts < prev_timestamps[-1]:
                            self.out_of_order += 1
                            msg_errors.append(f"Out of order: {created_on} < {prev_timestamps[-1].isoformat()}")
                        prev_timestamps.append(ts)
                        # Keep only last 100 per unit
                        if len(prev_timestamps) > 100:
                            prev_timestamps.pop(0)

                except (ValueError, TypeError) as e:
                    msg_errors.append(f"Invalid timestamp format: {created_on} ({e})")
                    self.timestamp_errors += 1

            # 4. No "type" field (Azure adds body wrapper, device should NOT include "type")
            # Actually - the device DOES include type for non-quality sensors. Skip this check.

            # 5. Quality sensor: params_data should be an object with string values
            if "params_data" in msg:
                pd = msg["params_data"]
                if not isinstance(pd, dict):
                    msg_errors.append(f"params_data is not an object: {type(pd)}")
                else:
                    for key, val in pd.items():
                        if not isinstance(val, str):
                            msg_errors.append(f"params_data[{key}] is {type(val).__name__}, expected string")

            # 6. Value sanity (basic)
            for key in ["consumption", "level_filled", "value"]:
                if key in msg:
                    try:
                        val = float(msg[key])
                        if val != val:  # NaN check
                            msg_errors.append(f"{key} is NaN")
                    except (ValueError, TypeError):
                        pass  # String values are OK

            # 7. Duplicate check
            msg_hash = hashlib.md5(json.dumps(msg, sort_keys=True).encode()).hexdigest()
            if msg_hash in self.duplicate_hashes:
                self.duplicates += 1
                msg_errors.append("Duplicate message")
            self.duplicate_hashes.add(msg_hash)
            # Keep set bounded
            if len(self.duplicate_hashes) > 10000:
                self.duplicate_hashes = set(list(self.duplicate_hashes)[-5000:])

            # Track per unit_id
            if unit_id != "MISSING":
                self.unit_message_count[unit_id] += 1
                self.unit_last_seen[unit_id] = time.time()

            # Record result
            if msg_errors:
                self.invalid_messages += 1
                for err in msg_errors:
                    self._add_issue("WARNING", unit_id, err)
            else:
                self.valid_messages += 1

            # Print live
            status = "OK" if not msg_errors else "ISSUES"
            print(f"  [{self.total_messages:4d}] {unit_id:12s} {status:6s} "
                  f"{created_on or 'no-timestamp':25s} "
                  f"{'  '.join(msg_errors[:2]) if msg_errors else ''}")

        # Store recent
        self.recent_messages.append({
            "time": now.isoformat(),
            "device": device_id,
            "payload": payload,
        })
        if len(self.recent_messages) > 100:
            self.recent_messages.pop(0)

    def _add_issue(self, severity, unit_id, description):
        """Track an issue."""
        self.issues.append((
            datetime.now().isoformat(), severity, unit_id, description
        ))

    def get_missing_sensors(self, timeout_sec=None):
        """Check which expected sensors haven't reported recently."""
        if timeout_sec is None:
            timeout_sec = config.TELEMETRY_INTERVAL_SEC * 2
        now = time.time()
        missing = []
        for uid in config.EXPECTED_SENSOR_UNIT_IDS:
            last = self.unit_last_seen.get(uid)
            if last is None:
                missing.append((uid, "never seen"))
            elif (now - last) > timeout_sec:
                age = int(now - last)
                missing.append((uid, f"last seen {age}s ago"))
        return missing

    def get_summary(self):
        """Get validation summary."""
        return {
            "total_messages": self.total_messages,
            "valid": self.valid_messages,
            "invalid": self.invalid_messages,
            "duplicates": self.duplicates,
            "out_of_order": self.out_of_order,
            "schema_errors": self.schema_errors,
            "timestamp_errors": self.timestamp_errors,
            "unique_unit_ids": list(self.unit_message_count.keys()),
            "per_unit_count": dict(self.unit_message_count),
            "total_issues": len(self.issues),
        }

    def save_report(self, filename=None):
        """Save validation results to CSV."""
        if not filename:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = os.path.join(config.REPORTS_DIR, f"mqtt_verify_{ts}.csv")
        os.makedirs(os.path.dirname(filename), exist_ok=True)

        with open(filename, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["timestamp", "severity", "unit_id", "description"])
            writer.writerows(self.issues)

        print(f"\n[MQTTVerifier] Report saved to {filename}")
        summary = self.get_summary()
        print(f"  Total: {summary['total_messages']}, "
              f"Valid: {summary['valid']}, Invalid: {summary['invalid']}")
        print(f"  Duplicates: {summary['duplicates']}, "
              f"Out-of-order: {summary['out_of_order']}")
        print(f"  Unit IDs seen: {summary['unique_unit_ids']}")
        print(f"  Per unit: {summary['per_unit_count']}")
        return filename


async def main_async(args):
    verifier = MQTTMessageVerifier()
    print(f"\n{'='*60}")
    print(f"  MQTT Message Verifier")
    print(f"  Device: {verifier.device_id}")
    print(f"  Duration: {'forever' if args.duration == 0 else f'{args.duration}s'}")
    print(f"{'='*60}\n")

    try:
        await verifier.start(duration_sec=args.duration)
    except KeyboardInterrupt:
        pass
    finally:
        verifier.save_report()


def main():
    parser = argparse.ArgumentParser(description="MQTT Message Verifier")
    parser.add_argument("--duration", type=int, default=0, help="Run for N seconds (0=forever)")
    parser.add_argument("--device-id", default=None)
    args = parser.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
