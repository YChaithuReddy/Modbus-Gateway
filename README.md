# Fluxgen ESP32 Modbus IoT Gateway - Stable Release

[![Stable Release](https://img.shields.io/badge/Status-STABLE-brightgreen.svg)](PRODUCTION_GUIDE.md)
[![Version](https://img.shields.io/badge/Version-1.0.0--Enhanced-blue.svg)](VERSION.md)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-orange.svg)](https://docs.espressif.com/projects/esp-idf/en/v5.4/)
[![64-bit Support](https://img.shields.io/badge/64--bit-Complete-purple.svg)](#)
[![License](https://img.shields.io/badge/License-Industrial-yellow.svg)](#)

## 🏭 Professional Industrial IoT Gateway

A production-ready ESP32-based Modbus IoT gateway designed for industrial environments. Features real-time RS485 Modbus communication, comprehensive ScadaCore data format support, and seamless Azure IoT Hub integration.

## ✨ Key Features

### 🔌 **Industrial Grade Communication**
- **Real-time RS485 Modbus RTU** with professional error handling
- **16 comprehensive data formats** including all ScadaCore interpretations
- **Robust diagnostics** with detailed troubleshooting guides
- **Support for up to 8 sensors** per gateway

### 🌐 **Professional Web Interface**
- **Responsive industrial design** optimized for field use
- **Individual sensor management** - Add, Edit, Delete, Test independently
- **Real-time testing** with comprehensive format display
- **Professional branding** with customizable company logos

### ☁️ **Enterprise Cloud Integration**
- **Azure IoT Hub connectivity** with secure MQTT communication
- **Configurable telemetry intervals** (30-3600 seconds)
- **Automatic reconnection** and error recovery
- **Persistent configuration** in flash memory

## 🚀 Quick Start

### 1. Hardware Setup
```
ESP32 Connections:
├── GPIO 16 (RX2) → RS485 B-
├── GPIO 17 (TX2) → RS485 A+
├── GPIO 4  (RTS) → RS485 Direction Control
└── GND         → Common Ground
```

### 2. Build and Flash
```bash
git clone <repository-url>
cd mqtt_azure_minimal
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 3. Configure via Web Interface
1. Connect to WiFi: `ModbusIoT-Config` (password: `config123`)
2. Open browser: `http://192.168.4.1`
3. Configure WiFi and Azure IoT settings
4. Add sensors with comprehensive format selection
5. Test each sensor individually

## 📊 Supported Data Formats

### 16-bit Formats (1 register)
- **UINT16_BE/LE** - 16-bit unsigned (0 to 65,535)
- **INT16_BE/LE** - 16-bit signed (-32,768 to 32,767)

### 32-bit Integer Formats (2 registers)
- **UINT32_ABCD** - Big Endian (Reg0+Reg1)
- **UINT32_DCBA** - Little Endian (Complete byte swap)
- **UINT32_BADC** - Mid-Big Endian (Word swap)
- **UINT32_CDAB** - Mid-Little Endian (Mixed swap)
- **INT32_ABCD/DCBA/BADC/CDAB** - Signed variants

### 32-bit Float Formats (2 registers)
- **FLOAT32_ABCD/DCBA/BADC/CDAB** - IEEE 754 with all byte orders

## 🔧 Configuration Example

```yaml
Sensor Configuration:
  Name: "Flow Meter Main Line"
  Unit ID: "FM001"
  Slave ID: 1
  Register Address: 30001
  Quantity: 2
  Data Type: "UINT32_ABCD"
  Baud Rate: 9600
```

## 📱 Web Interface Features

### Main Dashboard
- **Professional header** with company branding
- **WiFi configuration** with network scanning
- **Azure IoT Hub** setup with secure credentials
- **Individual sensor management** with comprehensive controls

### Sensor Management
- **Add sensors** with guided form and validation
- **Edit existing sensors** with full format selection
- **Delete sensors** with confirmation dialogs
- **Test sensors** with real-time RS485 communication
- **Comprehensive format display** showing all 16 interpretations

### Real-time Testing
```
Test Results Display:
├── Primary Value: 12345.67 (UINT32_ABCD)
├── Raw Registers: [0x3039, 0x1234]
├── HexString: 30391234
└── All Format Interpretations:
    ├── UINT16-BE: 12345    UINT16-LE: 14640
    ├── INT16-BE: 12345     INT16-LE: 14640
    ├── UINT32-ABCD: 808530484    UINT32-DCBA: 858989616
    ├── INT32-ABCD: 808530484     INT32-DCBA: 858989616
    └── FLOAT32-ABCD: 1.234e-5    FLOAT32-DCBA: 2.567e-6
```

## 🛡️ Production Features

### Industrial Reliability
- **Dual-core architecture** for optimal performance
- **Watchdog timers** and automatic recovery
- **Comprehensive error handling** with specific diagnostics
- **Industrial temperature range** support

### Security
- **WPA2-PSK WiFi encryption**
- **Azure IoT Hub device authentication**
- **Input validation** and sanitization
- **Secure configuration storage**

### Diagnostics
- **Real-time RS485 monitoring** with detailed logs
- **Professional troubleshooting guides** in web interface
- **Comprehensive error reporting** with solutions
- **Performance monitoring** and statistics

## 📁 Project Structure

```
mqtt_azure_minimal/
├── main/
│   ├── main.c              # Main application and dual-core setup
│   ├── web_config.c        # Web interface and sensor management
│   ├── web_config.h        # Configuration structures and API
│   ├── modbus.c           # RS485 Modbus RTU implementation
│   ├── modbus.h           # Modbus protocol definitions
│   ├── iot_configs.h      # Azure IoT Hub credentials
│   └── azure_ca_cert.pem  # Azure root certificate
├── VERSION.md             # Version history and release notes
├── PRODUCTION_GUIDE.md    # Comprehensive deployment guide
├── README.md             # This file
├── sdkconfig             # ESP-IDF configuration
└── partitions_4mb.csv    # Flash memory partition table
```

## 🏭 Production Deployment

### System Requirements
- **Hardware**: ESP32-WROOM-32 with 4MB flash
- **Power**: 3.3V regulated, 500mA minimum
- **Communication**: RS485 transceiver (MAX485/SP485)
- **Network**: WiFi 802.11b/g/n with internet access

### Deployment Checklist
- [ ] Hardware properly wired and tested
- [ ] RS485 network with proper termination
- [ ] WiFi credentials for production network
- [ ] Azure IoT Hub device provisioned
- [ ] All sensors configured and tested
- [ ] Performance benchmarks verified
- [ ] Support procedures established

See [PRODUCTION_GUIDE.md](PRODUCTION_GUIDE.md) for detailed deployment instructions.

## 📖 Documentation

- **[VERSION.md](VERSION.md)** - Version history and release notes
- **[PRODUCTION_GUIDE.md](PRODUCTION_GUIDE.md)** - Comprehensive production deployment guide
- **Web Interface** - Built-in help and troubleshooting guides
- **Serial Monitor** - Detailed diagnostic logging

## 🔄 Version History

### v1.0.0 (Current - Production Ready)
- ✅ Complete ScadaCore data format support (16 formats)
- ✅ Individual sensor management with real-time testing
- ✅ Professional web interface with company branding
- ✅ Enhanced error handling and diagnostics
- ✅ Production-ready reliability and performance
- ✅ Comprehensive documentation and support

## 🆘 Support

### Technical Support
- **Level 1**: Basic configuration and WiFi setup
- **Level 2**: RS485 communication and sensor integration  
- **Level 3**: Custom firmware and enterprise integration

### Troubleshooting
1. **Check hardware connections** - Verify RS485 wiring and power
2. **Monitor serial output** - Use `idf.py monitor` for diagnostics
3. **Test individual sensors** - Use web interface test functionality
4. **Review production guide** - Follow step-by-step procedures

## 🏢 About Fluxgen

**Fluxgen Industrial IoT Solutions** - Professional industrial automation and IoT connectivity solutions for modern manufacturing and process control environments.

---

**🎯 Status: PRODUCTION READY v1.0.0**

*This system is ready for industrial deployment with comprehensive monitoring, diagnostics, and professional support capabilities.*