"""Generate the weather icon set for the firmware (original artwork, drawn here with Pillow).

Writes firmware/src/widgets/weather_icons.c and .h: 12 icons (WMO weather code groups, day and
night variants) at 32, 48 and 72 px as LVGL RGB565A8 images (RGB565 plane, then an 8-bit alpha
plane). Shapes are drawn on a 100x100 design grid at 8x supersampling and scaled down, so edges are
anti-aliased.

Usage: python tools/gen_weather_icons.py [--preview out.png]
"""

import argparse
import pathlib

from PIL import Image, ImageChops, ImageDraw

SIZES = [32, 48, 72]
SS = 8  # supersampling factor on the 100-unit design grid

# Colors chosen for the dark card background (#1B1F25).
SUN = (255, 200, 69)
SUN_RAY = (255, 179, 0)
MOON = (215, 222, 235)
CLOUD = (201, 209, 219)
CLOUD_BACK = (138, 147, 158)
CLOUD_DARK = (110, 119, 132)
RAIN = (79, 195, 247)
SNOW = (240, 244, 250)
BOLT = (255, 193, 7)
FOG = (160, 168, 178)

ICONS = [
    "clear_day", "clear_night", "partly_day", "partly_night", "cloudy", "fog",
    "drizzle", "rain", "heavy_rain", "sleet", "snow", "thunder",
]


class Canvas:
    """RGBA canvas addressed in design units (0..100)."""

    def __init__(self):
        self.size = 100 * SS
        self.img = Image.new("RGBA", (self.size, self.size), (0, 0, 0, 0))

    def s(self, v):
        return int(round(v * SS))

    def box(self, x0, y0, x1, y1):
        return [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]

    def layer(self, mask, color):
        """Composites a solid color through an L-mode mask."""
        solid = Image.new("RGBA", self.img.size, color + (255,))
        solid.putalpha(mask)
        self.img = Image.alpha_composite(self.img, solid)

    def mask(self):
        return Image.new("L", self.img.size, 0)


def sun(c, cx, cy, r, rays=True):
    m = c.mask()
    d = ImageDraw.Draw(m)
    d.ellipse(c.box(cx - r, cy - r, cx + r, cy + r), fill=255)
    c.layer(m, SUN)
    if rays:
        import math

        m = c.mask()
        d = ImageDraw.Draw(m)
        for i in range(8):
            a = math.pi / 4 * i
            x0, y0 = cx + math.cos(a) * r * 1.35, cy + math.sin(a) * r * 1.35
            x1, y1 = cx + math.cos(a) * r * 1.75, cy + math.sin(a) * r * 1.75
            w = c.s(r * 0.22)
            d.line([c.s(x0), c.s(y0), c.s(x1), c.s(y1)], fill=255, width=w)
            for x, y in ((x0, y0), (x1, y1)):  # round caps
                d.ellipse([c.s(x) - w // 2, c.s(y) - w // 2, c.s(x) + w // 2, c.s(y) + w // 2], fill=255)
        c.layer(m, SUN_RAY)


def moon(c, cx, cy, r):
    m = c.mask()
    d = ImageDraw.Draw(m)
    d.ellipse(c.box(cx - r, cy - r, cx + r, cy + r), fill=255)
    cut = c.mask()
    ImageDraw.Draw(cut).ellipse(c.box(cx - r * 0.35, cy - r * 1.25, cx + r * 1.65, cy + r * 0.75), fill=255)
    c.layer(ImageChops.subtract(m, cut), MOON)


def cloud(c, x, y, w, color):
    """Cloud whose bounding box starts at (x, y) with width w (height about 0.62 w)."""
    m = c.mask()
    d = ImageDraw.Draw(m)
    h = w * 0.62
    base_top = y + h * 0.48
    d.rounded_rectangle(c.box(x, base_top, x + w, y + h), radius=c.s(h * 0.26), fill=255)
    for (fx, fy, fr) in ((0.30, 0.55, 0.24), (0.58, 0.40, 0.33), (0.80, 0.62, 0.20)):
        r = w * fr
        d.ellipse(c.box(x + w * fx - r, y + h * fy - r, x + w * fx + r, y + h * fy + r), fill=255)
    c.layer(m, color)


def drops(c, xs, y, length, color, width=4.5):
    m = c.mask()
    d = ImageDraw.Draw(m)
    w = c.s(width)
    for x in xs:
        x0, y0, x1, y1 = x, y, x - length * 0.35, y + length
        d.line([c.s(x0), c.s(y0), c.s(x1), c.s(y1)], fill=255, width=w)
        for px, py in ((x0, y0), (x1, y1)):
            d.ellipse([c.s(px) - w // 2, c.s(py) - w // 2, c.s(px) + w // 2, c.s(py) + w // 2], fill=255)
    c.layer(m, color)


def dots(c, pts, r, color):
    m = c.mask()
    d = ImageDraw.Draw(m)
    for (x, y) in pts:
        d.ellipse(c.box(x - r, y - r, x + r, y + r), fill=255)
    c.layer(m, color)


def flakes(c, pts, r, color):
    import math

    m = c.mask()
    d = ImageDraw.Draw(m)
    w = c.s(r * 0.38)
    for (x, y) in pts:
        for i in range(3):
            a = math.pi / 3 * i + math.pi / 2
            dx, dy = math.cos(a) * r, math.sin(a) * r
            d.line([c.s(x - dx), c.s(y - dy), c.s(x + dx), c.s(y + dy)], fill=255, width=w)
    c.layer(m, color)


def bolt(c, x, y, s):
    m = c.mask()
    pts = [(0.55, 0.0), (0.10, 0.55), (0.42, 0.55), (0.25, 1.0), (0.85, 0.38), (0.52, 0.38), (0.75, 0.0)]
    ImageDraw.Draw(m).polygon([(c.s(x + px * s), c.s(y + py * s)) for px, py in pts], fill=255)
    c.layer(m, BOLT)


def fog_lines(c, ys, color):
    m = c.mask()
    d = ImageDraw.Draw(m)
    for i, y in enumerate(ys):
        x0 = 14 + (i % 2) * 8
        d.rounded_rectangle(c.box(x0, y - 3, x0 + 64, y + 3), radius=c.s(3), fill=255)
    c.layer(m, color)


def draw(name):
    c = Canvas()
    if name == "clear_day":
        sun(c, 50, 50, 20)
    elif name == "clear_night":
        moon(c, 50, 50, 30)
    elif name == "partly_day":
        sun(c, 36, 36, 15)
        cloud(c, 30, 40, 62, CLOUD)
    elif name == "partly_night":
        moon(c, 38, 34, 20)
        cloud(c, 30, 40, 62, CLOUD)
    elif name == "cloudy":
        cloud(c, 8, 22, 54, CLOUD_BACK)
        cloud(c, 24, 36, 70, CLOUD)
    elif name == "fog":
        cloud(c, 18, 14, 64, CLOUD_BACK)
        fog_lines(c, [60, 72, 84], FOG)
    elif name == "drizzle":
        cloud(c, 12, 12, 76, CLOUD)
        dots(c, [(32, 68), (50, 76), (68, 68), (41, 88), (59, 88)], 3.6, RAIN)
    elif name == "rain":
        cloud(c, 12, 10, 76, CLOUD)
        drops(c, [36, 54, 72], 62, 20, RAIN)
    elif name == "heavy_rain":
        cloud(c, 12, 8, 76, CLOUD_DARK)
        drops(c, [30, 45, 60, 75], 58, 26, RAIN)
    elif name == "sleet":
        cloud(c, 12, 10, 76, CLOUD)
        drops(c, [36, 66], 62, 18, RAIN)
        flakes(c, [(51, 82)], 7, SNOW)
    elif name == "snow":
        cloud(c, 12, 10, 76, CLOUD)
        flakes(c, [(34, 70), (66, 70), (50, 86)], 7, SNOW)
    elif name == "thunder":
        cloud(c, 12, 8, 76, CLOUD_DARK)
        bolt(c, 38, 50, 34)
        drops(c, [28, 78], 62, 14, RAIN)
    return c.img


def rgb565a8(img):
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b, a in img.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        rgb += bytes((v & 0xFF, v >> 8))  # little endian, LVGL's native RGB565
        alpha.append(a)
    return bytes(rgb + alpha)


def c_array(data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a PNG sheet of all icons on the card color")
    ap.add_argument("--web", help="also write <name>.png (72 px) per icon into this folder (web editor preview)")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    out_c = root / "firmware" / "src" / "widgets" / "weather_icons.c"
    out_h = root / "firmware" / "src" / "widgets" / "weather_icons.h"

    rendered = {n: draw(n) for n in ICONS}
    parts = [
        "// Generated by tools/gen_weather_icons.py. Do not edit; change the script and rerun it.",
        "// Original weather icons, RGB565A8 (RGB565 plane, then 8-bit alpha).",
        "#include \"weather_icons.h\"",
        "",
    ]
    table = []
    for n in ICONS:
        for size in SIZES:
            img = rendered[n].resize((size, size), Image.LANCZOS)
            data = rgb565a8(img)
            sym = f"{n}_{size}"
            parts.append(f"static const uint8_t {sym}_map[] = {{\n{c_array(data)}\n}};")
            parts.append(
                f"static const lv_image_dsc_t {sym} = {{\n"
                f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0,\n"
                f"               .w = {size}, .h = {size}, .stride = {size * 2}, .reserved_2 = 0}},\n"
                f"    .data_size = sizeof({sym}_map),\n"
                f"    .data = {sym}_map,\n"
                f"    .reserved = NULL,\n"
                f"    .reserved_2 = NULL,\n"
                f"}};\n")
        table.append("    {" + ", ".join(f"&{n}_{s}" for s in SIZES) + "},")
    parts.append(f"static const lv_image_dsc_t* const kIcons[RP_WX_ICON_COUNT][{len(SIZES)}] = {{\n" + "\n".join(table) + "\n};\n")
    parts.append(
        "const lv_image_dsc_t* rp_weather_icon(rp_wx_icon_t icon, int px) {\n"
        "  if ((int)icon < 0 || icon >= RP_WX_ICON_COUNT) icon = RP_WX_CLOUDY;\n"
        f"  static const int kSizes[{len(SIZES)}] = {{{', '.join(str(s) for s in SIZES)}}};\n"
        "  int best = 0;\n"
        f"  for (int i = 0; i < {len(SIZES)}; ++i) {{\n"
        "    if (kSizes[i] <= px) best = i;\n"
        "  }\n"
        "  return kIcons[icon][best];\n"
        "}\n")
    out_c.write_text("\n".join(parts), encoding="utf-8", newline="\n")

    enum = "\n".join(f"  RP_WX_{n.upper()}," for n in ICONS)
    out_h.write_text(
        "// Generated by tools/gen_weather_icons.py. Do not edit; change the script and rerun it.\n"
        "#pragma once\n\n#include <lvgl.h>\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
        f"typedef enum {{\n{enum}\n  RP_WX_ICON_COUNT,\n}} rp_wx_icon_t;\n\n"
        f"// Largest available size not above px ({', '.join(str(s) for s in SIZES)}).\n"
        "const lv_image_dsc_t* rp_weather_icon(rp_wx_icon_t icon, int px);\n\n"
        "#ifdef __cplusplus\n}\n#endif\n",
        encoding="utf-8", newline="\n")

    if args.preview:
        cell = 80
        sheet = Image.new("RGBA", (cell * len(ICONS), cell * len(SIZES)), (0x1B, 0x1F, 0x25, 255))
        for i, n in enumerate(ICONS):
            for j, size in enumerate(SIZES):
                img = rendered[n].resize((size, size), Image.LANCZOS)
                sheet.alpha_composite(img, (i * cell + (cell - size) // 2, j * cell + (cell - size) // 2))
        sheet.save(args.preview)
    if args.web:
        web = pathlib.Path(args.web)
        web.mkdir(parents=True, exist_ok=True)
        for n in ICONS:
            rendered[n].resize((72, 72), Image.LANCZOS).save(web / f"{n}.png", optimize=True)
    print(f"wrote {out_c.name} ({out_c.stat().st_size // 1024} KB source), {len(ICONS)} icons x {SIZES}")


if __name__ == "__main__":
    main()
