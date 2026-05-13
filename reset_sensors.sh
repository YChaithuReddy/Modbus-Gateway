#!/bin/bash
# Sensor Configuration Reset Script for ESP32 Modbus IoT Gateway
# No CORS issues - runs directly from command line

echo "=========================================="
echo "ESP32 Sensor Configuration Reset"
echo "=========================================="
echo

DEVICE_IP="192.168.4.1"

echo "Checking device connection..."
if ! ping -c 1 -W 2 $DEVICE_IP >/dev/null 2>&1; then
    echo "[ERROR] Cannot reach device at $DEVICE_IP"
    echo
    echo "Please check:"
    echo "  - Device is powered on"
    echo "  - Web server is running"
    echo "  - You are connected to device WiFi: ModbusIoT-Config"
    echo
    exit 1
fi

echo "[OK] Device is reachable"
echo

echo "=========================================="
echo "Step 1: Resetting Sensor Configurations"
echo "=========================================="
echo
echo "Sending reset command..."
curl -X POST "http://$DEVICE_IP/reset_sensors" \
     -H "Content-Type: application/json" \
     -w "\n"
echo

echo "=========================================="
echo "Step 2: Rebooting Device"
echo "=========================================="
echo
read -p "Do you want to reboot the device now? (y/n): " -n 1 -r
echo

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo
    echo "Sending reboot command..."
    curl -X POST "http://$DEVICE_IP/reboot" \
         -H "Content-Type: application/json" \
         -w "\n"
    echo
    echo "[INFO] Device is rebooting..."
    echo "[INFO] Please wait 30 seconds before reconnecting"
    echo
else
    echo
    echo "[INFO] Reboot skipped. Please reboot manually to apply changes."
    echo "[INFO] You can run this command to reboot later:"
    echo "       curl -X POST http://$DEVICE_IP/reboot"
    echo
fi
