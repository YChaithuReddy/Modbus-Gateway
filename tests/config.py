"""
Shared configuration for all test components.
Modify these values to match your setup.
"""
import os

# Serial connection to ESP32
SERIAL_PORT = os.getenv("ESP32_PORT", "COM5")
SERIAL_BAUD = 115200

# ESP32 network (when connected to same WiFi/AP)
DEVICE_IP = os.getenv("ESP32_IP", "192.168.4.1")
DEVICE_HTTP_PORT = 80
WEB_USERNAME = "admin"
WEB_PASSWORD = "fluxgen123"

# Azure IoT Hub (for MQTT message verification)
IOT_HUB_CONNECTION_STRING = os.getenv("IOT_HUB_CONNECTION_STRING", "")
EXPECTED_DEVICE_ID = os.getenv("ESP32_DEVICE_ID", "testing_3")

# Modbus simulator (requires second USB-RS485 adapter)
MODBUS_SIMULATOR_PORT = os.getenv("MODBUS_SIM_PORT", "COM4")
MODBUS_BAUD = 9600

# Timing (from iot_configs.h)
TELEMETRY_INTERVAL_SEC = 300        # 5 minutes
HEARTBEAT_INTERVAL_SEC = 300        # 5 minutes
WATCHDOG_TIMEOUT_SEC = 120          # 2 minutes
TELEMETRY_TIMEOUT_SEC = 1800        # 30 minutes

# Heap thresholds (from project memory)
HEAP_NORMAL_MIN = 50000             # Web server ON
HEAP_WARNING = 30000
HEAP_CRITICAL = 20000
HEAP_EMERGENCY = 10000

# Web server limits
MAX_OPEN_SOCKETS = 7

# Reports output
REPORTS_DIR = os.path.join(os.path.dirname(__file__), "reports")

# Expected sensor unit_ids (fill in based on your device config)
EXPECTED_SENSOR_UNIT_IDS = []  # e.g., ["FM01", "WQ01", "AST001"]
