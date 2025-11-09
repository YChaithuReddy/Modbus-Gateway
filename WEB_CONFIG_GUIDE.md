# 🌐 Web-Configurable Multi-Sensor Modbus IoT System

## 🚀 **System Overview**

This firmware provides a **complete web-based configuration interface** for your multi-sensor Modbus IoT system. No more hardcoded configurations - everything is configurable through a user-friendly web interface!

## ✨ **Key Features**

### 🔧 **Web Configuration Interface**
- **Dual Mode Operation**: Setup mode (AP) and Operation mode (STA)
- **Multi-sensor support**: Configure up to 8 different sensors
- **Live sensor testing**: Test each sensor individually before deployment
- **Comprehensive settings**: WiFi, Azure IoT Hub, Modbus parameters

### 🔌 **Advanced Modbus Support**
- **Multiple data types**: INT16, UINT16, INT32, UINT32, FLOAT32
- **Configurable byte order**: Big Endian, Little Endian, Mixed modes
- **Flexible register types**: Holding Registers (0x03), Input Registers (0x04)
- **Individual sensor parameters**: Slave ID, baud rate, scaling factor
- **Live debugging**: Real-time hex analysis and data conversion

### ☁️ **Production-Ready Azure IoT**
- **Secure MQTT connection** with SAS token authentication
- **Multi-sensor telemetry** with individual unit IDs
- **Automatic error recovery** and connection monitoring
- **Rich telemetry data** with metadata and timestamps

## 🎯 **How It Works**

### **First Boot - Setup Mode**
1. ESP32 starts as WiFi Access Point: **`ModbusIoT-Config`** (password: `config123`)
2. Connect your phone/laptop to this AP
3. Visit **`http://192.168.4.1`** to access configuration interface
4. Configure WiFi, Azure IoT Hub, and sensors
5. Test each sensor individually 
6. Save configuration and switch to operation mode

### **Normal Operation - Production Mode**
1. ESP32 connects to your configured WiFi
2. Connects to Azure IoT Hub using your settings
3. Reads all configured sensors every 2 minutes (configurable)
4. Sends telemetry data with proper unit IDs
5. Handles errors gracefully with fallback mechanisms

## 🔧 **Configuration Interface**

### **WiFi Settings**
```
- SSID: Your WiFi network name
- Password: Your WiFi password
```

### **Azure IoT Hub Settings**
```
- IoT Hub FQDN: your-hub.azure-devices.net
- Device ID: your-device-name
- Device Key: your-device-primary-key
- Telemetry Interval: 30-3600 seconds
```

### **Sensor Configuration (Per Sensor)**
```
✅ Enabled: Enable/disable sensor
📛 Sensor Name: Human-readable name (e.g., "Flow Meter 1")
🏷️ Unit ID: Unique identifier for dashboard (e.g., "FM001")
🔧 Slave ID: Modbus slave address (1-247)
⚡ Baud Rate: 9600, 19200, 38400, 57600, 115200
📋 Register Type: Holding (0x03) or Input (0x04)
📍 Register Address: Starting register (0-65535)
📊 Quantity: Number of registers to read (1-10)
🔢 Data Type: INT16, UINT16, INT32, UINT32, FLOAT32
🔄 Byte Order: Big Endian, Little Endian, Mixed BADC, Mixed DCBA
⚖️ Scale Factor: Multiplication factor for raw value
📝 Description: Optional description
```

## 🧪 **Live Sensor Testing**

The system includes **real-time sensor testing** capabilities:

### **Individual Sensor Test**
- Click "🧪 Test This Sensor" for any configured sensor
- Shows raw hex values from Modbus response
- Displays converted values with your scaling
- Reports response time and error messages
- Helps debug byte order and scaling issues

### **All Sensors Test**
- Test all configured sensors at once
- Verify communication before deployment
- Identify problematic sensors quickly

## 📊 **Telemetry Data Format**

### **Real Sensor Data**
```json
{
  "unit_id": "FM001",
  "sensor_name": "Flow Meter 1", 
  "value": 689.1317,
  "valid": true,
  "timestamp": "2024-07-02T12:00:00Z",
  "data_source": "modbus_rs485"
}
```

### **Multi-Sensor Telemetry**
```json
{
  "message_type": "telemetry",
  "device_id": "testing_3",
  "sensors": [
    {"unit_id": "FM001", "value": 689.13, "valid": true},
    {"unit_id": "PM002", "value": 2.45, "valid": true},
    {"unit_id": "TM003", "value": 23.5, "valid": false}
  ],
  "timestamp": "2024-07-02T12:00:00Z"
}
```

## 🔌 **Hardware Connections**

### **RS485 Wiring**
```
ESP32 Pin    │ RS485 Module │ Function
─────────────┼──────────────┼──────────────
GPIO 16      │ RO (RXD)     │ Receive Data
GPIO 17      │ DI (TXD)     │ Transmit Data  
GPIO 4       │ DE/RE        │ Direction Control
3.3V         │ VCC          │ Power
GND          │ GND          │ Ground
```

### **Flow Meter Connection**
```
RS485 Module │ Flow Meter   │ Wire Color (typical)
─────────────┼──────────────┼────────────────────
A+           │ A+           │ Green or Blue
B-           │ B-           │ Yellow or White
GND          │ GND          │ Black
```

## 🚀 **Deployment Guide**

### **Step 1: Initial Setup**
```bash
# Flash the firmware
export IDF_PATH=/path/to/esp-idf
source $IDF_PATH/export.sh
idf.py -p /dev/ttyUSB0 flash

# Monitor for AP information
idf.py monitor
```

### **Step 2: Configuration**
1. Connect to **`ModbusIoT-Config`** WiFi (password: `config123`)
2. Visit **`http://192.168.4.1`**
3. Fill in your settings
4. Add your sensors with proper configurations
5. Test each sensor individually
6. Save and start operation mode

### **Step 3: Production Operation**
- Device automatically connects to your WiFi
- Starts sending telemetry to Azure IoT Hub
- Monitor via Azure IoT Explorer or your dashboard

## 🔍 **Troubleshooting**

### **WiFi AP Not Visible**
- Check if device is in setup mode (fresh install or reset)
- Look for "ModbusIoT-Config" network
- Power cycle the ESP32

### **Sensor Test Failures**
- Verify RS485 wiring (A+, B-, GND)
- Check slave ID matches your device
- Ensure proper termination resistors (120Ω)
- Try different baud rates
- Check register address in device manual

### **Wrong Sensor Values**
- Try different byte order settings
- Adjust scale factor
- Check if data type matches device specification
- Use the live testing feature to debug

### **Azure IoT Connection Issues**
- Verify device key and connection string
- Check WiFi connectivity
- Ensure device is registered in IoT Hub

## 🔧 **Advanced Configuration**

### **Adding New Data Types**
Edit `sensor_manager.c` to add support for new data formats:
```c
} else if (strcmp(data_type, "YOUR_TYPE") == 0) {
    // Add your conversion logic here
}
```

### **Custom Telemetry Frequency**
Configure in the web interface or modify:
```c
g_system_config.telemetry_interval = 60; // seconds
```

### **Multiple Device Support**
Each device can have unique:
- Device ID in Azure IoT Hub
- WiFi credentials
- Sensor configurations
- Unit ID naming schemes

## 📈 **System Monitoring**

### **Device Status**
The system reports:
- Connection status (WiFi, MQTT)
- Sensor read success/failure rates
- System uptime and performance
- Error counts and recovery actions

### **Azure IoT Hub Integration**
- Real-time telemetry data
- Device-to-cloud messaging
- Cloud-to-device commands (future feature)
- Device twin properties (future feature)

## 🔒 **Security Features**

- **TLS encryption** for all Azure communications
- **SAS token authentication** with time-based expiry
- **Certificate validation** using Azure Root CA
- **WiFi WPA2 security** for network access
- **Configuration persistence** in secure NVS storage

## 📋 **Supported Hardware**

### **Tested Flow Meters**
- Any Modbus RTU compatible device
- Registers: Holding (0x03) and Input (0x04)
- Data formats: 16-bit and 32-bit integers, IEEE 754 floats
- Baud rates: 9600-115200 bps

### **ESP32 Modules**
- ESP32-WROOM-32
- ESP32-DevKit-C
- ESP32-WROVER modules
- Any ESP32 with sufficient GPIO pins

This system provides a **complete, production-ready solution** for industrial IoT with **zero hardcoding** - everything is configurable through the web interface! 🎉