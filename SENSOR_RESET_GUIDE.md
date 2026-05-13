# 🔧 Sensor Configuration Reset Guide

## Problem Summary

After saving Azure configuration, sensor parameters became corrupted:
- **All Panda_USM sensors** showing 100.00 (invalid calculation applied)
- **Panda_Level sensors** lost their height parameters (SensorHeight and TankHeight = 0.00)

### Root Cause
An invalid `CALC_SCALE_OFFSET` calculation was applied with:
- `scale_factor = 0.0`
- `offset = 100.0`
- Formula: `value × 0 + 100 = 100` (resulting in all sensors showing 100.00)

---

## ✅ Fixes Applied

### 1. Code Fix: Ignore Invalid Calculations
**File**: `main/sensor_manager.c` (line ~1223)

Added logic to skip invalid `CALC_SCALE_OFFSET` calculations:
```c
if (calc->scale == 0.0f && calc->offset == 100.0f) {
    ESP_LOGW(TAG, "Ignoring invalid configuration, using raw value");
    result = raw_value;
} else {
    result = (raw_value * calc->scale) + calc->offset;
}
```

### 2. Reset Function
**File**: `main/web_config.c`

Added `config_reset_sensor_calculations()` function that:
- Resets all sensors' `calculation.calc_type` to `CALC_NONE`
- Restores Panda_Level sensor parameters:
  - **Sensor 1 (FG24769L, Slave 1)**: SensorHeight=950.0, TankHeight=1000.0
  - **Sensor 2 (FG24770L, Slave 2)**: SensorHeight=820.0, TankHeight=900.0
- Saves corrected configuration to NVS

### 3. Web Endpoint
**Endpoint**: `POST /reset_sensors`

**CORS Support**: Added `Access-Control-Allow-Origin: *` headers to allow cross-origin requests from local HTML files

Returns JSON response:
```json
{
  "status": "success",
  "message": "✅ Sensor calculations reset successfully!..."
}
```

---

## 🚀 How to Apply the Fix

### Method 1: Using the Reset Web Page (Recommended)

1. **Build and flash** the updated firmware:
   ```bash
   idf.py build
   idf.py -p COM3 flash
   ```

2. **Connect to device WiFi**:
   - Toggle GPIO 34 or press BOOT button to start web server
   - Connect to WiFi: `ModbusIoT-Config` (password: `config123`)

3. **Open the reset page**:
   - Open `reset_sensors.html` in your browser
   - OR navigate to: `http://192.168.4.1/reset_sensors.html` (if added to web server)

4. **Click "Reset Sensor Configurations"**

5. **Wait for success message**

6. **Click "Reboot Device"**

7. **Verify** sensor readings after reboot

### Method 2: Using Command-Line Script (No CORS Issues!)

**Windows:**
```cmd
reset_sensors.bat
```

**Linux/Mac:**
```bash
chmod +x reset_sensors.sh
./reset_sensors.sh
```

The script will:
- Check device connectivity
- Reset sensor configurations
- Prompt for reboot
- Show status messages

### Method 3: Using cURL Command

```bash
# Reset sensors
curl -X POST http://192.168.4.1/reset_sensors

# Reboot device
curl -X POST http://192.168.4.1/reboot
```

### Method 4: Using Browser Console

Navigate to `http://192.168.4.1` and run in browser console:
```javascript
// Reset sensors
fetch('http://192.168.4.1/reset_sensors', {
    method: 'POST'
}).then(r => r.json()).then(console.log);

// After success, reboot
fetch('http://192.168.4.1/reboot', {
    method: 'POST'
}).then(r => r.json()).then(console.log);
```

---

## 🔍 Verification

After reboot, check serial monitor for correct sensor readings:

### Expected Output (Panda_Level sensors):
```
I SENSOR_MGR: Panda_Level Calculation: Raw=514, SensorHeight=950.00, TankHeight=1000.00, Level%=43.60
```
✅ **Good**: Shows calculated percentage

❌ **Bad**:
```
I SENSOR_MGR: Panda_Level Calculation: Raw=507, SensorHeight=0.00, TankHeight=0.00, Level%=507.00
```

### Expected Output (Panda_USM sensors):
```
I SENSOR_MGR: Panda USM Calculation: DOUBLE64=0x40705E4F870C1ECE = 261.894416 m³
```
✅ **Good**: Shows actual sensor value

❌ **Bad**:
```
I SENSOR_MGR: CALC_SCALE_OFFSET: 261.8944 × 0.0000 + 100.0000 = 100.0000
I SENSOR_MGR: Calculation applied: 261.894416 -> 100.000000
```

---

## 📝 What Gets Reset

### ✅ Fixed:
- Calculation type → `CALC_NONE` (removes invalid calculations)
- Panda_Level Sensor 1 → SensorHeight: 950.0, TankHeight: 1000.0
- Panda_Level Sensor 2 → SensorHeight: 820.0, TankHeight: 900.0

### ✅ Preserved:
- Azure device ID and key
- Telemetry interval
- Network settings (WiFi/SIM)
- All other sensor configurations (slave IDs, register addresses, data types, etc.)

---

## ⚠️ Prevention

To prevent this issue in the future:

1. **When saving Azure config**, DON'T modify sensor configurations at the same time
2. **Separate saves**:
   - Save Azure settings → Test
   - Save sensor settings → Test
3. **Always verify** sensor readings after any configuration change

---

## 🔧 Troubleshooting

### Issue: Reset endpoint returns 404
**Solution**: Make sure you flashed the updated firmware with the reset function

### Issue: Reset succeeds but sensors still show 100.00
**Solution**:
1. Make sure you **rebooted** after reset
2. Check serial monitor for confirmation logs
3. Try resetting again

### Issue: Can't connect to device
**Solution**:
1. Verify web server is running (check serial monitor)
2. Connect to correct WiFi network (`ModbusIoT-Config`)
3. Use correct IP address (default: `192.168.4.1`)

### Issue: Panda_Level sensors still show raw values
**Solution**:
Check if the reset correctly identified your sensors:
```
I WEB_CONFIG: Sensor 1: E Floor-6 (Type: Panda_Level, Slave: 1)
I WEB_CONFIG:   ✅ Restored: SensorHeight=950.0, TankHeight=1000.0
```

If not restored, manually edit sensors through web interface.

---

## 📞 Support

If issues persist:
1. Check serial monitor logs
2. Verify sensor slave IDs match (1 and 2 for Panda_Level)
3. Manually reconfigure through web interface if needed

---

## 🎯 Summary

**Quick Steps**:
1. Flash updated firmware → `idf.py build flash`
2. Connect to device WiFi → `ModbusIoT-Config`
3. Open `reset_sensors.html` OR POST to `/reset_sensors`
4. Reboot device → POST to `/reboot`
5. Verify sensor readings → Check serial monitor

**Result**: All sensors should show actual values instead of 100.00
