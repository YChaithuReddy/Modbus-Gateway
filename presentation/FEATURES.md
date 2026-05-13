# Complete Feature List — ESP32 IoT Device Enhancement

> All features are verified against the actual codebase (`main/` source + `CLAUDE.md`). Nothing fabricated. Group them however fits your presentation.

---

## 🎯 The Big Four (primary user-facing additions)

These are the four anchors of your presentation — already covered on Slides 3–7.

| # | Feature | What it does |
|---|---|---|
| 1 | **SIM Module** | A7670C 4G LTE cellular connectivity — device uses mobile SIM directly when WiFi fails or isn't available |
| 2 | **SD Card Backup** | FAT32 local storage — stores every data ping during network outages, replays in chronological order when network returns |
| 3 | **RTC Module (DS3231)** | Real-Time Clock — provides accurate date/time continuously, timestamps every stored reading |
| 4 | **Web Server / Configuration Portal** | Browser-based setup — configure device via WiFi hotspot, no Arduino editing or firmware reflash |

---

## 📡 Connectivity Features

- **Dual Network Support** — WiFi (primary) + SIM cellular (fallback)
- **Automatic Failover** — seamless switch between WiFi and cellular
- **WiFi STA + AP mode** — can connect to a router AND host its own hotspot simultaneously
- **PPP Stack for Cellular** — standard IP over cellular for A7670C modem
- **Azure IoT Hub Integration** — industry-standard IoT cloud platform
- **MQTT over TLS 1.2** — encrypted, secure cloud communication
- **Auto-reconnect logic** — exponential backoff on connection failures
- **WiFi + SIM Credentials** — multiple networks stored, configurable via web UI

---

## 💾 Offline Data Handling Features

- **Automatic SD Backup Activation** — detects network drop, starts local caching without user intervention
- **Every Ping Stored Locally** — no sampling, no sacrifice, complete data preservation
- **Chronological Replay** — when network returns, cached data is sent in the exact order it was captured
- **Power-Loss Protection** — `FATFS_IMMEDIATE_FSYNC` ensures writes are flushed to SD card immediately
- **Corruption Detection** — malformed messages auto-detected and removed from the replay queue
- **SD Retry Logic** — 3 automatic retries on SD card I/O errors before fallback
- **RAM Fallback Buffer** — if SD card fails completely, last 3 messages held in RAM
- **Auto-Recovery** — system attempts SD card re-initialization every 60 seconds when failed
- **Graceful Degradation** — no data lost even during SD card transient failures

---

## 🕐 Time & Accuracy Features

- **DS3231 Hardware RTC** — battery-backed, temperature-compensated, accurate to ±2ppm
- **Offline Timestamp Accuracy** — correct time maintained even without network
- **Timestamp Per Reading** — every sensor reading tagged with exact capture time
- **Survives Power Cycles** — RTC retains time across reboots and outages
- **NTP Sync (when online)** — RTC can sync from internet when available
- **ISO 8601 Timestamps** — standard format in cloud payloads

---

## ⚙️ Configuration Features (Web Portal)

- **Browser-Based Setup** — any phone/laptop with a browser can configure the device
- **ESP-Hosted SoftAP** — device creates its own WiFi hotspot for configuration
- **No Code Editing** — zero Arduino IDE interaction required
- **No Firmware Reflash** — configuration changes without rebuilding firmware
- **Sensor Configuration** — add/remove/edit up to 10 sensors via UI
- **WiFi Credentials** — enter home/office WiFi from the browser
- **SIM / APN Settings** — configure cellular carrier details via UI
- **Azure IoT Hub Connection** — cloud endpoint setup via browser
- **Modbus Settings** — baud rate, retries, timeout — all UI-configurable
- **Live Test Utility** — test-read individual sensors from the web UI

---

## 🔌 Sensor Support Features

- **RS485 Modbus RTU** — industrial-standard sensor bus
- **Up to 10 Sensors Per Gateway** — multiple devices daisy-chained
- **Multiple Data Types** — UINT16, INT16, FLOAT32 all supported
- **All Byte Order Variants** — ABCD, CDAB, DCBA Modbus conventions handled
- **Per-Sensor Scale Factor** — raw register values converted to engineering units
- **Function Code 0x03 + 0x04** — holding registers + input registers
- **Validated Sensors** — Flow meters, Aquadax water quality, Opruss Ace, Opruss TDS
- **CRC Validation** — Modbus CRC-16 checked on every read
- **Configurable Slave ID** — per-sensor Modbus address
- **Configurable Poll Rate** — how often each sensor is queried

---

## ☁️ Cloud / Remote Operations (Azure Device Twin)

These are the remote controls available from the cloud:

- **Remote Sensor Configuration** — change sensor settings without visiting the site
- **Remote Telemetry Interval** — adjust how often data is sent from the cloud
- **Remote OTA Updates** — push firmware updates via `ota_url` property
- **Remote Web Server Toggle** — start/stop the config portal from the cloud
- **Remote Maintenance Mode** — pause telemetry for planned maintenance
- **Remote Device Reboot** — restart the device from the cloud
- **Remote Modbus Settings** — adjust retry count and delay remotely
- **Device Twin State Sync** — current device properties visible in Azure portal

---

## 🔁 Reliability & Self-Healing Features

- **Auto Modem Reset** — if MQTT disconnects, modem is power-cycled automatically
- **5-Minute Cooldown Logic** — prevents reset thrashing when network is unstable
- **Telemetry Timeout Detection** — system detects if data stops flowing
- **Smart Restart Logic** — skips restart if SD card is caching (no data loss)
- **Heap Monitoring** — warnings at 30KB free, critical at 20KB, emergency at 10KB
- **Watchdog Protection** — hardware watchdog prevents frozen-firmware lockups
- **Exponential Backoff** — MQTT reconnection spaces attempts intelligently
- **Max Reconnect Attempts** — 10 attempts before giving up to prevent tight loops

---

## 🔒 Security Features

- **TLS 1.2 Encryption** — all MQTT traffic encrypted to Azure
- **Device Authentication** — Azure IoT Hub device keys
- **Signed OTA Firmware** — firmware signature verified before flash
- **Dual-Slot OTA** — safe rollback to previous firmware if new version fails to boot
- **Secure Boot** — bootloader verifies firmware before execution
- **NVS Encrypted Storage** — sensitive config stored in encrypted partition

---

## 🎛️ Deployment & Operations Features

- **OTA (Over-The-Air) Updates** — firmware pushed from GitHub releases or Azure
- **Web-Based Commissioning** — deploy a site in minutes, not hours
- **LED Status Indicators** — visual feedback for MQTT, WiFi, Sensor activity
- **Config Button** — physical button to trigger web server for field access
- **Boot Button** — reset and recovery trigger
- **Multiple Build Targets** — dev, staging, production configurations
- **Automatic Version Reporting** — firmware version reported to Azure Device Twin

---

## 🔧 Hardware Integration Features

- **ESP32-WROOM-32 Platform** — dual-core Xtensa LX6, WiFi + Bluetooth capable
- **4MB Flash Partition Map** — NVS, OTA_0, OTA_1, factory slots
- **Multi-GPIO Peripherals** — RS485, SIM, SD, RTC, LEDs, buttons
- **I2C Bus** — for DS3231 RTC
- **SPI Bus** — for SD card
- **UART2** — for RS485 Modbus
- **UART1** — for SIM module AT commands
- **FreeRTOS** — real-time task scheduling
- **ESP-IDF v5.5.1** — latest stable framework

---

## 📊 Data Pipeline Features

- **Structured JSON Payloads** — standardized cloud format
- **Batch Telemetry** — multiple readings sent in one message
- **Device Twin Property Reporting** — device health visible in cloud
- **Configurable Telemetry Topic** — customizable MQTT topic per deployment
- **Cloud-to-Device Messages** — Azure can send commands to device
- **Telemetry History Buffer** — recent messages kept in memory for diagnostics

---

## 🛠️ Developer / Maintenance Features (behind-the-scenes)

You may not mention these explicitly, but they're there:

- **GPIO Conflict Detection** — compile-time check for pin reuse
- **Sensor Type Abstraction** — modular sensor driver architecture
- **Byte-Order Decoder** — handles all 4 Modbus byte conventions
- **Mutex-Protected SD Operations** — thread-safe concurrent access
- **Task Stack Monitoring** — prevents stack overflows
- **Heap Fragmentation Tracking** — monitors memory health
- **Exception Decoder Integration** — crash dump analysis

---

## 📈 Quick-Reference Summary (for any slide you need)

### If you need a 3-bullet version:
1. **Dual connectivity** — WiFi + SIM with auto-failover
2. **Offline resilience** — SD card + RTC for zero data loss
3. **Zero-code deployment** — web portal replaces Arduino reflashing

### If you need a 5-bullet version:
1. **SIM Module** — cellular connectivity
2. **SD Card Backup** — automatic offline storage
3. **RTC Module** — accurate timestamps
4. **Web Server** — browser-based configuration
5. **Remote OTA** — cloud firmware updates

### If you need an 8-bullet version:
1. SIM Module (cellular)
2. SD Card Backup (offline cache)
3. RTC Module (timestamps)
4. Web Server (configuration)
5. Remote OTA (cloud updates)
6. Device Twin (remote control)
7. Auto Self-Healing (modem reset)
8. TLS 1.2 Security

### If you need a one-line product tagline:
> "A WiFi-dependent ESP32 device transformed into a smart hybrid IoT solution — dual network, offline backup, accurate timestamps, zero-code deployment."

---

## 🎨 Icon suggestions for features (for any slide you add)

| Feature | Icon suggestion |
|---|---|
| SIM Module | Signal bars / antenna |
| SD Card Backup | SD card with arrow |
| RTC Module | Clock / hourglass |
| Web Server | Browser window / globe |
| Dual Network | Two overlapping circles |
| Offline Replay | Rewind / loop arrow |
| Remote OTA | Cloud with down-arrow |
| Device Twin | Two linked circles |
| Auto Self-Healing | Circular refresh / heart |
| TLS Security | Shield with check |
| LED Indicators | Lightbulb / dot |
| Web Commissioning | Wrench or settings gear |

---

## 💡 What I recommend you add or emphasize more

If you have room for 1-2 extra slides or a section within an existing slide, these are the highest-impact features not currently highlighted in the 9-slide deck:

1. **Azure Device Twin remote controls** — this is a genuinely impressive capability. You can change sensor config, reboot, toggle web server, all from the cloud. Consider adding a mini-slide or callout on Slide 7 or Slide 8.

2. **SD Card resilience** — the 3-retry + RAM fallback + 60s recovery is sophisticated engineering. A subtle mention on Slide 5 would add depth.

3. **Dual-slot OTA with rollback** — this is the "grown-up" OTA pattern. If a new firmware fails, device auto-rolls back. Worth mentioning if security/reliability is a topic in Q&A.

4. **Modbus byte order flexibility** — supporting ABCD/CDAB/DCBA is a big deal in industrial IoT because sensor vendors use different conventions. Your gateway works with any of them.
