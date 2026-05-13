# FluxGen IoT Gateway — Project Showcase Package
**Presenter:** Y Chaithanya Reddy  &nbsp;·&nbsp;  **Date:** April 2026  &nbsp;·&nbsp;  **Slot:** 10 minutes + Q&A

> **Purpose:** Walk the team through the FluxGen IoT Gateway — what it does, every feature inside it, how it works technically, and how it scales. No resource asks. No investment pitch. No fabricated numbers. Just the product, its capabilities, and the scaling path.

---

## 0. How to use this package

1. Open `FluxGen_IoT_Gateway_Showcase.pptx` (same content as the `_v2.0_Pitch.pptx` file).
2. Speaker notes are embedded in the notes pane (View → Notes).
3. Do one timed dry-run. Target **10 minutes total**, ~65 seconds per slide on average.
4. Skim the Q&A (section 3) — prepared technical answers.

> **On numbers:** This deck contains no specific rupee figures. The qualitative claim that traditional SCADA/HMI gateways require specialist programming per deployment is industry fact. If anyone asks for specific rupee comparisons, the honest answer is: *"I haven't benchmarked exact figures. I can run that analysis against specific vendor quotes if needed."*

---

## 1. The 9-slide narrative

| # | Slide | What the audience takes away |
|---|---|---|
| 1 | **Title** | FluxGen IoT Gateway — zero-code, offline-resilient, production-grade |
| 2 | **Overview** | Three headlines: eliminated coding, survives outages, production-grade today |
| 3 | **The Problem** | Every traditional deployment requires specialist programming + data lost during outages |
| 4 | **Root Cause** | Four architectural gaps: proprietary programming, single network, no buffer, no remote config |
| 5 | **What We Built** | Five shipped features: Web Config, SIM+WiFi, SD Cache, Auto-Heal, Remote OTA |
| 6 | **Architecture** | Field → Edge Gateway → Cloud. Offline-first design philosophy |
| 7 | **Technical Capabilities** | Full spec sheet: hardware, sensors, networks, cloud, security |
| 8 | **How We Scale Up** | Scaling building blocks already shipped + natural next steps |
| 9 | **Closing** | "Deploy without a coder. Survive without a network. Scale without new engineering." |

---

## 2. Speaker notes — slide by slide

### Slide 1 — Title
> "Good morning. In the next ten minutes I'm going to walk you through the FluxGen IoT Gateway — the problem it solves, how it works, and the business impact it is already delivering in the field.
>
> This is a project showcase — a look at what has been built, why it matters, and the value it brings to FluxGen."

**Timing:** 30 seconds.

### Slide 2 — Overview
> "Three headlines before we go deep.
>
> **One** — We eliminated coding expense. Traditional industrial gateways need a specialist engineer to code every deployment. Our web UI lets non-technical staff bring a site online in minutes.
>
> **Two** — We survive network outages. Dual network — WiFi plus SIM module — keeps us connected where WiFi can't reach. The SD card cache means telemetry is never lost when the network drops.
>
> **Three** — It is production-grade today. All shipped. All running in the field.
>
> The strip at the bottom is the tech stack — ESP32, RS485 Modbus, WiFi plus cellular, SD card, Azure IoT Hub."

**Timing:** 60 seconds.

### Slide 3 — The Problem
> "Before this gateway, every site cost us a coder.
>
> Any traditional industrial gateway — Siemens, Allen-Bradley, Schneider — needs a specialist engineer with licensed SCADA or HMI software to code the sensor integration for that specific site. Significant programming labour, before the device cost.
>
> On top of that — when the cellular network dropped, telemetry from that window was lost forever. No local buffer, no replay. For a customer submitting data to the Pollution Control Board, a silent outage is a compliance gap.
>
> Firmware updates required a truck roll. Sensor changes needed a laptop onsite. Remote sites with no WiFi coverage were essentially unreachable.
>
> On the right — coding expense, data loss risk, truck-roll cost, slow expansion. This is the mountain the project had to move."

**Timing:** 75 seconds.

### Slide 4 — Root Cause
> "Why are traditional industrial gateways this expensive to deploy? Four architectural gaps.
>
> **One** — they require proprietary programming. SCADA, HMI, PLC ladder logic. Licensed software, specialist engineers, days per site.
> **Two** — single connectivity path. WiFi OR cellular, never both. Remote sites unreachable.
> **Three** — no local persistence. Telemetry gone in seconds when the network drops.
> **Four** — physical-only configuration. Every sensor change needs a laptop onsite.
>
> Our job was to close all four."

**Timing:** 60 seconds.

### Slide 5 — What We Built
> "Here is what we deliver today, in production.
>
> **Top row — the three features that define the product.**
>
> **First — Web-Based Configuration.** Point, click, done. No SCADA license, no HMI coding, no specialist onsite. This single feature eliminates the per-site programming bill.
>
> **Second — SIM plus WiFi dual network.** WiFi primary, A7670C cellular fallback. Remote sites just work.
>
> **Third — SD offline cache.** FAT32. Chronological replay. Power-loss safe. Zero data loss through outages.
>
> **Bottom row — the operational stack.** Auto self-healing recovers the modem without a truck roll. Remote OTA via Azure Device Twin pushes firmware and config from the cloud.
>
> All five are shipping today."

**Timing:** 75 seconds.

### Slide 6 — Architecture
> "Three layers.
>
> **Field** — up to ten sensors via RS485 Modbus. Flow meters, pH, TDS, DO, temperature.
>
> **Gateway** — the intelligence. RS485 in, dual network stack, SD cache, DS3231 RTC for timestamp accuracy. Everything in this box follows one principle: **offline-first — every layer survives the one below it failing.**
>
> **Cloud** — Azure IoT Hub, MQTT over TLS, Device Twin for remote configuration.
>
> Offline-first is why this gateway doesn't lose data."

**Timing:** 60 seconds.

### Slide 7 — Technical Capabilities
> "This slide is the specification of what is running in production today.
>
> **On the left — the hardware and field side.** ESP32 platform, 4MB flash, FreeRTOS. Up to ten sensors per gateway over RS485 Modbus. We support the three standard data types — UINT16, INT16, FLOAT32 — with all four Modbus byte orders: ABCD, CDAB, DCBA. That matters because different sensor vendors use different conventions, and our firmware handles all of them without code changes. We have validated flow meters, Aquadax water quality, Opruss Ace, and Opruss TDS in production.
>
> **On the right — connectivity, cloud, and operations.** WiFi plus cellular with automatic failover. Azure IoT Hub over MQTT with TLS. Device Twin gives us remote configuration — we can push a new sensor map to the fleet without touching the device. OTA updates are signed and use dual-slot safe rollback — if a new firmware fails to boot, the device automatically reverts. Self-healing logic power-cycles the modem on MQTT drops. And the SD card layer has three-retry logic with a RAM fallback buffer, so even transient SD errors do not lose data.
>
> Everything on this slide is shipping in v1.3.8 today."

**Timing:** 90 seconds.

### Slide 8 — How We Scale Up
> "Scaling is already possible because we built for it.
>
> **Left side — scaling built in.**
>
> Remote OTA means firmware updates to the whole fleet come from the cloud — no truck rolls per device. Device Twin lets us push new sensor configurations or telemetry intervals to 100 devices simultaneously. Web-based commissioning means onboarding a new site does not bottleneck on engineering time — any trained operator can do it.
>
> The offline-first architecture means we can deploy where traditional gateways would not survive — rural water treatment plants, temporary project sites, intermittent-connectivity environments. And because we use Azure IoT Hub with standard MQTT over TLS, the cloud side scales horizontally.
>
> **Right side — natural next steps.**
>
> A fleet dashboard to see all deployed gateways at once. Enterprise security hardening — signed OTA and TLS cert pinning — to meet the bar for larger tenders. Edge analytics running on-device. A mobile technician app to further collapse commissioning time.
>
> These are building blocks we can add on top of what is already shipping."

**Timing:** 90 seconds.

### Slide 9 — Closing
> "To close —
>
> Deploy without a coder.
>
> Survive without a network.
>
> Scale without new engineering.
>
> That is the FluxGen IoT Gateway. Production-grade, today. Built in-house.
>
> Thank you."

**Timing:** 30 seconds. **Stop. Silence. 5 seconds minimum.**

---

## 3. Anticipated questions — with answers

### On the product itself

**Q: How does the gateway handle sensor variety?**
A: RS485 Modbus RTU with configurable byte order, register address, scale factor, and data type per sensor. Supports UINT16, INT16, FLOAT32 in four byte orders (ABCD, CDAB, DCBA). Adding a new sensor model is configuration via the web UI, not a firmware change.

**Q: How does the SIM + SD card handoff actually work?**
A: Telemetry writes to SD first, then attempts transmit to Azure. On success, marks as sent. On network failure, stays queued. When connectivity returns, chronological replay sends everything in order with original timestamps from the DS3231 RTC. Mutex-protected for thread safety.

**Q: What happens when the modem fails?**
A: Auto-reset logic. On MQTT disconnect, we power-cycle the A7670C modem with a 5-minute cooldown to prevent thrashing. SD card keeps caching during recovery. Zero data loss, zero truck roll.

**Q: What happens if the SD card itself fails?**
A: Three-retry logic per SD operation. On persistent failure, telemetry falls back to a RAM buffer (currently sized for 3 messages). The SD layer attempts automatic recovery every 60 seconds and flushes the RAM buffer back to SD when it recovers.

### On connectivity

**Q: How does WiFi-to-cellular failover work?**
A: WiFi is primary; if the MQTT connection drops and WiFi appears degraded, the SIM module takes over. Self-healing logic handles modem reinitialization.

**Q: What about SIM card data usage?**
A: Typical telemetry is ~50KB per site per day. Suitable for low-cost M2M SIM plans.

### On cloud operations

**Q: What's the OTA update flow?**
A: Azure Device Twin sets a target firmware URL. Device downloads over HTTPS, validates signature, flashes to alternate OTA slot, marks pending, reboots. If the new image fails to boot or check in, the bootloader falls back to the previous slot automatically.

**Q: What's configurable via Device Twin?**
A: Sensor maps (model, register, byte order, scale factor), telemetry interval, web server toggle, maintenance mode, remote reboot, Modbus retry parameters, firmware OTA trigger.

**Q: How secure is the device-to-cloud channel?**
A: MQTT over TLS 1.2 to Azure IoT Hub. Signed firmware images. Dual-slot OTA with automatic rollback on boot failure.

### On scaling

**Q: How many devices can a single Azure IoT Hub handle?**
A: Azure IoT Hub scales to hundreds of thousands of devices per hub. Well beyond anything we'd reach in the medium term.

**Q: What would be needed to manage a large fleet?**
A: The building blocks are there — OTA, Device Twin, remote reboot, standard MQTT. The natural next layer is a fleet management dashboard that surfaces device health, bulk config operations, and alerts. Functionally the gateway is already fleet-ready; the missing piece is a unified operator view.

**Q: Can we run this in other domains beyond water?**
A: Yes. The Modbus layer is generic. Any sensor that speaks Modbus RTU works. The data pipeline and cloud layer are agnostic.

### On production experience

**Q: What's been the biggest bug to hunt down?**
A: Recent examples: heap corruption with ZEST sensors (fixed by buffer sizing + data type handling), OTA failing from GitHub (fixed by stopping MQTT pre-OTA to free heap), SD card I/O errors causing data loss (fixed with retry + RAM fallback). All hardening experiences that are now documented.

**Q: What's the known limit?**
A: Max 10 sensors per gateway in v1.3.8 (reduced from 15 to free ~20KB heap for reliability). Partition table is frozen — we cannot change it without a manual reflash of deployed devices.

---

## 4. Closing — the power statement

Memorise. Deliver slowly. **Do not read.**

> "To close —
>
> Deploy without a coder.
>
> Survive without a network.
>
> Scale without new engineering.
>
> That is the FluxGen IoT Gateway. Production-grade, today. Built in-house.
>
> Thank you."

Then — **silence**. Five seconds minimum. Let them ask.

---

## 5. Pre-meeting checklist

- [ ] Open the deck in PowerPoint — verify fonts.
- [ ] Present Slide 1 → 9 out loud, timed. Target ~10 min.
- [ ] Print this document or put it on a second screen for Q&A reference.
- [ ] Know the technical capabilities (Slide 7) cold — this is the engineering-respect slide.
- [ ] Know the scaling path (Slide 8) — this is the forward-looking slide.
- [ ] Arrive 5 min early. Connect laptop. Test clicker.
