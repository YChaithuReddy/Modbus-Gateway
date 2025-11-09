# CLAUDE.md - ESP32 Modbus IoT Gateway Project Context

## Project Overview
**ESP32 Modbus IoT Gateway v1.0.0** - A production-ready industrial IoT gateway for RS485 Modbus communication with Azure IoT Hub integration.

## Project Type
- **ESP-IDF Project** (ESP-IDF v5.4.1)
- **Industrial IoT Gateway** with web configuration interface
- **Production-ready** system for manufacturing/process control

## Core Architecture

### Hardware Platform
- **ESP32-WROOM-32** with 4MB flash memory
- **RS485 Communication**: GPIO 16 (RX2), GPIO 17 (TX2), GPIO 4 (RTS)
- **Configuration Trigger**: GPIO 34 (pull-low for config mode)
- **Industrial temperature range**: -40°C to +85°C

### Software Architecture
- **Dual-core ESP32 utilization**:
  - Core 0: Modbus sensor reading tasks
  - Core 1: MQTT communication and telemetry tasks
- **Web-based configuration** with responsive interface
- **Multi-sensor support** (up to 8 sensors)
- **16 data format interpretations** (ScadaCore compatible)

## Key Components

### 1. Main Application (`main/main.c`)
- **Primary entry point** with dual-core task management
- **Azure IoT Hub** MQTT communication with SAS token authentication
- **Real-time telemetry** transmission with configurable intervals
- **GPIO interrupt handling** for configuration mode switching
- **Graceful task shutdown** and error recovery

### 2. Modbus Communication (`main/modbus.c` + `modbus.h`)
- **RS485 Modbus RTU** implementation
- **Professional error handling** with CRC validation
- **Multiple data formats**: UINT16/32, INT16/32, FLOAT32
- **Byte order support**: ABCD, DCBA, BADC, CDAB
- **Statistics tracking** and diagnostics

### 3. Web Configuration (`main/web_config_backup.c` + `web_config.h`)
- **Responsive web interface** for industrial use
- **Individual sensor management** (Add/Edit/Delete/Test)
- **Real-time sensor testing** with format display
- **WiFi and Azure configuration** management
- **Professional troubleshooting guides**

### 4. Sensor Management (`main/sensor_manager.c` + `sensor_manager.h`)
- **Multi-sensor reading** coordination
- **Data format conversion** and scaling
- **Test result formatting** and validation
- **Comprehensive error reporting**

### 5. JSON Templates (`main/json_templates.c` + `json_templates.h`)
- **Azure IoT telemetry** payload generation
- **Sensor-specific JSON** formatting
- **Energy meter support** with hex data
- **Template-based data structures**

## Configuration Files

### Build Configuration
- **`CMakeLists.txt`**: Main project configuration (ESP-IDF)
- **`main/CMakeLists.txt`**: Component-specific build settings
- **`sdkconfig`**: ESP-IDF configuration parameters
- **`partitions_4mb.csv`**: Flash memory partitioning

### IoT Configuration
- **`main/iot_configs.h`**: WiFi and Azure IoT Hub credentials
  - WiFi SSID: "Test" / Password: "config123"
  - Azure IoT Hub: "fluxgen-testhub.azure-devices.net"
  - Device ID: "testing_3"
  - Telemetry interval: 120 seconds

### Security
- **`main/azure_ca_cert.pem`**: Azure IoT Hub root certificate
- **WPA2-PSK WiFi** encryption
- **SAS token authentication** for Azure IoT Hub

## Data Formats Supported

### 16-bit Formats (1 register)
- UINT16_BE/LE, INT16_BE/LE

### 32-bit Integer Formats (2 registers)
- UINT32_ABCD (Big Endian)
- UINT32_DCBA (Little Endian)
- UINT32_BADC (Mid-Big Endian)
- UINT32_CDAB (Mid-Little Endian)
- INT32_* (signed variants)

### 32-bit Float Formats (2 registers)
- FLOAT32_ABCD/DCBA/BADC/CDAB (IEEE 754)

## Operating Modes

### 1. Setup Mode (CONFIG_STATE_SETUP)
- **Web configuration interface** active
- **WiFi AP**: "ModbusIoT-Config" (password: "config123")
- **Web portal**: http://192.168.4.1
- **Sensor configuration** and testing

### 2. Operation Mode (CONFIG_STATE_OPERATION)
- **Production telemetry** transmission
- **Real-time Modbus** sensor reading
- **Azure IoT Hub** connectivity
- **Background monitoring** and error recovery

## Build Commands

### Standard Build Process
```bash
idf.py build                    # Build project
idf.py -p /dev/ttyUSB0 flash   # Flash firmware
idf.py monitor                 # Monitor serial output
idf.py flash monitor           # Flash and monitor
```

### Development Commands
```bash
idf.py menuconfig              # Configure project
idf.py clean                   # Clean build
idf.py size                    # Check binary sizes
idf.py erase-flash            # Erase entire flash
```

## Production Features

### Industrial Reliability
- **Watchdog timers** and automatic recovery
- **Dual-core task isolation** for performance
- **Comprehensive error handling** with specific diagnostics
- **Persistent configuration** in NVS flash storage

### Professional Interface
- **Industrial-grade web UI** with company branding
- **Real-time diagnostics** and troubleshooting
- **Professional error messages** with solutions
- **Field-ready operation** with minimal maintenance

### Connectivity & Security
- **Secure MQTT** with Azure IoT Hub integration
- **WiFi management** with network scanning
- **Input validation** and sanitization
- **Certificate-based authentication**

## Monitoring & Diagnostics

### Serial Monitor Output
- **Detailed telemetry logs** with emoji indicators
- **Modbus communication** status and statistics
- **MQTT connection** status and error details
- **System performance** metrics (heap, tasks)

### Web Interface Diagnostics
- **Real-time sensor testing** with format display
- **Connectivity troubleshooting** guides
- **Professional error reporting** with solutions
- **System status** monitoring

## Documentation Structure
```
Project Root/
├── README.md                 # Main project documentation
├── PRODUCTION_GUIDE.md       # Deployment and setup guide
├── VERSION.md               # Version history and releases
├── CHANGELOG.md             # Detailed change log
├── IMPLEMENTATION_NOTES.md  # Technical implementation details
├── SYSTEM_DOCUMENTATION.md  # System architecture
├── WEB_CONFIG_GUIDE.md      # Web interface guide
└── CLAUDE.md               # This context file
```

## Development Notes

### Memory Management
- **Dynamic allocation** for sensor data to prevent stack overflow
- **Static buffers** for MQTT and telemetry payloads
- **Queue-based communication** between tasks
- **Heap monitoring** and leak detection

### Task Architecture
- **modbus_task** (Core 0, Priority 5): Sensor reading
- **mqtt_task** (Core 1, Priority 4): MQTT connectivity
- **telemetry_task** (Core 1, Priority 3): Data transmission
- **Web server tasks** (automatic core assignment)

### Error Recovery
- **MQTT reconnection** with attempt limits
- **Modbus reinitialization** on persistent failures
- **System restart** on critical errors (configurable)
- **Graceful task shutdown** for mode switching

## Integration Points

### Azure IoT Hub
- **Device-to-Cloud** telemetry via MQTT
- **Cloud-to-Device** command reception
- **SAS token** authentication with 1-hour validity
- **JSON payload** format with sensor arrays

### Modbus Devices
- **Standard RTU protocol** with CRC validation
- **Multiple slave support** with individual configuration
- **Register mapping** for different sensor types
- **Comprehensive error handling** and retry logic

## Quality Assurance
- **Production-ready** release (v1.0.0)
- **Industrial testing** completed
- **Comprehensive documentation** and support
- **Field deployment** guidelines available

---

**Important Notes for Claude:**
1. This is a **production industrial system** - prioritize stability and reliability
2. **Security conscious** - validate all inputs and protect credentials
3. **Professional grade** - maintain industrial standards and documentation
4. **Real-time requirements** - consider timing constraints for Modbus/MQTT
5. **Multi-sensor support** - handle up to 8 sensors with different configurations
6. **Web interface** is critical for field technicians - keep it simple and robust