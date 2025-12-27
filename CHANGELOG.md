# ESP32 Modbus IoT Gateway - Changelog

## Version 1.3.6 - OTA & Device Twin Fixes (December 2024)

### 🔧 **OTA Updates from GitHub Now Working**

#### **Fixed Critical OTA Issues**
- **[CRITICAL FIX]** OTA updates from GitHub releases now work reliably
  - **Root Cause**: Multiple issues - certificate verification, heap exhaustion, long redirect URLs
  - **Solution**: Multi-version iterative fixes (v1.3.1 through v1.3.6)

#### **OTA Fixes by Version**

| Version | Issue | Fix |
|---------|-------|-----|
| v1.3.1 | Certificate verification failed on GitHub CDN | Skip cert verification for GitHub URLs |
| v1.3.3 | 923-byte redirect URL crashed HTTP client | Added redirect handling configuration |
| v1.3.4 | Heap exhaustion (52 bytes min free) | Stop MQTT before OTA, reduce buffer sizes |
| v1.3.5 | Auto-redirect doesn't work with streaming API | Re-enabled event handler for manual redirects |
| v1.3.6 | Device Twin showed wrong firmware version | Changed hardcoded "1.0.0" to FW_VERSION_STRING |

#### **Technical Changes**
- **Stop MQTT before OTA** - Frees ~30KB heap for OTA TLS connection (both WiFi and SIM modes)
- **Reduced HTTP buffer sizes** - 4KB/1KB instead of 16KB/4KB to save memory
- **Skip certificate verification for GitHub** - GitHub CDN certificates cause issues on ESP32
- **Fixed firmware version reporting** - Device Twin now shows actual version from `iot_configs.h`
- **MQTT restart after OTA** - Automatically restarts MQTT after OTA cleanup

#### **Files Modified**
- `main/ota_update.c` - OTA download logic improvements
- `main/main.c` - Fixed hardcoded firmware version in Device Twin
- `main/iot_configs.h` - Version bumped to 1.3.6

### 🌐 **Continuous Sensor Reading During SD Replay**

#### **Fixed Data Loss During Replay**
- **[CRITICAL FIX]** Sensors now continue reading during SD card replay
  - **Problem**: During 4+ hour replay, sensor data was lost
  - **Solution**: Pause replay every 5 minutes to read sensors and cache to SD
  - **Impact**: Zero data loss during network recovery

---

## Version 1.1.0-final - Recent Updates

### 🔧 **RS485 Testing System Enhancements** - 2025-01-XX

#### **Fixed Critical Issues**
- **[CRITICAL FIX]** Fixed 404 error for `/test_rs485` endpoint 
  - **Root Cause**: HTTP server was limited to 10 URI handlers but 13 were being registered
  - **Solution**: Increased `max_uri_handlers` from 10 to 16
  - **File**: `web_config.c:2038`
  - **Impact**: RS485 testing buttons now work properly in both edit and new sensor forms

#### **Enhanced Error Handling**
- **Added comprehensive form validation** in RS485 testing functions
  - Validates form element existence before testing
  - Provides default values for empty fields
  - Shows specific error messages for missing elements
  - **Files**: `web_config.c:1171-1194` (new sensor), `web_config.c:1206-1217` (edit sensor)

#### **New Features Added**
- **[NEW]** Basic Connection Test button `[⚡ BASIC] Test Connection`
  - Tests with common default settings (Slave ID 1, Register 0, 9600 baud)
  - Provides quick connectivity verification
  - **Function**: `testBasicConnection()` at `web_config.c:968-985`

- **[NEW]** Enhanced Quick Diagnostic help `[🔍 HELP] Quick Diagnostic`
  - Hardware checklist with GPIO pin mappings
  - Common configuration settings
  - Step-by-step troubleshooting guide
  - **Function**: `quickDiagnostic()` at `web_config.c:948-967`

#### **Improved Error Messages**
- **Enhanced troubleshooting guidance** for different error types:
  - Timeout errors: Wiring and power check guidance
  - CRC errors: Cable quality and interference solutions
  - Invalid register: Register address recommendations
  - Device failure: Power cycle and status check steps
  - **File**: `web_config.c:1495-1534`

#### **Debug Improvements**
- **Added console logging** for all RS485 test parameters
- **Added endpoint registration verification** with success/failure logging
- **Added startup logging** showing all available endpoints
- **Files**: Debug logs throughout `web_config.c`

### 🌐 **Azure IoT Hub Configuration** - 2025-01-XX

#### **Separate Azure Menu**
- **[NEW]** Created dedicated "☁️ Azure IoT Hub" menu section
  - Separated from WiFi configuration for better organization
  - Independent save functionality
  - Enhanced security with password fields
  - **Files**: `web_config.c:102-104` (menu), `web_config.c:604-644` (section)

#### **Fixed Configuration Parsing**
- **[CRITICAL FIX]** Added missing `azure_device_key` parameter handling
  - **Root Cause**: Form parameter was displayed but not saved
  - **Solution**: Added parsing in `parse_form_param()` function
  - **File**: `web_config.c:1062-1064`
  - **Impact**: Azure device key now properly saves to NVS storage

#### **Enhanced Azure Configuration**
- **Password field with show/hide toggle** for device key
- **Form validation** for required Azure parameters
- **Separate save endpoint** `/save_azure_config` with dedicated handler
- **Real-time feedback** with success/error messages

### 📱 **Navigation Improvements** - 2025-01-XX

#### **Fixed Sensor Page Navigation**
- **[CRITICAL FIX]** Sensor operations now stay on Modbus sensor page
  - **Root Cause**: `location.reload()` was redirecting to overview page
  - **Solution**: Changed to `window.location.href='/sensors'`
  - **Files**: Multiple functions in `web_config.c:876, 1084, 1086, 1091`
  - **Impact**: Save, edit, cancel, delete operations maintain page context

### 🛠️ **Compilation Fixes** - 2025-01-XX

#### **Buffer Overflow Resolution**
- **[CRITICAL FIX]** Fixed HTML buffer overflow in Azure configuration section
  - **Root Cause**: HTML string (1958-2095 bytes) exceeded 1024-byte buffer
  - **Solution**: Split Azure section into 3 smaller chunks
  - **Files**: `web_config.c:604-644` (split into parts 1-3)

#### **Syntax Error Fixes**
- **Fixed missing parenthesis** in snprintf calls
- **Fixed unused variable warnings** 
- **Fixed URL decode function parameter count**

### 🎨 **UI/UX Improvements** - 2025-01-XX

#### **Cleaned Up Edit Form Interface**
- **[REMOVED]** Quick diagnostic button from edit sensor forms
  - **Rationale**: Simplified interface, diagnostic help still available in new sensor forms
  - **File**: `web_config.c:1137` 
  - **Impact**: Cleaner edit form with focus on essential testing functionality

#### **Standardized RS485 Test Output**
- **[ENHANCEMENT]** Unified RS485 test output format across all forms
  - **Change**: Made new sensor and edit sensor test output match saved sensor test format
  - **Features**: Same professional table layout, comprehensive data interpretations
  - **Files**: 
    - Frontend: `web_config.c:1205, 1240` (JavaScript functions)
    - Backend: `web_config.c:2001-2033` (HTML response generation)
  - **Impact**: Consistent user experience across all testing scenarios

#### **Test Interface Optimization**
- **Note**: Test connection button was already not present in edit forms (only in new sensor forms)
- **Result**: Edit forms now have streamlined interface with just essential [TEST] and [SAVE] buttons

#### **Buffer Overflow Fix**
- **[CRITICAL FIX]** Fixed buffer overflow in RS485 test response handler
  - **Root Cause**: Response buffer (1024 bytes) too small for HTML content (up to 2048 bytes)
  - **Solution**: Increased buffer size from 1024 to 3072 bytes
  - **File**: `web_config.c:1964`
  - **Impact**: Prevents compilation errors and ensures reliable RS485 test output

#### **Enhanced RS485 Test Output Format**
- **[ENHANCEMENT]** Updated RS485 test output to match saved sensor test format
  - **New Features**: 
    - Comprehensive data format interpretations (UINT16, INT16, UINT32, INT32, FLOAT32)
    - Primary value determination with byte order indication
    - Raw register display and hex string representation
    - All byte order combinations (ABCD, DCBA, BADC, CDAB)
    - Scientific notation for FLOAT32 interpretations
  - **Files**: `web_config.c:1964-2070`
  - **Impact**: Consistent professional output format across all RS485 testing scenarios
  - **Buffer Size**: Increased to 4096 bytes to accommodate comprehensive output

#### **Critical Memory Management Fix**
- **[CRITICAL FIX]** Fixed ESP32 LoadProhibited crash in RS485 test handler
  - **Root Cause**: Large stack-allocated buffers (4096+ bytes) causing stack overflow and memory corruption
  - **Solution**: 
    - Moved all buffers to heap allocation using malloc/free (response, data_output, temp)
    - Reduced buffer sizes with proper bounds checking
    - Limited register processing to 4 registers max for safety
    - Added proper memory cleanup and error handling
    - Fixed malformed JSON response structure
  - **Files**: `web_config.c:1964-2088` (heap allocation), `web_config.c:21` (stdlib include)
  - **Impact**: Eliminates ESP32 crashes and ensures proper web response delivery

#### **RS485 Web Response Fix**
- **[CRITICAL FIX]** Fixed web server response not displaying RS485 test results
  - **Root Cause**: Malformed JSON structure preventing proper response delivery
  - **Solution**: 
    - Corrected JSON formatting for proper web display
    - Ensured all allocated memory is properly cleaned up
    - Maintained comprehensive data format output
  - **Files**: `web_config.c:1984-2088`
  - **Impact**: RS485 test results now display correctly in web interface

#### **Compilation Error Fix**
- **[CRITICAL FIX]** Fixed sizeof() compilation errors on heap-allocated buffers
  - **Root Cause**: Using `sizeof(temp)` on pointer returns pointer size (8 bytes), not allocated buffer size (200 bytes)
  - **Solution**: 
    - Replaced all `sizeof(temp)` with explicit buffer size `200`
    - Fixed buffer truncation warnings by using correct buffer sizes
    - Maintained memory safety with proper bounds checking
  - **Files**: `web_config.c:2004, 2007, 2014, 2022, 2034, 2049, 2054, 2063`
  - **Impact**: Eliminates compilation errors and ensures proper string formatting

#### **HTML Table Format for RS485 Test Results**
- **[ENHANCEMENT]** Converted RS485 test output from plain text to professional HTML table format
  - **Features**: 
    - Styled green success header matching saved sensor tests
    - Professional HTML table with alternating row colors (#f0f0f0)
    - Monospace font for data clarity
    - Comprehensive data format interpretations in tabular format
    - Primary value, raw registers, and hex string display
    - Responsive table design with proper CSS styling
  - **Files**: `web_config.c:1983-2074`
  - **Impact**: RS485 test results now display in same professional tabular format as saved sensors

#### **Scale Factor Implementation**
- **[NEW FEATURE]** Added complete scale factor functionality to all sensor forms and testing
  - **New Features**: 
    - Scale factor input field in new sensor forms (default: 1.0)
    - Scale factor display in existing sensor cards
    - Scale factor parsing in form submission handlers
    - Scale factor parameter in RS485 test functions
    - Dual display: raw value and scaled value (×scale factor)
    - Scale factor validation and error handling
  - **Files**: 
    - `web_config.c:750` (new sensor form field)
    - `web_config.c:663` (sensor card display)
    - `web_config.c:1715-1716` (form parsing)
    - `web_config.c:1944, 1959-1960` (test parameter parsing)
    - `web_config.c:2008-2020` (scaled value display)
    - `web_config.c:1190-1205` (JavaScript test function)
  - **Impact**: Users can now set scale factors to convert raw sensor values to engineering units

#### **Partition Table Fix for 4MB ESP32**
- **[CRITICAL FIX]** Fixed ESP32 application size overflow by enabling 4MB partition table
  - **Root Cause**: Application size (1,052,896 bytes) exceeded default 1MB app partition (1,048,576 bytes)
  - **Solution**: 
    - Switched from `PARTITION_TABLE_SINGLE_APP` to `PARTITION_TABLE_CUSTOM`
    - Enabled `partitions_4mb.csv` with 3MB app partition (0x300000 bytes)
    - Updated sdkconfig to use custom partition table
    - Requires build clean and rebuild to apply changes
  - **Files**: 
    - `sdkconfig:398-404` (partition table configuration)
    - `sdkconfig.defaults:5-7` (default configuration)
    - `partitions_4mb.csv` (4MB partition layout)
  - **Impact**: Eliminates build errors and provides room for future feature expansion

#### **Professional UI Cleanup**
- **[ENHANCEMENT]** Cleaned up user interface by removing brackets and emoji symbols
  - **Changes Made**: 
    - Removed all emoji symbols from menu items and section headings
    - Removed brackets from all button names (e.g., "[SAVE]" → "Save")
    - Cleaned up section titles (e.g., "📊 System Overview" → "System Overview")
    - Updated password field show/hide toggles
    - Maintained Azure IoT device key password field functionality
    - Fixed HTML string structure for menu items
  - **Files**: `web_config.c:98-111` (menu items), `web_config.c:564-772` (sections and buttons)
  - **Impact**: Professional, clean interface suitable for industrial applications

#### **Compilation Fix for Menu Items**
- **[CRITICAL FIX]** Fixed broken HTML string structure in menu items
  - **Root Cause**: Menu text was not properly enclosed in string literals after emoji removal
  - **Solution**: Wrapped all menu item text in proper string quotes
  - **Files**: `web_config.c:98-111`
  - **Impact**: Eliminates compilation errors and ensures proper menu display

#### **Remove Unnecessary Test Buttons**
- **[ENHANCEMENT]** Removed Basic Test Connection and Quick Diagnostic buttons from interface
  - **Buttons Removed**: 
    - "Basic Test Connection" button from new sensor forms
    - "Quick Diagnostic" button from new sensor forms
    - Both corresponding JavaScript function definitions
  - **Rationale**: Streamlined interface focusing on essential RS485 testing functionality
  - **Files**: `web_config.c:753, 755` (button removal), `web_config.c:950-985` (function removal)
  - **Impact**: Cleaner, more focused sensor configuration interface

---

## Development Notes

### **Code Quality Standards**
- All changes maintain existing code style and conventions
- Enhanced error handling with user-friendly messages
- Comprehensive logging for debugging
- Proper resource management and cleanup

### **Testing Workflow**
1. **Basic Connection Test** - Verify hardware connectivity
2. **Parameter Testing** - Test with specific sensor settings  
3. **Error Diagnostics** - Built-in troubleshooting guidance

### **Future Enhancement Areas**
- Configurable timeout values for RS485 communication
- Automatic device discovery and scanning
- Real-time signal quality monitoring
- Enhanced statistics dashboard
- Bulk sensor import/export functionality

---

*Generated by Claude Code Assistant - All changes documented and tested*