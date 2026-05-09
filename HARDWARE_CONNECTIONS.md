# Hardware Connections Guide
## ESP32 Modbus IoT Gateway

This document provides pin connections and wiring diagrams for all hardware components.

---

## Table of Contents
1. [ESP32 Pin Overview](#esp32-pin-overview)
2. [A7670C SIM Module](#a7670c-sim-module)
3. [SD Card Module](#sd-card-module)
4. [DS3231 RTC Module](#ds3231-rtc-module)
5. [RS485 Modbus Module](#rs485-modbus-module)
6. [Status LEDs](#status-leds)
7. [Control Buttons](#control-buttons)
8. [Complete Wiring Diagram](#complete-wiring-diagram)

---

## ESP32 Pin Overview

```
                    ┌─────────────────────┐
                    │      ESP32 DevKit   │
                    │                     │
              3.3V ─┤ 3V3           VIN  ├─ 5V
               GND ─┤ GND           GND  ├─ GND
   (Config Btn) 34 ─┤ GPIO34      GPIO23 ├─ 23 (SD MOSI)
                35 ─┤ GPIO35      GPIO22 ├─ 22 (RTC SCL)
   (SIM TX)     32 ─┤ GPIO32      GPIO21 ├─ 21 (RTC SDA)
   (SIM RX)     33 ─┤ GPIO33      GPIO19 ├─ 19 (SD MISO)
   (Sensor LED) 27 ─┤ GPIO27      GPIO18 ├─ 18 (RS485 RTS/DE)
   (MQTT LED)   26 ─┤ GPIO26       GPIO5 ├─ 5  (SD CLK)
   (Web LED)    25 ─┤ GPIO25      GPIO17 ├─ 17 (RS485 TX)
                    │             GPIO16 ├─ 16 (RS485 RX)
   (SIM PWR)     4 ─┤ GPIO4       GPIO15 ├─ 15 (SD CS)
   (Modem RST)   2 ─┤ GPIO2        GPIO0 ├─ 0  (Boot Btn)
                    │                     │
                    └─────────────────────┘
```

---

## A7670C SIM Module

### Pin Connections

| ESP32 Pin | A7670C Pin | Description |
|-----------|------------|-------------|
| GPIO 33   | TXD        | ESP32 TX → A7670C RX (UART1 TX) |
| GPIO 32   | RXD        | ESP32 RX ← A7670C TX (UART1 RX) |
| GPIO 4    | PWRKEY     | Power control |
| ~~GPIO 15~~ | RESET   | Hardware reset (DISABLED by default - conflicts with SD Card CS) |
| GND       | GND        | Common ground |
| 5V (VIN)  | VCC        | Power supply (5V recommended) |

> **Note**: SIM Reset pin is disabled by default (`reset_pin = -1`) because GPIO 15 is
> used by the SD Card CS. If SD card is not used, SIM Reset can be enabled via the web UI.
> The modem hardware reset on GPIO 2 provides an alternative reset mechanism.

### Wiring Diagram

```
    ┌──────────────┐                    ┌──────────────┐
    │    ESP32     │                    │   A7670C     │
    │              │                    │              │
    │      GPIO33 ─┼──────────────────►─┼─ RXD        │
    │              │   (ESP32 TX)       │              │
    │      GPIO32 ─┼─◄──────────────────┼─ TXD        │
    │              │   (ESP32 RX)       │              │
    │       GPIO4 ─┼──────────────────►─┼─ PWRKEY     │
    │              │                    │              │
    │      GPIO15 ─┼─ ─ ─ ─ ─ ─ ─ ─ ►─┼─ RESET *    │
    │              │                    │              │
    │         GND ─┼──────────────────►─┼─ GND        │
    │              │                    │              │
    │         VIN ─┼──────────────────►─┼─ VCC (5V)   │
    │              │                    │              │
    └──────────────┘                    │   ┌─────┐   │
                                        │   │ SIM │   │
                                        │   │CARD │   │
                                        │   └─────┘   │
                                        │    ═══      │
                                        │   ANTENNA   │
                                        └──────────────┘
```

### Important Notes
- **Power**: A7670C requires stable 5V power supply (2A recommended)
- **Antenna**: Always connect LTE antenna for proper signal
- **SIM Card**: Insert SIM card before powering on
- **Level Shifting**: Not required (A7670C is 3.3V compatible)
- **UART**: Uses UART1 (configurable via web UI between UART1/UART2)
- **Baud Rate**: Default 115200 (configurable: 9600, 19200, 38400, 57600, 115200)
- **Reset Pin**: Disabled by default (GPIO 15 conflict with SD Card CS)
- **Modem Reset**: GPIO 2 provides hardware modem reset (configurable via web UI)

---

## SD Card Module

### Pin Connections (SPI Mode)

| ESP32 Pin | SD Card Pin | Description |
|-----------|-------------|-------------|
| GPIO 23   | MOSI / DI   | Master Out, Slave In |
| GPIO 19   | MISO / DO   | Master In, Slave Out |
| GPIO 5    | CLK / SCK   | SPI Clock |
| GPIO 15   | CS / SS     | Chip Select |
| 3.3V      | VCC         | Power (3.3V) |
| GND       | GND         | Common ground |

### Wiring Diagram

```
    ┌──────────────┐                    ┌──────────────┐
    │    ESP32     │                    │  SD Card     │
    │              │                    │  Module      │
    │      GPIO23 ─┼──────────────────►─┼─ MOSI (DI)  │
    │              │                    │              │
    │      GPIO19 ─┼─◄──────────────────┼─ MISO (DO)  │
    │              │                    │              │
    │       GPIO5 ─┼──────────────────►─┼─ CLK (SCK)  │
    │              │                    │              │
    │      GPIO15 ─┼──────────────────►─┼─ CS (SS)    │
    │              │                    │              │
    │        3.3V ─┼──────────────────►─┼─ VCC        │
    │              │                    │              │
    │         GND ─┼──────────────────►─┼─ GND        │
    │              │                    │  ┌────────┐ │
    └──────────────┘                    │  │ SD     │ │
                                        │  │ CARD   │ │
                                        │  └────────┘ │
                                        └──────────────┘
```

### Important Notes
- **Voltage**: Use 3.3V (NOT 5V) or use module with level shifter
- **Format**: SD card must be formatted as FAT32
- **Size**: Recommended 4GB - 32GB
- **Speed**: Class 4 or higher recommended

---

## DS3231 RTC Module

### Pin Connections (I2C)

| ESP32 Pin | DS3231 Pin | Description |
|-----------|------------|-------------|
| GPIO 21   | SDA        | I2C Data |
| GPIO 22   | SCL        | I2C Clock |
| 3.3V      | VCC        | Power (3.3V) |
| GND       | GND        | Common ground |

### Wiring Diagram

```
    ┌──────────────┐                    ┌──────────────┐
    │    ESP32     │                    │   DS3231     │
    │              │                    │   RTC        │
    │      GPIO21 ─┼──────────────────►─┼─ SDA        │
    │              │                    │              │
    │      GPIO22 ─┼──────────────────►─┼─ SCL        │
    │              │                    │              │
    │        3.3V ─┼──────────────────►─┼─ VCC        │
    │              │                    │              │
    │         GND ─┼──────────────────►─┼─ GND        │
    │              │                    │   ┌─────┐   │
    └──────────────┘                    │   │CR2032   │
                                        │   │Battery│ │
                                        │   └─────┘   │
                                        └──────────────┘
```

### Important Notes
- **Battery**: Install CR2032 battery for backup
- **I2C Address**: 0x68 (fixed)
- **Pull-ups**: Most modules have built-in pull-up resistors

---

## RS485 Modbus Module

### Pin Connections

| ESP32 Pin | RS485 Module | Description |
|-----------|--------------|-------------|
| GPIO 17   | DI (TX)      | Data In (ESP32 TX) |
| GPIO 16   | RO (RX)      | Read Out (ESP32 RX) |
| GPIO 18   | DE + RE      | Direction Enable (tied together) |
| 3.3V      | VCC          | Power (3.3V or 5V) |
| GND       | GND          | Common ground |

### Wiring Diagram

```
    ┌──────────────┐                    ┌──────────────┐
    │    ESP32     │                    │   MAX485     │
    │              │                    │   RS485      │
    │      GPIO17 ─┼──────────────────►─┼─ DI         │
    │              │                    │              │
    │      GPIO16 ─┼─◄──────────────────┼─ RO         │
    │              │                    │              │
    │      GPIO18 ─┼──────────────────►─┼─ DE ─┐      │
    │              │                    │      ├──┐   │
    │              │                    │ RE ──┘  │   │
    │              │                    │         │   │
    │        3.3V ─┼──────────────────►─┼─ VCC    │   │    ┌─────────────┐
    │              │                    │         │   │    │   MODBUS    │
    │         GND ─┼──────────────────►─┼─ GND    │   │    │   SENSOR    │
    │              │                    │         │   │    │             │
    └──────────────┘                    │    A ───┼───┼────┼─ A+        │
                                        │         │   │    │             │
                                        │    B ───┼───┼────┼─ B-        │
                                        │         │   │    │             │
                                        └─────────┘   │    │   GND ──────┤
                                                      │    └─────────────┘
                                           (Tie DE & RE together)
```

### Important Notes
- **DE and RE**: Must be tied together and connected to GPIO 18
- **RTS Pin History**: Changed from GPIO 32 to GPIO 18 to avoid conflict with SIM RX pin
- **Termination**: Add 120Ω resistor at end of bus if cable > 10m
- **Baud Rate**: Default 9600 (configurable per sensor via web UI)
- **Twisted Pair**: Use twisted pair cable for A and B lines
- **Max Sensors**: 10 per gateway (reduced from 15 in v1.3.8 for heap savings)
- **Function Codes**: Supports 0x03 (Holding Registers) and 0x04 (Input Registers)

### Supported Sensor Types
| Sensor Type | Data Format | Registers | Byte Order |
|-------------|------------|-----------|------------|
| Flow Meters | UINT16/32, INT16/32, FLOAT32 | 1-4 | ABCD/DCBA/BADC/CDAB |
| Aquadax Quality | FLOAT32 (COD, BOD, TSS, pH, Temp) | 12 from addr 1280 | CDAB + byte-swap |
| Opruss Ace Quality | FLOAT32 (COD, BOD, TSS) | 22 from addr 0 | CDAB (word-swap) |
| Opruss Ace TDS | FLOAT32 (TDS, Temp) | 2+2, addr 0x0016/0x0020 | DCBA (byte+word swap) |
| Generic Modbus | Configurable | Configurable | All variants |

---

## Status LEDs

### Pin Connections

| ESP32 Pin | LED Color | Function |
|-----------|-----------|----------|
| GPIO 25   | Green     | Web Server Active |
| GPIO 26   | Blue      | MQTT Connected |
| GPIO 27   | Yellow    | Sensor Activity |

### Wiring Diagram

```
    ┌──────────────┐
    │    ESP32     │
    │              │         ┌─────┐
    │      GPIO25 ─┼────────►│ LED │ (Green - Web Server)
    │              │    330Ω └──┬──┘
    │              │            │
    │              │         ┌─────┐
    │      GPIO26 ─┼────────►│ LED │ (Blue - MQTT)
    │              │    330Ω └──┬──┘
    │              │            │
    │              │         ┌─────┐
    │      GPIO27 ─┼────────►│ LED │ (Yellow - Sensor)
    │              │    330Ω └──┬──┘
    │              │            │
    │         GND ─┼────────────┴─────────────► GND
    │              │
    └──────────────┘
```

### LED Behavior
| LED | OFF | ON | Blinking |
|-----|-----|-----|----------|
| Web Server (Green) | Web server stopped | Web server running | - |
| MQTT (Blue) | Disconnected | Connected to Azure | - |
| Sensor (Yellow) | No response | Sensor responding | Reading data |

---

## Control Buttons

### Pin Connections

| ESP32 Pin | Function | Trigger |
|-----------|----------|---------|
| GPIO 34   | Config Mode | Rising edge (external pull-down) |
| GPIO 0    | Boot/Toggle | Falling edge (built-in) |

### Wiring Diagram

```
    ┌──────────────┐
    │    ESP32     │
    │              │
    │      GPIO34 ─┼─────────┬─────────► Config Button
    │              │         │           (Normally Open)
    │              │        ─┴─ 10kΩ
    │              │         │
    │              │        GND          (Pull-down)
    │              │
    │       GPIO0 ─┼──── Built-in BOOT button
    │              │
    └──────────────┘
```

### Button Functions
- **GPIO 34 (Config Button)**: Press to enter AP configuration mode
- **GPIO 0 (Boot Button)**: Press to toggle web server on/off

---

## Complete Wiring Diagram

```
                                    ┌─────────────────────────────────────────────────┐
                                    │                    ESP32                         │
                                    │                                                  │
    ┌─────────────┐                 │  3.3V ────────────┬───────────┬────────────────  │
    │   DS3231    │                 │                   │           │                  │
    │    RTC      │◄────────────────┤  GPIO21 (SDA)     │           │                  │
    │             │◄────────────────┤  GPIO22 (SCL)     │           │                  │
    │   VCC ──────┼─────────────────┤                   │           │                  │
    │   GND ──────┼─────────────────┤  GND ─────────────┼───────────┼───────────────   │
    └─────────────┘                 │                   │           │                  │
                                    │                   │           │                  │
    ┌─────────────┐                 │                   │           │                  │
    │  SD Card    │                 │                   │           │                  │
    │  Module     │◄────────────────┤  GPIO23 (MOSI)    │           │                  │
    │             │────────────────►┤  GPIO19 (MISO)    │           │                  │
    │             │◄────────────────┤  GPIO5  (CLK)     │           │                  │
    │             │◄────────────────┤  GPIO15 (CS)      │           │                  │
    │   VCC ──────┼─────────────────┤                   │           │                  │
    │   GND ──────┼─────────────────┤                   │           │                  │
    └─────────────┘                 │                   │           │                  │
                                    │                   │           │                  │
    ┌─────────────┐                 │                   │           │                  │
    │   A7670C    │                 │                   │           │                  │
    │    SIM      │────────────────►┤  GPIO33 (RX)      │           │                  │
    │   Module    │◄────────────────┤  GPIO32 (TX)      │           │                  │
    │             │◄────────────────┤  GPIO4  (PWR)     │           │                  │
    │             │◄────────────────┤  GPIO15 (RST)*    │           │                  │
    │   VCC ──────┼─────────────────┤  5V (VIN)         │           │                  │
    │   GND ──────┼─────────────────┤                   │           │                  │
    └─────────────┘                 │                   │           │                  │
                                    │                   │           │                  │
    ┌─────────────┐                 │                   │           │                  │
    │   MAX485    │                 │                   │           │                  │
    │   RS485     │◄────────────────┤  GPIO17 (TX)      │           │                  │
    │             │────────────────►┤  GPIO16 (RX)      │           │                  │
    │   DE+RE ────┼◄────────────────┤  GPIO18 (RTS)     │           │                  │
    │   VCC ──────┼─────────────────┤                   │           │                  │
    │   GND ──────┼─────────────────┤                   │           │                  │
    │    A ───────┼─────────────────┼──────────────────►│ Modbus Sensor A+            │
    │    B ───────┼─────────────────┼──────────────────►│ Modbus Sensor B-            │
    └─────────────┘                 │                   │           │                  │
                                    │                   │           │                  │
    ┌─────────────┐                 │                   │           │                  │
    │   LEDs      │                 │                   │           │                  │
    │  (Green)────┼◄────────────────┤  GPIO25 (Web)     │           │                  │
    │  (Blue) ────┼◄────────────────┤  GPIO26 (MQTT)    │           │                  │
    │  (Yellow)───┼◄────────────────┤  GPIO27 (Sensor)  │           │                  │
    │   GND ──────┼─────────────────┤                   │           │                  │
    └─────────────┘                 │                   │           │                  │
                                    │                   │           │                  │
    ┌─────────────┐                 │                   │           │                  │
    │  Buttons    │                 │                   │           │                  │
    │  (Config)───┼────────────────►┤  GPIO34           │           │                  │
    │  (Boot) ────┼────────────────►┤  GPIO0            │           │                  │
    └─────────────┘                 │                   │           │                  │
                                    └───────────────────┴───────────┴──────────────────┘

    * Note: GPIO15 is shared between SD Card CS and SIM Reset
            Configure in web portal which module uses this pin
```

---

## Pin Summary Table

| GPIO | Function | Module | Direction | UART/Bus |
|------|----------|--------|-----------|----------|
| 0 | Boot Button (web server toggle) | ESP32 | Input | - |
| 2 | Modem Hardware Reset | A7670C | Output | - |
| 4 | SIM Power Key (PWRKEY) | A7670C | Output | - |
| 5 | SD Card CLK (SCK) | SD Card | Output | SPI3 |
| 15 | SD Card CS (shared w/ SIM Reset*) | SD/SIM | Output | SPI3 |
| 16 | RS485 RX (RXD2) | MAX485 | Input | UART2 |
| 17 | RS485 TX (TXD2) | MAX485 | Output | UART2 |
| 18 | RS485 RTS/DE/RE (Direction) | MAX485 | Output | UART2 |
| 19 | SD Card MISO (DO) | SD Card | Input | SPI3 |
| 21 | RTC SDA | DS3231 | Bidirectional | I2C0 |
| 22 | RTC SCL | DS3231 | Output | I2C0 |
| 23 | SD Card MOSI (DI) | SD Card | Output | SPI3 |
| 25 | Web Server Active LED | LED | Output | - |
| 26 | MQTT Connection LED | LED | Output | - |
| 27 | Sensor Response LED | LED | Output | - |
| 32 | SIM Module RX (ESP32 RX ← A7670C TX) | A7670C | Input | UART1 |
| 33 | SIM Module TX (ESP32 TX → A7670C RX) | A7670C | Output | UART1 |
| 34 | Config Button (AP mode trigger) | Button | Input | - |

> \* GPIO 15 conflict: SD Card CS has priority. SIM Reset is disabled by default (`reset_pin = -1`).
> SIM module uses GPIO 2 modem reset as the primary hardware reset mechanism.

---

## Power Requirements

| Component | Voltage | Current (Typical) | Current (Peak) |
|-----------|---------|-------------------|----------------|
| ESP32 | 3.3V / 5V | 80mA | 500mA |
| A7670C SIM | 5V | 100mA | 2A |
| SD Card | 3.3V | 50mA | 100mA |
| DS3231 RTC | 3.3V | 1mA | 3mA |
| MAX485 RS485 | 3.3V/5V | 5mA | 10mA |
| LEDs (x3) | 3.3V | 30mA | 60mA |
| **TOTAL** | **5V** | **~270mA** | **~2.7A** |

### Power Supply Recommendation
- **Minimum**: 5V 2A power adapter
- **Recommended**: 5V 3A power adapter (for stable operation)
- **USB**: Not recommended (insufficient current for SIM module)

---

## Troubleshooting

### SIM Module Not Responding
1. Check 5V power supply (must be 2A+)
2. Verify TX/RX connections: GPIO 33 → A7670C RXD, GPIO 32 ← A7670C TXD
3. Ensure SIM card is inserted properly
4. Check antenna connection
5. Try modem reset via web UI or GPIO 2 power cycle
6. Check serial monitor for AT command responses

### SD Card Not Detected
1. Verify card is FAT32 formatted
2. Check SPI connections (MOSI, MISO, CLK, CS)
3. Try different SD card
4. Ensure 3.3V power (not 5V)

### RTC Time Wrong
1. Check I2C connections (SDA, SCL)
2. Verify CR2032 battery is installed
3. Enable NTP sync in configuration

### RS485 No Response
1. Check A/B wiring to sensor
2. Verify DE/RE tied together and connected to GPIO 18
3. Check baud rate matches sensor (default 9600)
4. Try swapping A and B lines
5. Verify slave ID matches sensor configuration
6. Check function code: 0x03 for holding registers, 0x04 for input registers
7. Use "Test RS485" in web UI for live diagnostics

---

## References
- [ESP32 Pinout Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html)
- [A7670C Datasheet](https://www.simcom.com/product/A7670X.html)
- [DS3231 Datasheet](https://www.maximintegrated.com/en/products/analog/real-time-clocks/DS3231.html)
- [MAX485 Datasheet](https://www.maximintegrated.com/en/products/interface/transceivers/MAX485.html)

---

*Last Updated: February 2026*
*Firmware Version: 1.3.8*
*Partition Table: FROZEN as of v1.3.7 - DO NOT modify partitions_4mb.csv*
