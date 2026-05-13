"""
Generate premium PNG assets for the FluxGen v2.0 pitch deck.
- Hero gradient backgrounds (title, closing)
- Iconography (flat, circular, glowing) for v1.x solutions + v2.0 features + problem tiles
- Architecture diagram as one polished wide image
"""

from PIL import Image, ImageDraw, ImageFilter, ImageFont
from pathlib import Path
import math

OUT = Path(__file__).parent / "assets"
OUT.mkdir(exist_ok=True)
(OUT / "icons").mkdir(exist_ok=True)

# ---------- PALETTE ----------
NAVY       = (10, 37, 64)
NAVY_DARK  = (6, 20, 38)
NAVY_DEEP  = (3, 12, 24)
TEAL       = (0, 194, 184)
TEAL_DARK  = (0, 140, 133)
TEAL_GLOW  = (32, 232, 224)
AMBER      = (244, 162, 97)
AMBER_DARK = (212, 132, 65)
AMBER_GLOW = (255, 190, 130)
ALERT      = (230, 57, 70)
SUCCESS    = (42, 157, 143)
WHITE      = (255, 255, 255)
BG_LIGHT   = (248, 249, 251)
TEXT_LIGHT = (149, 156, 168)


def radial_gradient(size, inner, outer, center=None, radius_ratio=0.85):
    w, h = size
    if center is None:
        center = (w // 2, h // 2)
    cx, cy = center
    max_r = math.hypot(max(cx, w - cx), max(cy, h - cy)) * radius_ratio
    img = Image.new("RGB", size, outer)
    px = img.load()
    for y in range(h):
        for x in range(w):
            d = math.hypot(x - cx, y - cy)
            t = min(d / max_r, 1.0)
            r = int(inner[0] * (1 - t) + outer[0] * t)
            g = int(inner[1] * (1 - t) + outer[1] * t)
            b = int(inner[2] * (1 - t) + outer[2] * t)
            px[x, y] = (r, g, b)
    return img


def linear_gradient_h(size, left, right):
    w, h = size
    img = Image.new("RGB", size)
    px = img.load()
    for x in range(w):
        t = x / max(w - 1, 1)
        r = int(left[0] * (1 - t) + right[0] * t)
        g = int(left[1] * (1 - t) + right[1] * t)
        b = int(left[2] * (1 - t) + right[2] * t)
        for y in range(h):
            px[x, y] = (r, g, b)
    return img


def linear_gradient_v(size, top, bottom):
    w, h = size
    img = Image.new("RGB", size)
    px = img.load()
    for y in range(h):
        t = y / max(h - 1, 1)
        r = int(top[0] * (1 - t) + bottom[0] * t)
        g = int(top[1] * (1 - t) + bottom[1] * t)
        b = int(top[2] * (1 - t) + bottom[2] * t)
        for x in range(w):
            px[x, y] = (r, g, b)
    return img


def dot_grid_overlay(size, color=(255, 255, 255, 18), spacing=40, radius=1):
    w, h = size
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for y in range(0, h, spacing):
        for x in range(0, w, spacing):
            d.ellipse([x - radius, y - radius, x + radius, y + radius], fill=color)
    return layer


def circuit_flourish(size, color=(0, 194, 184, 28)):
    """Abstract geometric accents: thin lines + circles, bottom-left quadrant."""
    w, h = size
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    # Concentric arcs from bottom-left
    for r in range(200, 1400, 120):
        bbox = [-r, h - r, r, h + r]
        d.arc(bbox, start=270, end=360, fill=color, width=2)
    # Floating dots along an arc
    for i in range(10):
        a = math.radians(280 + i * 8)
        dist = 900
        x = dist * math.cos(a)
        y = h + dist * math.sin(a)
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(*color[:3], 90))
    return layer


def soft_blob(size, color, center, radius, alpha=60):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = center
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius],
              fill=(*color, alpha))
    return layer.filter(ImageFilter.GaussianBlur(radius // 3))


# ============================================================
# HERO BACKGROUNDS
# ============================================================
def make_title_bg():
    """Title slide hero: 16:9, navy radial + teal glow + dot grid + accents."""
    size = (1920, 1080)
    # Base radial from center-top
    base = radial_gradient(size, inner=NAVY, outer=NAVY_DEEP, center=(600, 400))
    base = base.convert("RGBA")
    # Glow blobs
    base.alpha_composite(soft_blob(size, TEAL, (300, 900), 600, alpha=55))
    base.alpha_composite(soft_blob(size, AMBER, (1700, 200), 500, alpha=40))
    base.alpha_composite(soft_blob(size, TEAL, (1100, 600), 300, alpha=40))
    # Dot grid
    base.alpha_composite(dot_grid_overlay(size, color=(255, 255, 255, 14), spacing=48, radius=1))
    # Circuit flourish
    base.alpha_composite(circuit_flourish(size, color=(0, 194, 184, 50)))
    base.convert("RGB").save(OUT / "bg_title.png", "PNG", optimize=True)


def make_closing_bg():
    """Closing slide: navy radial + amber/teal corner glows + subtle grid."""
    size = (1920, 1080)
    base = radial_gradient(size, inner=(14, 44, 74), outer=NAVY_DEEP, center=(960, 540))
    base = base.convert("RGBA")
    base.alpha_composite(soft_blob(size, AMBER, (150, 950), 700, alpha=45))
    base.alpha_composite(soft_blob(size, TEAL, (1770, 140), 600, alpha=40))
    base.alpha_composite(dot_grid_overlay(size, color=(255, 255, 255, 12), spacing=54, radius=1))
    base.convert("RGB").save(OUT / "bg_closing.png", "PNG", optimize=True)


def make_section_bg():
    """Light section background with subtle gradient."""
    size = (1920, 1080)
    base = linear_gradient_v(size, (255, 255, 255), BG_LIGHT)
    base.save(OUT / "bg_light.png", "PNG", optimize=True)


# ============================================================
# ICONOGRAPHY
# ============================================================
def make_icon_canvas(size=512, bg_color=TEAL, glow=True):
    """Circular badge canvas with optional glow + gradient."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    if glow:
        glow_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow_layer)
        pad = size // 12
        gd.ellipse([pad, pad, size - pad, size - pad],
                   fill=(*bg_color, 110))
        glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(size // 18))
        img.alpha_composite(glow_layer)
    # Inner disc with subtle gradient
    grad = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    pad = size // 8
    gd.ellipse([pad, pad, size - pad, size - pad], fill=(*bg_color, 255))
    img.alpha_composite(grad)
    # Highlight crescent
    hi = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hi)
    pad = size // 8
    hd.ellipse([pad + 20, pad + 10, size - pad - 120, size - pad - 120],
               fill=(255, 255, 255, 45))
    hi = hi.filter(ImageFilter.GaussianBlur(size // 30))
    img.alpha_composite(hi)
    return img


def save_icon(img, name):
    img.save(OUT / "icons" / f"{name}.png", "PNG", optimize=True)


# -------- WiFi (dual connectivity)
def icon_wifi(color=TEAL):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, int(size * 0.64)
    for i, r in enumerate([60, 120, 180]):
        alpha = 255 - i * 30
        bbox = [cx - r, cy - r, cx + r, cy + r]
        d.arc(bbox, start=210, end=330, fill=(255, 255, 255, alpha), width=22)
    # Dot at base
    d.ellipse([cx - 18, cy - 18, cx + 18, cy + 18], fill=(255, 255, 255, 255))
    return img


# -------- SD card (offline cache)
def icon_sdcard(color=TEAL):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    # Card body with clipped top-right corner
    w, h = 180, 240
    x0, y0 = (size - w) // 2, (size - h) // 2
    x1, y1 = x0 + w, y0 + h
    clip = 50
    pts = [
        (x0, y0 + clip),
        (x0 + clip, y0),
        (x1, y0),
        (x1, y1),
        (x0, y1),
    ]
    d.polygon(pts, fill=(255, 255, 255, 255))
    # Contact strips on top
    strip_top = y0 + 30
    for i in range(5):
        sx = x0 + 40 + i * 20
        d.rectangle([sx, strip_top, sx + 10, strip_top + 40], fill=color)
    return img


# -------- Refresh / auto-heal
def icon_refresh(color=TEAL):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r = 110
    # Arc
    d.arc([cx - r, cy - r, cx + r, cy + r], start=20, end=320,
          fill=(255, 255, 255, 255), width=28)
    # Arrow head
    tip_a = math.radians(325)
    tip = (cx + int(r * math.cos(tip_a)), cy + int(r * math.sin(tip_a)))
    d.polygon([(tip[0] - 30, tip[1] - 35),
               (tip[0] + 25, tip[1] - 10),
               (tip[0] - 15, tip[1] + 35)], fill=(255, 255, 255, 255))
    return img


# -------- Cloud (OTA)
def icon_cloud(color=AMBER):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    # Cloud blob
    d.ellipse([140, 200, 260, 320], fill=(255, 255, 255, 255))
    d.ellipse([200, 170, 350, 320], fill=(255, 255, 255, 255))
    d.ellipse([280, 200, 400, 320], fill=(255, 255, 255, 255))
    d.rectangle([180, 280, 370, 340], fill=(255, 255, 255, 255))
    # Down arrow (representing OTA delivery)
    ax = size // 2
    ay = 340
    d.rectangle([ax - 10, ay - 10, ax + 10, ay + 50], fill=color)
    d.polygon([(ax - 28, ay + 40), (ax + 28, ay + 40), (ax, ay + 80)], fill=color)
    return img


# -------- Globe/network (web commissioning)
def icon_globe(color=AMBER):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r = 130
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 255, 255, 255), width=18)
    # Meridians
    for dr in [60, 0, -60]:
        d.arc([cx - 130, cy - r, cx + 130, cy + r], start=90, end=270,
              fill=(255, 255, 255, 220), width=10)
    d.line([cx - r, cy, cx + r, cy], fill=(255, 255, 255, 255), width=10)
    d.line([cx, cy - r, cx, cy + r], fill=(255, 255, 255, 255), width=10)
    return img


# -------- Grid / fleet console
def icon_grid(color=NAVY):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    g = 60  # tile size
    gap = 16
    start_x = (size - (g * 3 + gap * 2)) // 2
    start_y = (size - (g * 3 + gap * 2)) // 2
    for row in range(3):
        for col in range(3):
            x = start_x + col * (g + gap)
            y = start_y + row * (g + gap)
            d.rounded_rectangle([x, y, x + g, y + g], radius=10,
                                fill=(255, 255, 255, 255))
    # One highlighted
    x = start_x + 0 * (g + gap); y = start_y + 0 * (g + gap)
    d.rounded_rectangle([x, y, x + g, y + g], radius=10, fill=TEAL_GLOW)
    return img


# -------- Shield (security hardening)
def icon_shield(color=AMBER):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    cx = size // 2
    top = 140
    bottom = 380
    # Shield shape
    pts = [
        (cx, top),
        (cx + 110, top + 40),
        (cx + 110, top + 150),
        (cx, bottom),
        (cx - 110, top + 150),
        (cx - 110, top + 40),
    ]
    d.polygon(pts, fill=(255, 255, 255, 255))
    # Checkmark
    d.line([(cx - 40, 260), (cx - 10, 290), (cx + 50, 220)],
           fill=color, width=22, joint="curve")
    return img


# -------- Analytics / chart (edge analytics)
def icon_chart(color=TEAL):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    # Bars
    base_y = 360
    bar_w = 40
    heights = [80, 140, 110, 180, 150]
    start_x = (size - (len(heights) * bar_w + (len(heights) - 1) * 20)) // 2
    for i, hgt in enumerate(heights):
        x = start_x + i * (bar_w + 20)
        d.rounded_rectangle([x, base_y - hgt, x + bar_w, base_y], radius=6,
                            fill=(255, 255, 255, 255))
    # Trend line
    pts = [(start_x + bar_w // 2 + i * (bar_w + 20), base_y - hgt + 15)
           for i, hgt in enumerate(heights)]
    for a, b in zip(pts, pts[1:]):
        d.line([a, b], fill=AMBER_GLOW, width=6)
    for p in pts:
        d.ellipse([p[0] - 8, p[1] - 8, p[0] + 8, p[1] + 8], fill=AMBER_GLOW)
    return img


# -------- Mobile phone (technician app)
def icon_mobile(color=TEAL_DARK):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    # Phone body
    pw, ph = 180, 300
    x0, y0 = (size - pw) // 2, (size - ph) // 2
    d.rounded_rectangle([x0, y0, x0 + pw, y0 + ph], radius=28,
                        fill=(255, 255, 255, 255))
    # Screen
    pad = 18
    d.rounded_rectangle([x0 + pad, y0 + pad + 20, x0 + pw - pad, y0 + ph - pad - 24],
                        radius=10, fill=color)
    # Home indicator
    d.rounded_rectangle([x0 + pw // 2 - 28, y0 + ph - 30, x0 + pw // 2 + 28, y0 + ph - 20],
                        radius=4, fill=color)
    # Screen content mini-grid
    for i in range(3):
        for j in range(2):
            tx = x0 + pad + 14 + j * 60
            ty = y0 + pad + 40 + i * 60
            d.rounded_rectangle([tx, ty, tx + 50, ty + 50], radius=8, fill=(255, 255, 255, 70))
    return img


# -------- Code / refactor
def icon_code(color=AMBER_DARK):
    size = 512
    img = make_icon_canvas(size, color)
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    # Left bracket <
    d.line([(cx - 40, cy - 90), (cx - 140, cy), (cx - 40, cy + 90)],
           fill=(255, 255, 255, 255), width=24, joint="curve")
    # Right bracket >
    d.line([(cx + 40, cy - 90), (cx + 140, cy), (cx + 40, cy + 90)],
           fill=(255, 255, 255, 255), width=24, joint="curve")
    # Slash
    d.line([(cx + 40, cy - 90), (cx - 40, cy + 90)],
           fill=(255, 255, 255, 255), width=20)
    return img


# -------- Problem icons (red-tinted accent)
def icon_problem_dataloss(color=ALERT):
    size = 512
    img = make_icon_canvas(size, color, glow=False)
    d = ImageDraw.Draw(img)
    # Broken signal bars
    base_y = 340
    xs = [180, 240, 300, 360]
    heights = [60, 100, 140, 180]
    for i, (x, h) in enumerate(zip(xs, heights)):
        alpha = 255 if i < 2 else 110
        d.rounded_rectangle([x, base_y - h, x + 36, base_y], radius=6,
                            fill=(255, 255, 255, alpha))
    # Slash
    d.line([(150, 150), (380, 380)], fill=(255, 255, 255, 255), width=22)
    return img


def icon_problem_truck(color=AMBER_DARK):
    size = 512
    img = make_icon_canvas(size, color, glow=False)
    d = ImageDraw.Draw(img)
    # Truck body
    d.rounded_rectangle([130, 220, 310, 330], radius=12, fill=(255, 255, 255, 255))
    d.polygon([(310, 240), (400, 260), (400, 330), (310, 330)], fill=(255, 255, 255, 255))
    # Window
    d.rounded_rectangle([325, 260, 385, 305], radius=6, fill=color)
    # Wheels
    d.ellipse([170, 310, 230, 370], fill=color)
    d.ellipse([180, 320, 220, 360], fill=(255, 255, 255, 255))
    d.ellipse([320, 310, 380, 370], fill=color)
    d.ellipse([330, 320, 370, 360], fill=(255, 255, 255, 255))
    return img


def icon_problem_laptop(color=ALERT):
    size = 512
    img = make_icon_canvas(size, color, glow=False)
    d = ImageDraw.Draw(img)
    # Laptop
    d.rounded_rectangle([140, 180, 380, 320], radius=10, fill=(255, 255, 255, 255))
    d.rounded_rectangle([160, 200, 360, 300], radius=6, fill=color)
    d.polygon([(100, 340), (420, 340), (400, 370), (120, 370)], fill=(255, 255, 255, 255))
    return img


def icon_problem_eye(color=AMBER_DARK):
    size = 512
    img = make_icon_canvas(size, color, glow=False)
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    # Eye outline (ellipse)
    d.ellipse([cx - 140, cy - 70, cx + 140, cy + 70], outline=(255, 255, 255, 255), width=18)
    # Slash (no visibility)
    d.line([(cx - 160, cy + 120), (cx + 160, cy - 120)], fill=(255, 255, 255, 255), width=22)
    return img


# ============================================================
# ARCHITECTURE DIAGRAM
# ============================================================
def make_architecture_diagram():
    size = (1920, 720)
    img = Image.new("RGBA", size, BG_LIGHT + (255,))
    # Subtle background
    bg = linear_gradient_v(size, (255, 255, 255), (244, 247, 250))
    img = bg.convert("RGBA")
    d = ImageDraw.Draw(img)

    # Three boxes
    box_y = 100
    box_h = 480
    margin = 60
    gap = 120
    box_w = (size[0] - margin * 2 - gap * 2) // 3

    # ---- FIELD box ----
    fx = margin
    d.rounded_rectangle([fx, box_y, fx + box_w, box_y + box_h], radius=24,
                        fill=(239, 247, 246, 255), outline=TEAL + (255,), width=3)
    d.rounded_rectangle([fx, box_y, fx + box_w, box_y + 80], radius=24,
                        fill=TEAL + (255,))
    # Only round top
    d.rectangle([fx, box_y + 40, fx + box_w, box_y + 80], fill=TEAL + (255,))

    # ---- GATEWAY box ----
    gx = fx + box_w + gap
    d.rounded_rectangle([gx, box_y, gx + box_w, box_y + box_h], radius=24,
                        fill=NAVY + (255,))
    # header strip
    d.rounded_rectangle([gx, box_y, gx + box_w, box_y + 80], radius=24,
                        fill=NAVY_DARK + (255,))
    d.rectangle([gx, box_y + 40, gx + box_w, box_y + 80], fill=NAVY_DARK + (255,))

    # ---- CLOUD box ----
    cx_box = gx + box_w + gap
    d.rounded_rectangle([cx_box, box_y, cx_box + box_w, box_y + box_h], radius=24,
                        fill=(253, 245, 236, 255), outline=AMBER + (255,), width=3)
    d.rounded_rectangle([cx_box, box_y, cx_box + box_w, box_y + 80], radius=24,
                        fill=AMBER + (255,))
    d.rectangle([cx_box, box_y + 40, cx_box + box_w, box_y + 80], fill=AMBER + (255,))

    # ---- Arrows between boxes (gradient-like) ----
    def arrow(d, x1, y1, x2, color):
        # Thick line
        d.rectangle([x1, y1 - 6, x2 - 20, y1 + 6], fill=color + (255,))
        # Arrow head
        d.polygon([(x2 - 25, y1 - 20), (x2, y1), (x2 - 25, y1 + 20)], fill=color + (255,))

    mid_y = box_y + box_h // 2
    arrow(d, fx + box_w + 10, mid_y, gx - 10, TEAL_DARK)
    arrow(d, gx + box_w + 10, mid_y, cx_box - 10, AMBER_DARK)

    # ---- Labels on arrows ----
    try:
        font_label = ImageFont.truetype("arialbd.ttf", 18)
        font_header = ImageFont.truetype("arialbd.ttf", 22)
        font_title = ImageFont.truetype("arialbd.ttf", 34)
        font_body = ImageFont.truetype("arial.ttf", 20)
        font_small = ImageFont.truetype("arial.ttf", 17)
    except:
        font_label = ImageFont.load_default()
        font_header = font_label
        font_title = font_label
        font_body = font_label
        font_small = font_label

    # Arrow label pills
    def label_pill(d, cx, cy, text, fill, color=WHITE, font=None):
        tw = d.textlength(text, font=font)
        pad = 12
        d.rounded_rectangle([cx - tw / 2 - pad, cy - 16, cx + tw / 2 + pad, cy + 16],
                            radius=14, fill=fill + (255,))
        d.text((cx - tw / 2, cy - 12), text, fill=color, font=font)

    label_pill(d, fx + box_w + 10 + (gx - (fx + box_w + 10)) // 2, mid_y - 30,
               "RS485 · Modbus", TEAL_DARK, font=font_label)
    label_pill(d, gx + box_w + 10 + (cx_box - (gx + box_w + 10)) // 2, mid_y - 30,
               "MQTT · TLS", AMBER_DARK, font=font_label)

    # ---- FIELD content ----
    d.text((fx + 30, box_y + 25), "FIELD", fill=WHITE, font=font_header)
    d.text((fx + 30, box_y + 110), "Sensors", fill=NAVY, font=font_title)

    field_items = [
        ("Flow meters", "Modbus RTU"),
        ("pH · TDS · DO", "Water quality"),
        ("Temperature", "Real-time"),
        ("10 sensors", "Per gateway"),
    ]
    cy = box_y + 200
    for title, sub in field_items:
        d.ellipse([fx + 40, cy + 10, fx + 58, cy + 28], fill=TEAL + (255,))
        d.text((fx + 76, cy), title, fill=NAVY, font=font_body)
        d.text((fx + 76, cy + 26), sub, fill=TEXT_LIGHT, font=font_small)
        cy += 60

    # ---- GATEWAY content ----
    d.text((gx + 30, box_y + 25), "EDGE GATEWAY", fill=TEAL, font=font_header)
    d.text((gx + 30, box_y + 110), "ESP32 Gateway", fill=WHITE, font=font_title)

    gw_items = [
        "RS485 · Modbus RTU",
        "Dual network (WiFi + 4G)",
        "SD offline cache",
        "DS3231 RTC",
    ]
    cy = box_y + 200
    for item in gw_items:
        d.rounded_rectangle([gx + 30, cy, gx + box_w - 30, cy + 52],
                            radius=10, fill=(19, 50, 82, 255))
        # Dot
        d.ellipse([gx + 50, cy + 20, gx + 66, cy + 36], fill=TEAL + (255,))
        d.text((gx + 80, cy + 15), item, fill=WHITE, font=font_body)
        cy += 62

    # ---- CLOUD content ----
    d.text((cx_box + 30, box_y + 25), "CLOUD", fill=NAVY_DARK, font=font_header)
    d.text((cx_box + 30, box_y + 110), "Azure IoT Hub", fill=NAVY, font=font_title)

    cloud_items = [
        ("MQTT · TLS 1.2", "Encrypted channel"),
        ("Device Twin", "Remote config"),
        ("Telemetry", "Real-time ingestion"),
        ("OTA trigger", "Fleet delivery"),
    ]
    cy = box_y + 200
    for title, sub in cloud_items:
        d.ellipse([cx_box + 40, cy + 10, cx_box + 58, cy + 28], fill=AMBER_DARK + (255,))
        d.text((cx_box + 76, cy), title, fill=NAVY, font=font_body)
        d.text((cx_box + 76, cy + 26), sub, fill=TEXT_LIGHT, font=font_small)
        cy += 60

    # ---- Footer tagline ----
    tagline = "Offline-first — every layer survives the one below it failing."
    tw = d.textlength(tagline, font=font_title)
    d.text(((size[0] - tw) / 2, size[1] - 80), tagline, fill=NAVY, font=font_title)

    img.convert("RGB").save(OUT / "architecture.png", "PNG", optimize=True)


# ============================================================
# SECTION: problem + v1 + v2 icon sprite sheet reused as images
# ============================================================

def generate_all():
    print("Generating backgrounds...")
    make_title_bg()
    make_closing_bg()
    make_section_bg()

    print("Generating icons...")
    save_icon(icon_wifi(TEAL), "wifi")
    save_icon(icon_sdcard(TEAL), "sdcard")
    save_icon(icon_refresh(TEAL), "refresh")
    save_icon(icon_cloud(AMBER), "cloud")
    save_icon(icon_globe(AMBER), "globe")

    save_icon(icon_grid(NAVY), "grid")
    save_icon(icon_shield(AMBER), "shield")
    save_icon(icon_chart(TEAL), "chart")
    save_icon(icon_mobile(TEAL_DARK), "mobile")
    save_icon(icon_code(AMBER_DARK), "code")

    save_icon(icon_problem_dataloss(ALERT), "p_dataloss")
    save_icon(icon_problem_truck(AMBER_DARK), "p_truck")
    save_icon(icon_problem_laptop(ALERT), "p_laptop")
    save_icon(icon_problem_eye(AMBER_DARK), "p_eye")

    print("Generating architecture diagram...")
    make_architecture_diagram()
    print("Done.")


if __name__ == "__main__":
    generate_all()
