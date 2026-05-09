# Production Deployment Guide - ESP32 Modbus IoT Gateway v1.3.8

## 🏭 Production Ready Features

### ✅ Industrial Grade Communication
- **Real-time RS485 Modbus RTU** with comprehensive error handling
- **Professional ScadaCore format support** - 16 data interpretations (UINT16/32, INT16/32, FLOAT32 with ABCD/DCBA/BADC/CDAB byte orders)
- **Robust error detection** - CRC validation, timeout handling, device failure detection
- **Industrial temperature range** support (-40°C to +85°C)

### ✅ Professional Web Interface
- **Responsive design** optimized for industrial environments
- **Individual sensor management** - Add, Edit, Delete, Test sensors independently
- **Real-time sensor testing** with comprehensive format display
- **Professional troubleshooting guides** with step-by-step diagnostics
- **Company branding** with customizable logo and styling

### ✅ Enterprise Connectivity
- **Azure IoT Hub integration** with secure MQTT communication
- **WiFi management** with network scanning and dual-mode operation
- **Persistent configuration** stored in flash memory with NVS
- **Automatic reconnection** and error recovery

## 🔧 Hardware Requirements

### Minimum Requirements
- **ESP32-WROOM-32** or compatible module
- **4MB Flash Memory** (minimum required)
- **RS485 transceiver** (MAX485, SP485, or similar)
- **A7670C SIM module** (for cellular connectivity, optional if using WiFi only)
- **SD Card module** (SPI mode, for offline data caching)
- **DS3231 RTC module** (for accurate timestamps when offline)
- **Power supply**: 3.3V regulated, minimum 500mA

### Complete Hardware Pin Map (v1.3.8)
```
╔══════════════════════════════════════════════════════════════╗
║              ESP32 GPIO Pin Assignments                      ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  RS485 Modbus (UART2)                                        ║
║  ├── GPIO 16  → RS485 RX (RXD2)                             ║
║  ├── GPIO 17  → RS485 TX (TXD2)                             ║
║  └── GPIO 18  → RS485 RTS (Direction Control)               ║
║                                                              ║
║  SIM Module - A7670C (UART1, configurable via web UI)        ║
║  ├── GPIO 33  → SIM TX (ESP32 TX → A7670C RX)               ║
║  ├── GPIO 32  → SIM RX (ESP32 RX ← A7670C TX)              ║
║  ├── GPIO 4   → SIM Power Key (PWR)                         ║
║  └── Reset    → Disabled by default (GPIO 15 conflict w/SD) ║
║                                                              ║
║  SD Card (SPI3)                                              ║
║  ├── GPIO 23  → MOSI                                        ║
║  ├── GPIO 19  → MISO                                        ║
║  ├── GPIO 5   → CLK                                         ║
║  └── GPIO 15  → CS (Chip Select)                            ║
║                                                              ║
║  DS3231 RTC (I2C0)                                           ║
║  ├── GPIO 21  → SDA                                         ║
║  └── GPIO 22  → SCL                                         ║
║                                                              ║
║  Status LEDs                                                 ║
║  ├── GPIO 25  → Web Server Active (ON when AP running)      ║
║  ├── GPIO 26  → MQTT Connected (ON when Azure connected)    ║
║  └── GPIO 27  → Sensor Response (blinks on Modbus read)     ║
║                                                              ║
║  Control Buttons                                             ║
║  ├── GPIO 34  → Config Button (rising edge trigger)         ║
║  └── GPIO 0   → Boot Button (falling edge trigger)          ║
║                                                              ║
║  Modem Reset                                                 ║
║  └── GPIO 2   → Modem hardware reset (configurable via web) ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝

Power Requirements:
├── Input: 5V DC or 3.3V regulated
├── Current: 1A minimum (A7670C module draws 2A peak during TX)
└── Backup: Optional battery for RTC time retention
```

### GPIO Conflict Notes
```
⚠️  Known Conflicts:
├── GPIO 15 is shared between SD Card CS and SIM Reset
│   → SIM Reset is DISABLED by default to avoid conflict
│   → If SD card is not used, SIM Reset can be enabled via web UI
├── GPIO 18 was changed from GPIO 32 for RS485 RTS
│   → GPIO 32 is used by SIM RX, so RTS moved to GPIO 18
└── GPIO 2 is used for Modem Reset AND has onboard LED on some boards
    → Be aware of LED blinking during modem reset cycles
```

### RS485 Network Setup
```
RS485 Bus Topology:
┌─────────────┐    ┌──────────────┐    ┌──────────────┐
│   ESP32     │    │   Sensor 1   │    │   Sensor 2   │
│  Gateway    │────│  (Slave 1)   │────│  (Slave 2)   │
│  GPIO16/17  │    │  e.g. Flow   │    │  e.g. Opruss │
└─────────────┘    └──────────────┘    └──────────────┘
      │                    │                    │
   120Ω Term          (Optional)           120Ω Term

Network Specifications:
├── Max Distance: 1200 meters (4000 feet)
├── Max Devices: 247 (practical limit ~32)
├── Max Sensors per Gateway: 10 (reduced from 15 for heap savings)
├── Termination: 120Ω resistors at both line ends
├── Default Baud Rate: 9600 bps (configurable per sensor)
├── Supported Baud Rates: 9600, 19200, 38400, 115200 bps
└── Topology: Linear daisy-chain (no stars or branches)

Supported Sensor Types:
├── Flow Meters (UINT16/32, INT16/32, FLOAT32 with byte order variants)
├── Aquadax Quality (COD, BOD, TSS, pH, Temp - 12 registers, CDAB+byteswap)
├── Opruss Ace Quality (COD, BOD, TSS - 22 registers, FLOAT32 CDAB)
├── Opruss Ace TDS (TDS + Temp - input registers 0x04, FLOAT32 DCBA)
└── Generic Modbus sensors (configurable data type and byte order)
```

## 🚀 Production Deployment Steps

### 1. Flash the Firmware
```bash
# Build and flash the firmware
cd /path/to/mqtt_azure_minimal
idf.py build
idf.py -p /dev/ttyUSB0 flash

# Monitor for successful startup
idf.py monitor
```

### 2. Initial Configuration
1. **Power on ESP32** - Look for WiFi network `ModbusIoT-Config`
2. **Connect to configuration network**:
   - SSID: `ModbusIoT-Config`
   - Password: `config123`
3. **Open web browser** to `http://192.168.4.1`
4. **Configure WiFi settings** for production network
5. **Configure Azure IoT Hub** credentials
6. **Add sensors** using individual sensor forms

### 3. Sensor Configuration
```
Production Sensor Setup Example:
┌─────────────────────────────────────┐
│ Sensor Name: Flow Meter Main Line  │
│ Unit ID: FM001                      │
│ Slave ID: 1                         │
│ Register Address: 30001             │
│ Quantity: 2 (for 32-bit data)      │
│ Data Type: UINT32_ABCD              │
│ Baud Rate: 9600 bps                 │
└─────────────────────────────────────┘

Supported Data Types:
├── UINT16_BE/LE - 16-bit unsigned (counters, status)
├── INT16_BE/LE  - 16-bit signed (temperature, pressure)
├── UINT32_ABCD/DCBA/BADC/CDAB - 32-bit unsigned (totalizers)
├── INT32_ABCD/DCBA/BADC/CDAB  - 32-bit signed (differences)
└── FLOAT32_ABCD/DCBA/BADC/CDAB - IEEE 754 floats (precise values)
```

### 4. Production Testing
```bash
# Test each sensor individually:
1. Click [TEST] Test RS485 on each sensor
2. Verify real-time communication
3. Check all 16 format interpretations
4. Validate data ranges and byte orders
5. Confirm Azure IoT Hub data transmission

# Expected Test Results:
✅ RS485 Communication Success
✅ Comprehensive format display (16 interpretations)
✅ Data matches expected sensor values
✅ Azure IoT Hub receives telemetry data
```

## 📊 Operational Monitoring

### Key Performance Indicators (KPIs)
```
System Health Monitoring:
├── RS485 Communication Success Rate: >99.5%
├── WiFi Connection Uptime: >99.9%
├── Azure IoT Hub Message Delivery: >99.8%
├── Sensor Response Time: <1000ms
├── Memory Usage: <80% of available RAM
└── Flash Wear: Monitor NVS write cycles
```

### Diagnostic Capabilities
- **Real-time RS485 diagnostics** with detailed error reporting
- **Comprehensive format testing** shows all data interpretations
- **Professional troubleshooting guides** for common issues
- **Serial monitor logging** for advanced diagnostics
- **Web interface error reporting** with specific solutions

### Production Logs Analysis
```bash
# Monitor production logs:
idf.py monitor | grep -E "(ERROR|SUCCESS|Modbus|WiFi|Azure)"

# Key log messages to monitor:
✅ "Modbus RS485 initialization complete"
✅ "WiFi connected successfully"  
✅ "Azure IoT Hub connection established"
❌ "RS485 Communication Timeout" - Check wiring/power
❌ "CRC verification failed" - Check electromagnetic interference
❌ "WiFi connection failed" - Check network credentials
```

## 🛡️ Security Considerations

### Production Security Features
- **WPA2-PSK WiFi encryption** for network security
- **Azure IoT Hub device keys** for cloud authentication
- **HTTPS support** for secure web interface access
- **Input validation** prevents malicious configuration data
- **Secure boot** capabilities (ESP32 hardware feature)

### Recommended Security Practices
```
Security Checklist:
├── ✅ Change default WiFi AP password from 'config123'
├── ✅ Use strong Azure IoT Hub device keys
├── ✅ Implement network segmentation for IoT devices
├── ✅ Regular firmware updates via secure channels
├── ✅ Physical security for ESP32 device enclosure
└── ✅ Monitor for unauthorized configuration changes
```

## 🔄 Maintenance and Updates

### Routine Maintenance
```
Weekly:
├── Check RS485 communication success rates
├── Verify Azure IoT Hub data flow
├── Monitor system memory usage
└── Review error logs for patterns

Monthly:
├── Backup sensor configurations
├── Check physical connections and terminations
├── Update firmware if new versions available
└── Performance optimization review

Quarterly:
├── Full system health assessment
├── RS485 network integrity testing
├── Security audit and updates
└── Capacity planning review
```

### Configuration Backup
```bash
# Backup current configuration:
# All sensor configs stored in NVS flash memory
# Web interface allows export of sensor configurations
# Azure IoT Hub maintains telemetry history

# Recovery procedure:
1. Flash firmware to new device
2. Access configuration interface
3. Re-enter sensor configurations
4. Test each sensor individually
5. Verify Azure connectivity
```

## 📈 Scalability and Performance

### Production Capacity
- **Maximum sensors**: 10 per ESP32 gateway (reduced from 15 for heap savings)
- **Update frequency**: Configurable (30-3600 seconds, default 300s/5min)
- **Concurrent connections**: 7 web interface sockets (LRU purge disabled)
- **Data throughput**: Up to 1000 Modbus requests/minute
- **Network capacity**: 32 devices per RS485 network (practical limit)
- **Offline caching**: SD card stores telemetry when MQTT disconnected
- **OTA updates**: Firmware updates via Azure Device Twin or GitHub releases

### Multi-Gateway Deployment
```
Large Scale Deployment:
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  Gateway 1  │    │  Gateway 2  │    │  Gateway 3  │
│  (8 sensors)│    │  (8 sensors)│    │  (8 sensors)│
└─────────────┘    └─────────────┘    └─────────────┘
      │                    │                    │
      └────────────────────┼────────────────────┘
                           │
                  ┌─────────────────┐
                  │   Azure IoT Hub │
                  │   Central Cloud │
                  └─────────────────┘

Benefits:
├── Distributed processing reduces network load
├── Independent operation if one gateway fails  
├── Centralized monitoring and analytics
└── Scalable to thousands of sensors
```

## 🆘 Production Support

### Technical Support Matrix
```
Issue Level 1 - Basic Configuration:
├── WiFi connection problems
├── Basic sensor setup
├── Web interface access
└── Standard troubleshooting

Issue Level 2 - Advanced Integration:
├── RS485 communication debugging
├── Custom data format configuration
├── Azure IoT Hub integration
└── Performance optimization

Issue Level 3 - System Integration:
├── Custom firmware modifications
├── Enterprise integration requirements
├── Large-scale deployment planning
└── Advanced security implementations
```

### Emergency Procedures
1. **Device Reset**: Hold boot button during power-on
2. **Factory Reset**: Flash firmware with `idf.py erase_flash`
3. **Configuration Recovery**: Use backup configuration files
4. **Network Isolation**: Disconnect from production network if compromised
5. **Escalation**: Contact technical support with system logs

---

## 🎯 Production Readiness Checklist

### Pre-Deployment Verification
- [ ] Hardware connections verified and documented
- [ ] RS485 network tested with all connected devices
- [ ] WiFi connectivity stable in production environment
- [ ] Azure IoT Hub credentials configured and tested
- [ ] All sensors configured and individually tested
- [ ] Error handling and recovery procedures verified
- [ ] Performance benchmarks established
- [ ] Security measures implemented
- [ ] Documentation complete and accessible
- [ ] Support procedures established

### Go-Live Criteria
- [ ] 99%+ RS485 communication success rate
- [ ] All sensors reporting expected data ranges
- [ ] Azure IoT Hub receiving consistent telemetry
- [ ] Web interface responsive and accessible
- [ ] Error recovery working as expected
- [ ] Performance meeting or exceeding targets
- [ ] Team trained on operation and maintenance
- [ ] Emergency procedures tested and documented

**System Status: PRODUCTION READY - Version 1.3.8**

*This system has been rigorously tested and is ready for industrial deployment with comprehensive monitoring, diagnostics, and support capabilities.*

**Partition Table**: FROZEN as of v1.3.7 - DO NOT modify `partitions_4mb.csv`