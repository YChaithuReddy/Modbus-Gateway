# Level Sensor Configuration Fix

## Problem
The level sensor is giving a negative level_filled value (-38592.5%) instead of the correct percentage.

## Root Cause
The scale_factor is set incorrectly, causing the raw millimeter reading to not be properly converted to meters.

## Solution

### 1. Correct Sensor Configuration Values:
- **Sensor Height**: 2.228 meters (or 2228 mm)
- **Max Water Level (Tank Height)**: 2.228 meters (or 2228 mm)
- **Scale Factor**: 0.001 (converts millimeters to meters)

### 2. Configuration Steps:

#### Option A: Via Web Interface
1. Open the web configuration interface (http://192.168.4.1 in config mode)
2. Find your Level sensor configuration
3. Update these values:
   - Scale Factor: **0.001**
   - Sensor Height: **2.228**
   - Max Water Level: **2.228**
4. Save the configuration

#### Option B: Check Current Configuration in Code
Look in your sensor configuration where the level sensor is defined and ensure:
```c
sensor_config.scale_factor = 0.001;      // Convert mm to meters
sensor_config.sensor_height = 2.228;     // Sensor height in meters
sensor_config.max_water_level = 2.228;   // Tank height in meters
```

### 3. Formula Explanation:
The level calculation formula is:
```
level_filled = ((SENSOR_HEIGHT_M - level_open_m) / TANK_HEIGHT_M) * 100
```

Where:
- SENSOR_HEIGHT_M = 2.228 m (height where sensor is mounted, top of tank)
- level_open_m = sensor reading in meters (raw_value × 0.001)
- TANK_HEIGHT_M = 2.228 m (total tank height)

### 4. Expected Results:
With correct configuration:
- When sensor reads 521mm → level_filled = 76.61%
- When sensor reads 464mm → level_filled = 79.17%
- When tank is empty (2228mm reading) → level_filled = 0%
- When tank is full (0mm reading) → level_filled = 100%

### 5. Verification:
After updating, your telemetry should show:
```json
{"unit_id":"FG23886L","created_on":"2025-10-27T07:52:25Z","type":"LEVEL","level_filled":79.17}
```

## Notes:
- The sensor measures distance from sensor to water surface
- Smaller distance = fuller tank (inverse relationship)
- Ensure all units are consistent (meters or millimeters throughout)