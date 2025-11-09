#!/usr/bin/env python3
"""
Simple Azure IoT Hub telemetry monitor (without checkpoint store)
"""

import asyncio
import json
from azure.eventhub.aio import EventHubConsumerClient

# Your IoT Hub connection string (set as environment variable or replace with your own)
import os
IOT_HUB_CONNECTION_STRING = os.getenv('IOT_HUB_CONNECTION_STRING', 'REPLACE_WITH_YOUR_IOT_HUB_CONNECTION_STRING')

async def on_event(partition_context, event):
    print(f"\n📩 Received telemetry from device:")
    print(f"Device ID: {event.system_properties.get('iothub-connection-device-id', 'Unknown')}")
    print(f"Message ID: {event.system_properties.get('message-id', 'None')}")
    print(f"Timestamp: {event.system_properties.get('iothub-enqueuedtime', 'Unknown')}")
    
    try:
        message_body = json.loads(event.body_as_str())
        print(f"Payload: {json.dumps(message_body, indent=2)}")
    except:
        print(f"Raw Payload: {event.body_as_str()}")
    
    print("-" * 50)

async def main():
    print("🚀 Starting Azure IoT Hub telemetry monitor...")
    print("Waiting for messages from device 'testing_3'...")
    print("Press Ctrl+C to stop\n")
    
    client = EventHubConsumerClient.from_connection_string(
        IOT_HUB_CONNECTION_STRING,
        consumer_group="$Default"
    )
    
    try:
        async with client:
            await client.receive(
                on_event=on_event,
                starting_position="-1"  # Start from latest
            )
    except KeyboardInterrupt:
        print("\n👋 Stopping monitor...")

if __name__ == "__main__":
    asyncio.run(main())