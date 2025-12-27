# ESP32 Modbus IoT System - Version 1.3.6

## Version Information
- **Version**: 1.3.6 (Production Ready)
- **Release Date**: December 2024
- **Build Target**: ESP32 ESP-IDF v5.5.1
- **Product Name**: Fluxgen Modbus IoT Gateway
- **Company**: Fluxgen Industrial IoT Solutions

## Version History

### Version 1.3.6 - OTA & Device Twin Fixes (Current)
**Release Date**: December 2024

**OTA Updates:**
- ✅ **OTA from GitHub Releases** - Full support for GitHub-hosted firmware
- ✅ **Fixed heap exhaustion** - Stop MQTT before OTA to free memory
- ✅ **Fixed certificate issues** - Skip verification for GitHub CDN
- ✅ **Fixed version reporting** - Device Twin shows correct firmware version
- ✅ **Continuous sensor reading during replay** - No data loss during recovery

### Version 1.2.0 - Autonomous Recovery Release
**Release Date**: December 2024

**Major Features:**
- ✅ **SD Card offline caching** - Store data during network outages
- ✅ **Automatic replay** - Send cached data chronologically when online
- ✅ **Watchdog protection** - 2-minute timeout for stuck main loop
- ✅ **WiFi dongle power cycling** - Auto-recovery with 5-min cooldown

### Version 1.0.0 - Production Release
**Release Date**: December 2024

**Major Features:**
- ✅ **Professional Web-based Configuration Interface**
- ✅ **Real-time RS485 Modbus Communication**
- ✅ **Comprehensive ScadaCore Data Format Support (16 formats)**
- ✅ **Azure IoT Hub Integration with MQTT**
- ✅ **Individual Sensor Management (Add/Edit/Delete/Test)**
- ✅ **WiFi Configuration with Network Scanning**
- ✅ **Enhanced Error Handling and Diagnostics**
- ✅ **Professional Branding and UI/UX**

**Production-Ready Capabilities:**
- Industrial-grade RS485 Modbus RTU communication
- Support for up to 8 individual sensors
- Comprehensive data format interpretations (UINT16/32, INT16/32, FLOAT32 with all byte orders)
- Real-time sensor testing with detailed format display
- Persistent configuration storage in flash memory
- Professional troubleshooting guides and error handling
- Dual-core architecture for optimal performance
- Robust WiFi connectivity with AP/STA modes

**Technical Specifications:**
- **Hardware**: ESP32 dual-core (240MHz)
- **Flash Memory**: 4MB with custom partition table
- **RAM**: 520KB SRAM
- **Communication**: RS485 (UART2), WiFi 802.11b/g/n
- **Protocols**: Modbus RTU, MQTT, HTTP/HTTPS
- **Data Formats**: 16 comprehensive ScadaCore interpretations
- **Operating Temperature**: Industrial grade (-40°C to +85°C)

## Breaking Changes from Previous Versions
- **Data Type Enhancement**: Previous generic types (UINT16, UINT32, etc.) now support specific byte order formats
- **Individual Sensor Save**: Sensors are now saved individually instead of batch configuration
- **Enhanced UI**: Complete redesign with professional industrial interface
- **New Endpoints**: Added `/save_single_sensor` for individual sensor management

## Backward Compatibility
- ✅ **Existing configurations preserved** - Old data types automatically map to new BE formats
- ✅ **Configuration migration** - Seamless upgrade from previous versions
- ✅ **API compatibility** - All existing endpoints maintained for backwards compatibility

## Known Issues
- None reported for production release

## Next Version (1.1.0) - Planned Features
- Multi-language support
- Advanced data logging and analytics
- Custom Modbus function codes
- OTA (Over-The-Air) firmware updates
- Enhanced security features