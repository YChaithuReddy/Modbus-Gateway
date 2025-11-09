# Build and Testing Guide

## Quick Build Commands

```bash
# Navigate to project
cd "C:\Users\chath\OneDrive\Documents\Python code\modbus_iot_gateway"

# Clean build (recommended for first build after changes)
idf.py fullclean
idf.py build

# Or regular build
idf.py build

# Flash to device (replace COM port as needed)
idf.py -p COM3 flash

# Monitor serial output
idf.py monitor

# Flash and monitor in one command
idf.py -p COM3 flash monitor
```

## Expected Build Output

The build should complete successfully with:
- No compilation errors
- All new source files compiled
- Binary size within 4MB flash limit

## If Build Fails

Check for:
1. **Missing includes** - Verify all header files are present
2. **Type mismatches** - Check function signatures match between .h and .c
3. **Undefined references** - Ensure all functions are implemented
4. **CMakeLists.txt** - Verify all source files are listed

## Common Issues and Fixes

### Issue 1: network_manager.h not found
**Fix:** Verify `main/network_manager.h` exists and is in the correct location

### Issue 2: Undefined reference to network_manager functions
**Fix:** Check `main/CMakeLists.txt` includes `network_manager.c`

### Issue 3: GPIO_NUM_X undefined
**Fix:** Add `#include "driver/gpio.h"` to affected files

## Testing Checklist

Once build succeeds:

### 1. Initial Boot Test
- [ ] Flash device
- [ ] Check serial output for boot messages
- [ ] Verify no crash loops
- [ ] Check for memory allocation errors

### 2. WiFi Mode Test (Regression)
- [ ] Enter config mode (pull GPIO 34 low)
- [ ] Connect to "ModbusIoT-Config" AP
- [ ] Access http://192.168.4.1
- [ ] Configure WiFi settings
- [ ] Test Modbus sensor reading
- [ ] Verify Azure IoT Hub telemetry

### 3. Network Manager Test
- [ ] Check logs for network manager initialization
- [ ] Verify default mode is WiFi
- [ ] Check network stats fetching (signal strength logs)

### 4. Optional Features (Disabled by Default)
- [ ] Verify SIM module NOT initialized (disabled)
- [ ] Verify SD card NOT mounted (disabled)
- [ ] Verify RTC NOT initialized (disabled)

## Serial Monitor Expected Output

Look for these log messages:

```
[NET] 🌐 Initializing Network Manager
[NET] 🚀 Starting network connection (WiFi mode)...
[NET] ✅ Network connected successfully!
[NET] 📊 Network Statistics:
[NET]    Type: WiFi
[NET]    Signal Strength: -XX dBm
```

If SIM/SD/RTC are disabled (default):
```
[RTC] RTC disabled in configuration
[SD] SD card disabled in configuration
[SIM] SIM module disabled in configuration
```

## Troubleshooting

### Device won't boot
- Check power supply (ESP32 needs stable 3.3V)
- Verify flash was successful
- Check for GPIO conflicts

### Network manager errors
- Verify WiFi credentials in iot_configs.h
- Check WiFi network is available
- Look for WiFi init error messages

### Memory issues
- Check heap usage in logs
- Verify partition table allocates enough space
- Look for stack overflow messages

## Next Steps After Successful Build

1. **Test WiFi functionality** - Ensure no regression
2. **Enable SD card** - Modify config to enable SD, test caching
3. **Enable SIM module** - Insert SIM, configure APN, test 4G
4. **Enable RTC** - Test time synchronization

---

**Note:** The current code defaults to WiFi mode with SIM/SD/RTC disabled, so it should behave exactly like the original WiFi-only version until you enable the new features through configuration.
