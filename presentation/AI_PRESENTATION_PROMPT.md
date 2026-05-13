# AI Presentation Prompt — ESP32 IoT Device Enhancement

> **How to use:** Scroll to **Section A** and copy-paste everything between the `===== PROMPT START =====` / `===== PROMPT END =====` markers into your AI tool: **Gamma AI**, **Beautiful.ai**, **Canva AI**, or **PowerPoint Copilot**.
>
> **Sections B–G** are supporting reference for you — visuals, transitions, chart decisions, speaker-note cues.
>
> **Hard rule for all four tools:** do not invent numbers, timelines, or customer names. If a figure isn't in the brief, it doesn't exist.

---

## Section A — The Copy-Paste Prompt

```
===== PROMPT START =====

ROLE: You are a senior presentation designer trained on McKinsey,
Apple, and Tesla keynotes. Create an executive-grade 10-slide deck
for a 10-minute office meeting. Audience is a mixed team of
leadership, engineering, operations, finance, and product — so the
language must be business-smart but never dumbed down. Technical
depth is welcome where it earns credibility.

THE ABSOLUTE RULES (honor these before anything else):

1. Use ONLY the facts in this brief. Do NOT invent numbers, costs,
   percentages, customer names, deployment counts, or timelines.
   If a figure isn't in the prompt, it doesn't exist.
2. This is a project showcase, not an investment pitch. Do not add
   "The Ask" slides, resource requests, team-hire asks, or ROI
   calculators.
3. Tone is calm, confident, factual. No hype. No emojis. No
   exclamation marks.
4. Every claim must trace back to a real feature in the brief.

─────────────────────────────────────────────────────────────────
PROJECT CONTEXT
─────────────────────────────────────────────────────────────────

Project title: ESP32 IoT Device Enhancement with SIM Connectivity,
               SD Card Backup & Web-Based Configuration

The project took a WiFi-dependent ESP32 IoT device and transformed
it into a smart hybrid IoT solution. Four additions made the
difference:

  - SIM Module      (cellular connectivity)
  - SD Card Backup  (offline storage)
  - RTC Module      (accurate timestamps)
  - Web Server      (browser-based configuration)

Presenter: Y Chaithanya Reddy · IoT Platform · FluxGen

─────────────────────────────────────────────────────────────────
DESIGN SYSTEM
─────────────────────────────────────────────────────────────────

Aesthetic: Apple keynote + McKinsey slide clarity + Tesla reveal
minimalism. Premium corporate. High white space. No clutter.

Color palette:
  Primary      Deep Navy        #0A2540    (dark surfaces, titles)
  Primary dark Near-Black Navy  #06172A    (hero backgrounds)
  Accent 1     Teal / Cyan      #00C2B8    (brand accent)
  Accent 2     Warm Amber       #F4A261    (selective emphasis)
  Alert Red    #E63946                     (used sparingly on pains)
  Success      #2A9D8F                     (after-state emphasis)
  Background   Off-White        #F8F9FB    (light slides)
  Text primary Near-Black       #1A1A1A
  Text muted   Slate            #5B6472
  Dividers     Pale Grey        #E5E8EC

Typography:
  Headlines   — sans-serif, bold, generous tracking.
                Inter / SF Pro Display / Calibri.
  Body        — clean sans-serif, regular weight, comfortable
                leading.

Layout rules:
  - 16:9 widescreen
  - 5% minimum margin on every edge
  - Max ~25 words body text per slide (exceptions: features 04
    BEFORE/AFTER table)
  - Cards have soft shadow and ≤8px corner radius
  - Icons are flat, circular, monochrome-on-tinted-background
  - No stock photography, no clip art, no gradient fills on text

─────────────────────────────────────────────────────────────────
DECK STRUCTURE — 10 SLIDES, ~60 seconds each
─────────────────────────────────────────────────────────────────

═══ SLIDE 1 — TITLE ═══════════════════════════════════════════════
Purpose: Establish identity. Premium visual register.

Layout: Full-bleed dark navy hero background with subtle radial
gradient (center glow). Faint geometric motif at low opacity. Left-
aligned title block. Bottom-left presenter panel.

Content:
  Kicker (small, teal, uppercase): "PROJECT SHOWCASE"
  Title (huge, white, bold, two lines):
    ESP32 IoT Device
    Enhancement
  Teal accent divider (short horizontal line)
  Subtitle (medium, teal):
    "SIM Connectivity  ·  SD Card Backup  ·  Web-Based Configuration"
  Tagline (small, muted light):
    "From WiFi-dependent to smart hybrid IoT solution"
  Presenter block (glassy panel, bottom-left):
    PRESENTED BY
    Y Chaithanya Reddy
    IoT Platform · Firmware & Cloud · April 2026

Transition in: Fade.
Entrance animations: Staggered fade-up — kicker → Title L1 → Title
L2 → Divider wipe → Subtitle → Tagline → Presenter panel.

═══ SLIDE 2 — PREVIOUS SYSTEM CHALLENGES ══════════════════════════
Purpose: What was broken before the upgrade.

Layout: Light background. Split. Left panel (60%) on soft-red tint
containing a headline + 5 pain bullets. Right panel (40%) with 2×2
impact tiles (icon + title + 1-line body).

Content:

  LEFT PANEL
    Kicker (red): "FIELD REALITY"
    Headline: "WiFi-only. Code-only."
    Bullets (teal dots):
      - Only WiFi connectivity → no cellular fallback.
      - Unstable WiFi at many sites → frequent downtime.
      - Network drops → significant data loss, no recovery.
      - Any config change → open Arduino code, edit, recompile.
      - Every update → reflash firmware into the ESP32 manually.

  RIGHT PANEL — 2×2 IMPACT TILES
    Kicker (amber): "IMPACT"
    Headline: "The cost of the old way."
    Tile 1 — Data loss
      Icon: broken-signal glyph
      Body: Every network outage lost telemetry permanently.
    Tile 2 — Technical dependency
      Icon: laptop glyph
      Body: Every change required a developer and recompile.
    Tile 3 — Slow rollout
      Icon: service vehicle glyph
      Body: Each site needed code edits + manual reflash.
    Tile 4 — Poor user experience
      Icon: crossed-eye glyph
      Body: Non-technical users locked out of configuration.

Transition: Fade.
Entrance animations: Left bullets top-down; right tiles fade in as
a group after.

═══ SLIDE 3 — NEW SOLUTION IMPLEMENTED ═════════════════════════════
Purpose: The four additions at a glance.

Layout: Light background. Header "NEW SOLUTION IMPLEMENTED · Four
additions that transformed the device". 2×2 grid of solution cards.
Each card has: colored top accent bar, large circular icon on left,
number (01–04), title, body paragraph.

Content (four cards):

  Card 01 — SIM Module  [Teal accent]
    Icon: WiFi / signal waves (represents cellular signal)
    Body: Cellular connectivity — device now uses mobile SIM network
    directly, reducing WiFi dependency and improving uptime in
    remote or unstable areas.

  Card 02 — SD Card Backup  [Teal accent]
    Icon: SD card glyph
    Body: Automatic offline storage — when network drops, SD card
    activates and stores every data ping locally. Zero data loss
    during outages.

  Card 03 — RTC Module  [Amber accent]
    Icon: circular-arrow / clock glyph
    Body: Real-Time Clock — provides accurate date and time
    continuously, so every stored ping gets an exact timestamp.
    Reliable historical records.

  Card 04 — Web Server  [Amber accent]
    Icon: globe / browser glyph
    Body: Browser-based configuration portal — configure device
    settings from any browser. No Arduino editing, no firmware
    reflash, no developer needed.

Transition: Fade.
Entrance animations: Cards reveal in Z-pattern (top-left, top-right,
bottom-left, bottom-right), 250 ms stagger.

═══ SLIDE 4 — SIM MODULE (DEEP DIVE) ═══════════════════════════════
Purpose: Explain the SIM Module and its benefits.

Layout: Header "FEATURE 01 · SIM MODULE · Cellular connectivity for
unstable WiFi locations". Left column: large icon inside a soft-
tinted circular badge, with caption below. Right column: five
benefit bullets (bold title + 1-line body each).

Content:
  Icon caption: "Mobile SIM Network" / "Cellular (A7670C 4G LTE)"

  Benefits (BOLD title + descriptive line):
    1. Mobile SIM network, directly.
       Device can now connect through a cellular SIM — no longer
       dependent on WiFi.
    2. Reduced WiFi dependency.
       Even if local WiFi fails, the device stays connected over
       the mobile network.
    3. Works in remote or unstable areas.
       Sites with weak or no WiFi now have a reliable connectivity
       path.
    4. Fewer transmission failures.
       Telemetry reaches the cloud even when the primary network
       is degraded.
    5. Higher uptime and reliability.
       Overall device availability improves across all deployment
       conditions.

Transition: Fade.
Entrance animations: Icon badge scales in from 90% to 100%. Then
benefits reveal top-down with 150 ms stagger.

═══ SLIDE 5 — SD CARD BACKUP (DEEP DIVE) ═══════════════════════════
Purpose: Explain how offline storage works.

Layout: Same pattern as Slide 4 — icon badge on left, benefits on
right. Plus a horizontal "flow strip" at the bottom of the slide
showing the offline backup sequence.

Content:
  Icon caption: "Local Offline Storage" / "FAT32 · Chronological
  replay"

  Benefits:
    1. Activates automatically.
       When the internet or network goes down, SD backup kicks in
       without any manual step.
    2. Stores every data ping locally.
       Every reading is written to the SD card with its timestamp.
    3. Prevents data loss during outages.
       No matter how long the network is down, nothing is dropped.
    4. Chronological replay on recovery.
       When the network returns, cached data is sent to the cloud
       in correct order.

  BOTTOM FLOW STRIP (dark navy pill, white bold text):
    Kicker: "OFFLINE FLOW"
    Text: "Network drop → SD backup activates → Every ping stored
    locally → Network back → Chronological replay to cloud"

Transition: Fade.
Entrance animations: Icon badge scales in. Benefits reveal top-down.
Flow strip fades in last.

═══ SLIDE 6 — RTC MODULE (DEEP DIVE) ═══════════════════════════════
Purpose: Explain why accurate time matters.

Layout: Same pattern as Slides 4 & 5.

Content:
  Icon caption: "DS3231 RTC" / "Accurate time, even offline"

  Points:
    1. RTC = Real-Time Clock Module.
       Dedicated hardware that tracks the current date and time
       continuously.
    2. Accurate time, even offline.
       The RTC keeps correct time even when the device is
       disconnected from the network.
    3. Every stored ping gets an exact timestamp.
       When SD backup is active, each reading is tagged with the
       precise time of capture.
    4. Correct historical records.
       Server-side logs, reports, and compliance data reflect
       actual capture time — not arrival time.
    5. Traceability across outages.
       You can always tell when each reading was taken, regardless
       of when it reached the cloud.

Transition: Fade.
Entrance animations: Icon badge scales in. Points reveal top-down.

═══ SLIDE 7 — WEB SERVER (DEEP DIVE) ═══════════════════════════════
Purpose: Show the before/after of configuration workflow.

Layout: Icon badge on left. On the right, a BEFORE/AFTER two-column
comparison panel. Each column has a colored header (BEFORE = red,
AFTER = green) and a list of steps. Left column uses "×" prefix;
right column uses "✓" prefix.

Content:
  Icon caption: "Web Configuration Portal" / "Browser-based · No
  code, no reflash"

  BEFORE (red header):
    × Open Arduino IDE
    × Edit source code manually
    × Recompile firmware
    × Reflash the ESP32
    × Requires a developer
    × Slow, technical, error-prone

  AFTER (green header):
    ✓ Open browser
    ✓ Navigate to device portal
    ✓ Change settings in UI
    ✓ Click save
    ✓ Anyone can do it
    ✓ Fast, user-friendly

Transition: Fade.
Entrance animations: Icon badge scales in. BEFORE column slides in
from left. AFTER column slides in from right (same moment).

═══ SLIDE 8 — WEB PORTAL LIVE VIEW (SCREENSHOTS) ═══════════════════
Purpose: Show the actual portal in action. Two screenshots of the
running web interface side by side, with captions.

Layout: Header "FEATURE 04 · WEB PORTAL LIVE VIEW · Modern,
browser-based configuration interface". Two framed screenshots
side-by-side (each ~45% of slide width) with drop shadow and white
padding frame. Below each screenshot: caption block with a colored
bar, number, bold title, and a 1-line description. Bottom: navy
pill strip listing all 6 portal sections.

Content:

  LEFT SCREENSHOT (Dashboard / Overview page)
    - Shows: System Status panel (Firmware, MAC Address, Uptime,
      Flash Memory, Active Tasks) and Memory Usage panel (Heap
      Usage bar, Free Heap, Internal RAM, SPIRAM, Largest Block)
    - Left sidebar visible with FluxGen logo + navigation
    Caption:
      Color bar: Teal
      Title: "01  ·  Dashboard"
      Body: "Real-time system status — firmware version, MAC
      address, uptime, heap usage, active tasks."

  RIGHT SCREENSHOT (Modbus Sensors page)
    - Shows: tabs (Regular Sensors, Water Quality Sensors, Modbus
      Explorer), a configured sensor card ("St Martha hospital
      (Sensor 2) - Dailian") with all properties visible, and
      action buttons EDIT / TEST RS485 / DELETE
    Caption:
      Color bar: Amber
      Title: "02  ·  Sensor Management"
      Body: "Add, edit, test and delete Modbus sensors from the
      browser — no code, no reflash."

  BOTTOM NAVIGATION STRIP (dark navy pill, full-width):
    Kicker (teal): "PORTAL SECTIONS"
    Text (white): "Overview  ·  Network Config  ·  Azure IoT Hub
                   ·  Modbus Sensors  ·  Write Operations  ·  OTA
                   Update"

Transition: Fade.
Entrance animations: Screenshots fade in together. Captions reveal
below each. Nav strip fades in last.

═══ SLIDE 9 — OVERALL BUSINESS IMPACT ══════════════════════════════
Purpose: Six outcomes that result from the four features.

Layout: 3×2 grid of impact tiles. Each tile has: colored accent top
bar, small icon on left, bold title (2 lines), 1-line body
description below.

Content (six tiles):

  Tile 1 — "Major reduction in data loss."
    Icon: SD card. Accent: Teal.
    Body: SD backup + SIM failover keep data flowing through
    outages.

  Tile 2 — "Improved connectivity reliability."
    Icon: WiFi. Accent: Teal.
    Body: Dual network removes the single-point-of-failure from
    WiFi.

  Tile 3 — "Easier field deployment."
    Icon: Globe. Accent: Teal.
    Body: Plug-and-play commissioning via the browser — no
    laptops, no code.

  Tile 4 — "Reduced technical dependency."
    Icon: Laptop. Accent: Amber.
    Body: Non-technical users can configure, monitor, and adjust
    settings.

  Tile 5 — "Faster customer support."
    Icon: Refresh. Accent: Amber.
    Body: Configuration changes happen remotely, without site
    visits.

  Tile 6 — "Better user experience."
    Icon: Eye / UI. Accent: Amber.
    Body: Simple UI and reliable behavior across all network
    conditions.

Transition: Fade.
Entrance animations: Tiles reveal row-by-row left-to-right, 200 ms
stagger.

═══ SLIDE 10 — FINAL SUMMARY & CLOSING ═════════════════════════════
Purpose: Mic-drop close. Use the project's closing line.

Layout: Full-bleed dark navy with subtle radial glow (amber bottom-
left, teal top-right, low opacity). Centered-left composition.

Content:
  Kicker (small teal uppercase): "FINAL SUMMARY"

  Main statement (huge white bold, 2 lines):
    From Data Loss
    to Smart Reliability.

  Sub-statement (medium teal, single line):
    "A Complete IoT Upgrade."

  Short amber divider line.

  Summary line (white bold):
    "The device transformed from a WiFi-dependent system into a
    smart hybrid IoT solution."

  Feature recap (small muted grey):
    "Dual network capability  ·  Offline data backup  ·  Accurate
    timestamp logging  ·  Easy web-based control"

  Bottom-left thank-you block:
    Thank you
    Y Chaithanya Reddy · FluxGen · IoT Platform

Transition: Fade.
Entrance animations: Kicker → main statement line 1 → line 2 →
sub-statement → divider wipe → summary line → feature recap →
thank-you block. 300 ms stagger.

─────────────────────────────────────────────────────────────────
CHART / DATA NOTE (critical — read twice)
─────────────────────────────────────────────────────────────────

This deck contains ZERO verified numeric data. Do NOT add:
  - Revenue projections
  - Cost-savings bar charts
  - ROI graphs
  - User-count trend lines
  - Market-size pie charts
  - Any "before/after" numeric comparisons

If your tool auto-suggests a chart, skip it. The diagrams that ARE
appropriate are:
  - Slide 5: bottom flow strip showing the offline data flow
  - Slide 7: BEFORE/AFTER workflow comparison (text, not chart)

The deck earns credibility through feature specification clarity,
not fabricated metrics.

─────────────────────────────────────────────────────────────────
TRANSITION MATRIX (quick reference)
─────────────────────────────────────────────────────────────────

All slides: Fade, medium speed, advance on click.
Do not mix transition types. Consistency is the executive signal.

─────────────────────────────────────────────────────────────────
ANIMATION RESTRAINT RULE
─────────────────────────────────────────────────────────────────

Animate the HERO element of each slide and the FIRST-REVEAL group.
Do not animate every bullet. Modern executive decks use motion to
draw the eye to what matters, not to prove the tool can do motion.

─────────────────────────────────────────────────────────────────
OUTPUT DELIVERY
─────────────────────────────────────────────────────────────────

Generate in 16:9 widescreen. Apply the design system strictly.
Preserve every piece of content verbatim. Use circular flat icons
in tinted badges. Do not add filler slides ("Agenda", "Thank You
v2", "Next Steps"). The Thank You is built into Slide 9.

===== PROMPT END =====
```

---

## Section B — Visual Suggestions Per Slide

| # | Slide | Visual treatment |
|---|---|---|
| 1 | Title | Full-bleed navy with subtle concentric-arc motif + faint dot-grid overlay at low opacity |
| 2 | Challenges | Left panel on soft-red tint (#FDF3F3). Right 2×2 tiles with 2px colored left border |
| 3 | Solution Overview | 2×2 grid of cards, each with large icon in soft-tinted badge + number + body |
| 4 | SIM Module | Left: large circular icon badge on tinted bg. Right: 5 bullet benefits with teal dots |
| 5 | SD Card Backup | Same layout as 4 + horizontal navy flow-strip at bottom |
| 6 | RTC Module | Same layout as 4 and 5 (amber badge tint for variety) |
| 7 | Web Server | Left icon. Right: 2-column BEFORE/AFTER table with red and green headers |
| 8 | Web Portal Live View | Two framed screenshots side-by-side with drop shadow. Teal/amber accent captions below. Navy nav strip at bottom |
| 9 | Business Impact | 3×2 grid of mini-cards, alternating teal/amber accent bars |
| 10 | Closing | Full-bleed navy. Subtle amber + teal corner glows at low opacity |

---

## Section C — Chart Decisions

**Use these visual elements:**
- **Slide 5 flow strip** — horizontal sequence: Network drop → SD activates → Store → Network back → Replay
- **Slide 7 before/after table** — 6 steps in each column with × and ✓ prefixes

**Do NOT use anywhere:**
- Bar charts (no verified comparison data)
- Pie charts (no segmentation data)
- Line graphs (no time-series data)
- KPI tiles with specific numbers

If Gamma/Beautiful/Canva auto-suggests any of the above, **reject them**.

---

## Section D — Transition & Animation Quick Reference

### Transitions (slide-to-slide)
- **Every slide: Fade (medium speed)**
- Uniform transitions = executive signal
- If tool supports **Morph**, use it for Slide 3 → 4 and Slide 5 → 6 to animate matching elements

### Entrance animations (per slide)

| # | Slide | Sequence |
|---|---|---|
| 1 | Title | Kicker → Title L1 → Title L2 → Divider wipe → Subtitle → Tagline → Presenter (200 ms) |
| 2 | Challenges | Left bullets top-down, right tiles as a group |
| 3 | Solution | 4 cards in Z-pattern (250 ms stagger) |
| 4 | SIM | Icon badge scale-in, then benefits top-down |
| 5 | SD Card | Icon scale-in, benefits top-down, flow strip fades in last |
| 6 | RTC | Icon scale-in, points top-down |
| 7 | Web Server | Icon scale-in, BEFORE slides in from left, AFTER slides in from right simultaneously |
| 8 | Business Impact | Tiles row-by-row, left-to-right (200 ms stagger) |
| 9 | Closing | Kicker → L1 → L2 → sub → divider wipe → summary → recap → thank-you (300 ms) |

### Restraint rule
Animate the hero and first-reveal group only. Do not animate each bullet individually.

---

## Section E — Speaker Notes (delivery-ready)

**Slide 1 (30 sec)**
> "Good morning. In the next ten minutes, I'll walk you through a project that transformed our ESP32 IoT device — from a WiFi-dependent system into a smart hybrid solution that handles network outages, stores data safely offline, and lets anyone configure it through a web browser."

**Slide 2 — Challenges (75 sec)**
> "Before this upgrade, the device had two big problems. One — connectivity. It only supported WiFi. At many sites WiFi was unstable or simply unavailable. Whenever the network dropped, data was lost permanently. Two — configuration. To change any setting, we had to open Arduino source code, modify it, and reflash firmware. Technical, time-consuming, and non-technical users were locked out. On the right — the real-world impact."

**Slide 3 — Solution Overview (60 sec)**
> "Four additions solved both problems. SIM Module — mobile cellular network directly. SD Card Backup — automatic local storage when the network drops. RTC Module — accurate real-time clock for timestamps. Web Server — browser-based configuration, no Arduino editing. The next four slides go into each one."

**Slide 4 — SIM Module (70 sec)**
> "First addition — SIM Module. The device connects through a mobile SIM, like a smartphone. Reduces WiFi dependency. Works in remote or unstable areas. Fewer transmission failures. Higher uptime and reliability. Five concrete benefits."

**Slide 5 — SD Card Backup (70 sec)**
> "Second addition — SD Card Backup. Whenever the internet or network connection goes down, the SD card activates automatically. Every data ping gets stored locally with its timestamp. When the network returns, the device replays the cached data to the cloud in chronological order. The bottom bar shows the flow — drop, activate, store, recover, replay."

**Slide 6 — RTC Module (65 sec)**
> "Third addition — the RTC Module. RTC is a Real-Time Clock — dedicated hardware that tracks date and time continuously, independent of the network. When SD backup is active, every stored ping gets an exact, accurate timestamp. This means our historical records and compliance reports reflect real capture time — not arrival time."

**Slide 7 — Web Server (70 sec)**
> "Fourth addition — the Web Server. Before this, changing any configuration meant opening Arduino IDE, editing code, recompiling, and reflashing firmware. Needed a developer and a build environment. Now anyone opens a browser, connects to the device, changes settings in a clean UI, and clicks save. Before took six technical steps. After takes four simple clicks — and anyone can do it."

**Slide 8 — Web Portal Live View (75 sec)**
> "This is what the portal actually looks like, running live on the device. On the left — the dashboard shows real-time system status: firmware version, MAC address, uptime, heap usage, active tasks. Operations teams can check device health at a glance without a terminal or developer. On the right — sensor management. We can add a Modbus sensor, configure its unit ID, slave ID, register, data type, byte order, and scale factor — all from the browser. Then test with a single click. The bar at the bottom shows all six portal sections — Overview, Network Config, Azure IoT Hub, Modbus Sensors, Write Operations, OTA Update. This portal replaced the Arduino-IDE-and-reflash workflow."

**Slide 9 — Business Impact (75 sec)**
> "Those four features produce six business outcomes. Major reduction in data loss. Improved connectivity reliability. Easier field deployment. Reduced technical dependency — non-technical users can deploy. Faster customer support — configuration changes happen remotely. Better user experience across the board."

**Slide 10 — Closing (30 sec)**
> "To summarize — this project took a WiFi-dependent ESP32 device and transformed it into a smart hybrid IoT solution. SIM, SD, RTC, Web Server — four additions that together delivered major reliability, offline safety, and a much better user experience. From data loss to smart reliability. A complete IoT upgrade. Thank you."
>
> *Then stop. Silence for 5 seconds. Let them ask.*

---

## Section F — Tool-Specific Tips

### Gamma AI
- Paste full Section A into "Create with AI"
- After generation, check icons per slide — may default to Gamma's icon library. Match: SIM→signal, SD→sdcard, RTC→clock, Web→globe
- Use "Restyle" to lock the navy/teal/amber palette

### Beautiful.ai
- Paste Section A → manually pick templates: "Numbered list" for Slide 3, "Two-column" for Slides 4-7, "Grid" for Slide 8
- Override default colors with the hex codes

### Canva AI
- Break Section A into per-slide prompts if truncation hits
- Use a "Business - Corporate" template as base, apply color overrides

### PowerPoint Copilot
- Paste whole brief
- Apply Morph transition manually (Copilot defaults to Fade)
- Search Designer icon library: "signal", "sd card", "clock", "globe", "laptop", "vehicle", "eye"

---

## Section G — Pre-meeting checklist

- [ ] Paste Section A into your AI tool, generate the deck
- [ ] Verify no invented numbers snuck in (scan every slide)
- [ ] Verify Slide 9 closing is exact: *"From Data Loss to Smart Reliability. A Complete IoT Upgrade."*
- [ ] Export to PowerPoint (.pptx) as backup — in case the web tool is unavailable in the meeting room
- [ ] Practice the deck out loud, timed. Target 10 minutes.
- [ ] Know Slides 4–7 cold — the feature deep-dives are the heart of the showcase
- [ ] Arrive 5 min early. Connect laptop. Test clicker. Breathe.

---

## Section H — Structure mapping (user's outline → slides)

Direct mapping between the user's original 9-section brief and the 10 slides in this deck (one slide added for web portal live view):

| User's section | Slide |
|---|---|
| Project Title | 1 — Title |
| Previous System Challenges | 2 — Previous System Challenges |
| New Solution Implemented | 3 — New Solution Implemented |
| SIM Module Benefits | 4 — SIM Module Deep Dive |
| SD Card Backup Feature | 5 — SD Card Backup Deep Dive |
| RTC Module Purpose | 6 — RTC Module Deep Dive |
| Web Server Development | 7 — Web Server Deep Dive (before/after) |
| *(additional — proof of execution)* | 8 — Web Portal Live View (screenshots) |
| Overall Business Impact | 9 — Overall Business Impact |
| Final Summary + Closing Line | 10 — Final Summary & Closing |

Every section preserved. Slide 8 was added to show the actual portal in action with real screenshots — proves the web server feature isn't vaporware, it's a polished product running today.
