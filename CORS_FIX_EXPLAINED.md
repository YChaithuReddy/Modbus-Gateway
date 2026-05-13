# 🔒 CORS Error - Explanation & Fix

## What is CORS?

**CORS** = Cross-Origin Resource Sharing

It's a browser security feature that blocks web pages from making requests to a different domain than the one that served the web page.

---

## Why Did You Get This Error?

```
Access to fetch at 'http://192.168.4.1/reset_sensors' from origin 'null'
has been blocked by CORS policy
```

### The Problem:
1. You opened `reset_sensors.html` **locally** (from your file system)
   - Origin: `file://` (or `null`)

2. The JavaScript tried to fetch from `http://192.168.4.1`
   - Different origin!

3. Browser blocked the request for security

---

## How We Fixed It

### 1. Added CORS Headers to ESP32 Server

**File**: `main/web_config.c`

```c
// In reset_sensors_handler() and reboot_handler()
httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
```

This tells the browser: "It's OK to accept requests from any origin"

### 2. Handle OPTIONS Preflight Requests

Browsers send a "preflight" OPTIONS request before POST to check permissions:

```c
// Handle OPTIONS preflight request
if (req->method == HTTP_OPTIONS) {
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
```

### 3. Registered OPTIONS Endpoints

```c
// Register OPTIONS handler for CORS preflight
httpd_uri_t reset_sensors_options_uri = {
    .uri = "/reset_sensors",
    .method = HTTP_OPTIONS,
    .handler = reset_sensors_handler,
    .user_ctx = NULL
};
httpd_register_uri_handler(g_server, &reset_sensors_options_uri);
```

---

## Alternative Solutions (No CORS Issues)

### ✅ Option 1: Use Command-Line Script
No browser = No CORS!

**Windows:**
```cmd
reset_sensors.bat
```

**Linux/Mac:**
```bash
./reset_sensors.sh
```

### ✅ Option 2: Direct cURL
```bash
curl -X POST http://192.168.4.1/reset_sensors
curl -X POST http://192.168.4.1/reboot
```

### ✅ Option 3: Open HTML from ESP32 Server
Instead of opening `reset_sensors.html` locally, serve it from the ESP32:
1. Copy `reset_sensors.html` to ESP32's file system
2. Access via `http://192.168.4.1/reset_sensors.html`
3. Same origin = No CORS!

---

## Testing the Fix

### Before Fix:
```
❌ CORS error in browser console
❌ Request blocked
❌ Network tab shows "CORS error"
```

### After Fix:
```
✅ OPTIONS request returns 200 OK
✅ POST request returns 200 OK
✅ Response received successfully
```

---

## Understanding CORS in Your Browser

### 1. Open Developer Tools (F12)

### 2. Network Tab
Look for:
- **OPTIONS request** (preflight) → Should return 200
- **POST request** (actual request) → Should return 200

### 3. Headers Tab
Should see:
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: POST, GET, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

### 4. Console Tab
- ✅ No CORS errors
- ✅ Response logged successfully

---

## Security Note

### Why `Access-Control-Allow-Origin: *`?

For **local development/testing** on IoT devices, this is fine because:
- Device is on local network (not public internet)
- Web interface is for configuration only
- Protected by WiFi password

### For Production Web Apps:
Replace `*` with specific domain:
```c
httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "https://yourdomain.com");
```

---

## Summary

**Problem**: Browser blocked cross-origin request from local file

**Solution**: ESP32 now sends CORS headers allowing cross-origin requests

**Result**: `reset_sensors.html` works from local file system! 🎉

**Alternative**: Use command-line scripts (no CORS issues at all)
