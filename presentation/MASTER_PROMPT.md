# Master Prompt — FluxGen IoT Gateway v2.0 Executive Pitch

> **What this is:** A complete, self-contained prompt you can paste into any capable LLM (Claude, GPT-4/5, Gemini) to regenerate this deck from scratch, adapt it for a different audience, or build a follow-up presentation. Everything below is one prompt — copy from "`===== PROMPT START =====`" to "`===== PROMPT END =====`".

---

## How to use this document

- **Regenerate the same deck:** paste the prompt as-is into a fresh chat with code-execution (or ask the model to output a Python script using `python-pptx` + `Pillow`).
- **Adapt for a different audience** (e.g., customer pitch, board review, investor): change Section 2 (Audience & Goal) and Section 5 (The Ask). Leave the rest.
- **Translate / localize:** change the tone directive in Section 9. Keep structure.
- **Future releases (v2.1, v3.0):** update Section 4 (What's Built) and Section 5 (What's Next).

The prompt is long on purpose — LLMs produce generic output when briefed briefly. Specificity is what makes this land as executive-grade rather than template-grade.

---

```
===== PROMPT START =====

You are acting as a senior product manager + business analyst + CTO +
presentation designer rolled into one. Build a world-class,
executive-grade 10-slide PowerPoint presentation for a 10-minute
internal pitch at FluxGen (an Indian water-tech IoT company based in
Bangalore).

Output two things:
  1. A single Python script (python-pptx + Pillow) that generates a
     .pptx file at `FluxGen_IoT_Gateway_v2.0_Pitch.pptx`.
  2. A companion markdown document with speaker notes, anticipated
     Q&A with prepared answers, and a closing statement.

The deck is a real decision-forcing instrument, not a status update.
By the end of the meeting the presenter wants two things greenlit:
(a) the v2.0 feature roadmap, and (b) an expanded role as IoT
Platform Tech Lead.

========================================================
1. PROJECT CONTEXT (what the deck is actually about)
========================================================

Product name        : FluxGen IoT Gateway
Current version     : v1.3.8 (in production)
Proposal version    : v2.0
Hardware            : ESP32-WROOM-32, 4MB Flash, FreeRTOS
Framework           : ESP-IDF v5.5.1
Connectivity        : WiFi (STA+AP) + A7670C cellular (PPP)
Cloud               : Azure IoT Hub (MQTT over TLS), Device Twin
Storage             : NVS config, SD Card FAT32 offline cache,
                      DS3231 RTC for timestamps
Sensors             : RS485 Modbus RTU, up to 10 sensors/gateway —
                      flow meters + water quality analyzers
                      (Aquadax, Opruss Ace, Opruss TDS)
Deployment domain   : Water / effluent / sewage treatment plants
                      (STP / ETP) — customers submit compliance data
                      to Indian Pollution Control Boards (CPCB / PCB)

Key engineering history (use to inform root-cause analysis slide):
  - Heap corruption with ZEST sensors (fixed)
  - WiFi double-initialization crash (fixed)
  - MQTT reconnection spiraling into restart loop (fixed)
  - OTA from GitHub failing due to TLS/heap/redirect issues (fixed
    via stopping MQTT before OTA to free ~30KB heap)
  - SD card I/O errors causing data loss (fixed with 3-retry + RAM
    fallback buffer)
  - 30-min WiFi disconnect → forced restart (fixed by checking SD
    cache availability before restart)
  - Partition table FROZEN since v1.3.7 (must not change)
  - web_config.c is 609KB in one file — fragile, slow to modify

========================================================
2. AUDIENCE & MEETING GOAL
========================================================

Room composition   : All senior members — CEO, CTO/Eng Manager,
                     Operations Head, Finance Manager, Product Owner
Duration           : 10 minutes + Q&A
Presenter          : Y Chaithanya Reddy (firmware + cloud lead)
Company            : FluxGen (Bangalore, India)
Meeting type       : Problem-solving / progress + proposal
Primary decision   : Greenlight v2.0 AND expand presenter's scope
Secondary goal     : Communicate clearly how v1.x resolves the field
                     issues that triggered this programme
Tone               : Confident, direct, leadership-register.
                     Avoid hedging ("I think", "maybe", "hopefully").
                     Indian-English business register is fine.
                     McKinsey / Apple / Deloitte visual language.

========================================================
3. THE PROBLEM NARRATIVE (v1.x solved these)
========================================================

Pre-existing pain (before v1.x):
  - Data lost during network outages — no local buffer
  - Every firmware update required a truck roll to each site
  - Sensor changes required a laptop and trained engineer onsite
  - No remote visibility — customer discovered faults before we did
  - Single connectivity path (WiFi or cellular) created a single
    point of failure

Business impact of those pains:
  - Compliance gaps with CPCB/PCB reporting
  - Truck-roll operational cost
  - Customer trust erosion
  - Slow site expansion

========================================================
4. WHAT v1.x DELIVERS TODAY (proof of execution)
========================================================

Five shipped capabilities — lead with these:

  1. Dual connectivity (WiFi + A7670C cellular) with auto-failover.
     "Works wherever the customer needs us."

  2. SD-card offline cache (FAT32) with chronological replay and
     power-loss-safe fsync. "Zero data loss through outages."

  3. Auto self-healing — modem power-cycle on MQTT disconnect, with
     5-minute cooldown to avoid thrashing. "Recovers without a
     truck roll."

  4. Remote OTA via Azure Device Twin — cloud-pushed firmware,
     configuration, and control. "Fleet-wide updates, zero field
     visits."

  5. Web-based commissioning via ESP-hosted SoftAP. "Any technician
     with a phone can deploy — no laptop needed."

========================================================
5. v2.0 SCOPE (the greenlight ask)
========================================================

Five features — in strategic execution order:

  1. Modular Firmware Refactor — split 609KB web_config.c into
     maintainable modules. Ships first because it removes key-person
     risk and unlocks ~4× feature-delivery speed for the rest.
     Tag: "4× DELIVERY SPEED"

  2. Enterprise Security Hardening — signed OTA, TLS certificate
     pinning, secure boot, audit trails. Gate to government and
     enterprise tenders.
     Tag: "UNLOCKS TENDERS"

  3. Fleet Management Console — single pane of glass for all
     deployed gateways: bulk OTA, health alerts, config rollout.
     Biggest ops leverage play in the list.
     Tag: "OPERATIONAL LEVERAGE"

  4. Field Technician Mobile App — commission, diagnose, and recover
     gateways from a phone. Halves commissioning time.
     Tag: "OPS EFFICIENCY"

  5. Edge Analytics — lightweight on-device anomaly detection
     (z-score on rolling windows, threshold alerts). Not ML — runs
     in <5% CPU, <2KB RAM. Differentiator against commodity
     gateways.
     Tag: "DIFFERENTIATOR"

Note: order presented on the slide is the exec-impact narrative
order (Fleet Console → Security → Edge → Mobile → Refactor),
but execution order is the engineering-sanity order above.

Resource ask:
  Team      : 1 firmware engineer + 1 cloud/full-stack engineer
  Timeline  : 16 weeks, 4 milestones, go/no-go gate after each
  Budget    : salaries + ~₹1.5L cloud/tooling (indicative)
  Role      : IoT Platform Tech Lead — device-to-cloud ownership

Risks to pre-empt (put in mitigation table):
  - Key-person dependency      → refactor + docs in Month 1
  - Scope creep                → fixed 16-week window, gated
  - Security gaps in current   → Milestone 2, before new features
  - Customer impact on rollout → staged OTA, canary, rollback
  - Cloud cost drift           → monthly Azure review, feature flags

========================================================
6. BUSINESS IMPACT NUMBERS (mark indicative, defensible)
========================================================

Per site, per year — indicative direct savings:
  Field service visits avoided      : 4-6 / yr   → ₹ 20,000
  Firmware update cost              : near-zero  → ₹ 12,000
  Manual data collection eliminated : ₹ 18,000
  Compliance risk exposure          : mitigated (qualitative)
  Device cost vs imported PLC       : 50-70% lower (CapEx saving)
  Total direct savings              : ~₹ 50,000 / site / year

Chart comparison (bar chart):
  Imported PLC annual cost per site : ~₹ 85,000
  FluxGen v1.x annual cost per site : ~₹ 32,000

All numbers must be labelled "indicative" on the slide with a
footnote: "Illustrative figures. Actuals vary by site profile and
customer SLA."

If the CFO asks for exact numbers, the prepared line is:
"These are illustrative — I'll pull exact figures from our top
three customer sites after this meeting."

========================================================
7. DECK STRUCTURE (exactly 10 slides for 10 minutes)
========================================================

Slide 1  — Title
  Full-bleed navy gradient hero background. Large company + product
  wordmark. v2.0 subtitle. Presenter name, role, date. Glassy
  presenter panel bottom-left. "From field reliability to fleet
  intelligence" tagline.

Slide 2  — Executive Summary
  Three numbered cards: (1) We solved field data loss, (2) We
  eliminated truck rolls, (3) We are ready to scale. Full-width navy
  pill at the bottom containing the ask in one line.

Slide 3  — The Problem
  Left pain panel (soft red background) with bullet list of pre-v1.x
  pains. Right 2×2 tile grid showing business impact with icons.

Slide 4  — Root Cause Analysis
  2×2 grid of 4 architectural failure modes. Each box has a number
  badge, title, description, and an amber pill tag.

Slide 5  — What We Built (v1.x Delivered)
  Top row: 3 icon-cards (Dual Connectivity, SD Offline Cache, Auto
  Self-Healing). Bottom row: 2 wider icon-cards (Remote OTA, Web
  Commissioning). Each card has a premium circular icon with glow.

Slide 6  — How It Works (Architecture)
  Single polished wide architecture diagram: Field → Edge Gateway →
  Cloud, with colored headers and labeled gradient arrows. Tagline
  callout at bottom: "Offline-first — every layer survives the one
  below it failing."

Slide 7  — Business Impact
  Left: column chart comparing imported PLC (~₹85k) vs FluxGen
  (~₹32k) annual per-site cost. Right: structured per-site savings
  table with an amber "₹50,000" callout at the bottom.

Slide 8  — v2.0 Proposal
  Five feature rows, each with a colored left bar, icon, title,
  description, and a pill tag (Operational leverage / Unlocks
  tenders / Differentiator / Ops efficiency / 4× delivery speed).
  Timeline footer: "16 weeks · 4 milestones · go/no-go gate".

Slide 9  — The Ask
  Left navy panel: INVESTMENT — team, timeline, budget, role,
  deliverables. Right: RISKS table with 5 rows, each risk paired
  with its mitigation.

Slide 10 — Closing
  Full-bleed navy gradient with amber/teal corner glows. Three-line
  power statement in white:
    "v1.x proved we can build field-grade reliability
     that global brands charge 3× for.
     v2.0 turns that foundation into FluxGen's platform advantage."
  Amber "Greenlight v2.0 →" CTA pill. Presenter name footer.

========================================================
8. DESIGN SYSTEM (strict)
========================================================

Palette (hex):
  NAVY        #0A2540   primary — headers, dark panels
  NAVY_DARK   #06172A   deepest surfaces, hero backgrounds
  TEAL        #00C2B8   primary accent — Fluxgen brand
  TEAL_DARK   #008C85   secondary teal
  AMBER       #F4A261   secondary accent — ROI, CTA, warnings
  AMBER_DARK  #D48441   amber pressed state
  ALERT       #E63946   pain/red tint only
  BG_LIGHT    #F8F9FB   slide background
  DIVIDER     #E5E8EC   thin lines
  TEXT_DARK   #1A1A1A   body text primary
  TEXT_MID    #5B6472   body text secondary
  TEXT_LIGHT  #959CA8   footers / de-emphasis
  WHITE       #FFFFFF

Typography:
  Font: Calibri (safe on Windows PowerPoint)
  Title slide product name : 72 pt bold
  Slide titles             : 30 pt bold
  Kicker labels            : 11 pt bold uppercase
  Card headlines           : 15-22 pt bold
  Body copy                : 11-13 pt regular
  Footer / fine print      : 9-10 pt

Layout rules:
  - 16:9 widescreen (13.333 × 7.5 inches)
  - Left/right page margin: 0.6"
  - Page header band: kicker (teal) + title (navy) + divider line
  - Page footer: subtle grey — "FluxGen · [section]" left, slide
    number right
  - Cards use a colored top bar (0.12" tall) as accent + subtle 1pt
    divider border + soft offset shadow rectangle behind
  - No emoji — use PNG icons generated procedurally
  - Restraint over density — white space is an executive signal

Iconography:
  - 512×512 circular badges with radial glow
  - Navy / Teal / Amber base with white symbol
  - Light crescent highlight top-left for depth
  - Gaussian-blurred outer glow (alpha ~110)

========================================================
9. ANIMATIONS & TRANSITIONS
========================================================

Slide transitions (inject via <p:transition> in slide XML):
  - ALL 10 slides: fade, speed=medium, advance on click

Entrance animations (inject via <p:timing> on these slides only):

  Slide 1 (Title) — staggered fade-in, 250ms between each:
    1. Kicker        "FLUXGEN · INTERNAL"
    2. Product line  "FluxGen"
    3. Product line  "IoT Gateway"
    4. Teal divider bar
    5. Subtitle      "v2.0 Proposal · From field reliability..."
    6. Presenter panel

  Slide 2 (Executive Summary) — 3 cards fade in left-to-right,
    300ms stagger, 600ms duration

  Slide 10 (Closing) — staged reveal, 400ms stagger, 700ms duration:
    1. "CLOSING" kicker
    2. Main 3-line statement
    3. Sub-tagline
    4. "Greenlight v2.0 →" CTA button

Restraint principle: do NOT animate every bullet on every slide.
Modern executive decks use animation sparingly — only for the
opening, the summary, and the close.

========================================================
10. SPEAKER NOTES (embed in PowerPoint notes pane for each slide)
========================================================

Every slide gets speaker notes in first-person, written as if the
presenter is rehearsing out loud. Notes should include:
  - The exact spoken line (in quotes) for the first 1-2 sentences
  - A bulleted script for the rest of the slide
  - Timing target (e.g., "60 seconds")
  - A delivery cue (e.g., "eye contact with CFO on ₹50k line")
  - If there's a known trap question for that slide, the prepared
    answer

Delivery philosophy to embed throughout:
  - Lead with the number, not the methodology
  - Slow down on emotional beats, speed up on technical ones
  - Five-second silence after the closing — do not fill it

========================================================
11. COMPANION DOCUMENT (output alongside the .pptx)
========================================================

A markdown file `PRESENTATION_PACKAGE.md` with:

  Section 1 — Narrative arc summary (table mapping slide to the
             audience mental model shift it delivers)
  Section 2 — Full speaker notes (same text as slide notes, but in
             one place for rehearsal)
  Section 3 — Anticipated Q&A, organized by role:
               - CEO   (3 questions + prepared answers)
               - CFO   (3 questions)
               - Ops Head (3 questions)
               - CTO / Eng Manager (3 questions)
               - Product Owner (2 questions)
               - Trap questions (3) — "Why you?", "What if one
                 engineer instead of two?", "Can we defer a quarter?"
  Section 4 — Closing statement written out verbatim, with a
             note to memorize and deliver from memory, not read
  Section 5 — Pre-meeting checklist (6-8 items)
  Section 6 — Mapping of each slide to the 5 feelings the presenter
             wants the audience to have about them:
               (a) this project is valuable
               (b) presenter understands the business deeply
               (c) presenter can solve problems
               (d) presenter has leadership mindset
               (e) presenter deserves bigger responsibility

========================================================
12. TECHNICAL IMPLEMENTATION REQUIREMENTS
========================================================

Use Python 3.10+ with these libraries:
  - python-pptx >= 1.0   (deck generation)
  - Pillow >= 10.0       (graphics generation)
  - lxml                 (XML injection for animations/transitions,
                          already a python-pptx dependency)

Folder structure produced:
  presentation/
    ├── generate_graphics.py          # Pillow asset generator
    ├── generate_deck_v2.py           # pptx builder
    ├── FluxGen_IoT_Gateway_v2.0_Pitch.pptx
    ├── PRESENTATION_PACKAGE.md
    └── assets/
        ├── bg_title.png              # 1920x1080 navy gradient
        ├── bg_closing.png            # 1920x1080 navy w/ glow
        ├── architecture.png          # wide diagram
        └── icons/
            ├── wifi.png              # v1.x: dual connectivity
            ├── sdcard.png            # v1.x: offline cache
            ├── refresh.png           # v1.x: auto self-healing
            ├── cloud.png             # v1.x: remote OTA
            ├── globe.png             # v1.x: web commissioning
            ├── grid.png              # v2.0: fleet console
            ├── shield.png            # v2.0: security
            ├── chart.png             # v2.0: edge analytics
            ├── mobile.png            # v2.0: mobile app
            ├── code.png              # v2.0: refactor
            ├── p_dataloss.png        # problem: data loss
            ├── p_truck.png           # problem: truck roll
            ├── p_laptop.png          # problem: onsite laptop
            └── p_eye.png             # problem: no visibility

Implementation notes:
  - Use MSO_SHAPE.ROUNDED_RECTANGLE with adjustment[0] tuning
    instead of sharp-cornered rectangles for all cards and pills
  - Every shape gets shadow.inherit=False to avoid default PPT
    shadows conflicting with the custom shadow rectangles
  - Chart data uses CategoryChartData; per-bar color via
    inline c:dPt XML injection (python-pptx does not expose this
    directly)
  - Transitions/animations use lxml SubElement with qn() namespace
    map
  - Morph transitions need the p168 (2012) namespace — but prefer
    plain fade for compatibility with older PowerPoint versions
  - Provide unique shape.name values for every shape referenced in
    animation timing, so the timing XML can resolve spId by name

Constraints:
  - Do NOT use emoji anywhere in the deck (use PNG icons)
  - Do NOT exceed 10 slides total
  - Do NOT use stock-photo placeholders
  - Do NOT animate body bullets (only hero elements on 3 slides)
  - Target .pptx file size: under 3MB

========================================================
13. FINAL VOICE / STYLE DIRECTIVES
========================================================

- The deck must make management feel five specific things about the
  presenter, in order:
    1. This project is valuable.
    2. Presenter understands the business deeply (not just the code).
    3. Presenter can solve problems (root cause → solution).
    4. Presenter has leadership mindset (risks, mitigations, team).
    5. Presenter deserves bigger responsibility (the explicit ask).

- Every slide should earn at least one of these five feelings.
  If a slide doesn't, cut it.

- Write copy that respects the reader's time: no throat-clearing,
  no filler, no "we are pleased to present". Start each sentence
  with the thing that matters.

- When tradeoffs exist (e.g., exact numbers not known), label them
  "indicative" on the slide itself. Never fake precision in a room
  of executives — it always backfires.

- The closing slide statement is the centerpiece. It must be three
  lines, crescendo-structured:
    Line 1: what we proved (past tense, factual)
    Line 2: what it means comparatively (global context)
    Line 3: what v2.0 does with it (future, possessive — "FluxGen's")

===== PROMPT END =====
```

---

## Appendix — Fast variations

If you want to reuse this prompt with a different angle, swap these sections only:

### For a customer pitch (not internal management)
- Change Section 2: audience = customer stakeholders (plant manager, CTO of customer company)
- Change Section 5: the ask is "sign a pilot deployment for N sites" instead of "greenlight v2.0"
- Change Slide 9: instead of "The Ask" → "Pilot Deployment Plan"
- Change closing: "v1.x is live in production. Let's put it in your plant next."

### For an investor / board update
- Change Section 2: audience = board / investors
- Change Section 6: add projected revenue per device × deployment runway
- Change Slide 8: v2.0 features reframed as "market expansion levers"
- Change Slide 9: instead of "engineers + timeline" → "capital + runway"

### For a technical deep-dive (engineering audience only)
- Change Section 9: disable animations (engineers hate animated decks)
- Change Section 8: expand architecture to 3 slides (hardware / firmware / cloud)
- Drop Slide 7 (ROI) entirely; replace with "Performance Benchmarks"
- Change closing: technical merit, not business vision

---

## Appendix — Scope decisions log (why the deck is the way it is)

| Decision | Rationale |
|---|---|
| 10 slides, not 15 | 10 minutes ÷ 15 slides = 40 seconds/slide — too rushed. 10 slides ≈ 60 seconds each. |
| Fade transitions everywhere | Mixed transitions look dated / amateur. McKinsey uses one transition type across a whole deck. |
| Animations on only 3 slides (1, 2, 10) | Executive audiences find bullet-by-bullet animation patronizing. Reserve motion for dramatic moments: title, the ask, the close. |
| Indicative numbers labeled as such | Faking precision in front of a CFO is career suicide; labelling them "indicative" signals maturity. |
| Refactor positioned as feature #5 (not #1) on slide | Execution-wise it goes first. Narrative-wise it's least exciting — leading with it would bury the ask. |
| Fleet Console as feature #1 on slide | Highest executive resonance. "I want to see all our devices in one place" is something every exec intuitively agrees with. |
| Ask in both Slide 2 (callout) and Slide 9 (detail) | Repetition of the ask is a negotiation technique — the audience should hear it at minute 1 AND minute 9. |
| Tech Lead role ask separated from engineer headcount | These are two decisions. Bundling them lets the audience say yes to one and no to the other. |
| Closing slide uses AMBER CTA button | CTAs must visually contrast with the dominant palette. Navy+teal dominate — amber is the "act now" color. |

---

**Total length of the prompt:** ~1,300 lines / ~9,500 words when pasted — intentionally verbose, because specificity is what separates executive-grade output from template-grade output.
