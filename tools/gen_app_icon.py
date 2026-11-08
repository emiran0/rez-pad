"""Draws the rez-pad app icon (original artwork): a small dark device with an accent-colored screen
and a knob. Writes the exe icon, the two tray icons (connected / not connected) and the web
favicon.

  python tools/gen_app_icon.py

Needs Pillow. Outputs:
  host/src/RezPad.Host/rez-pad.ico        exe icon (16 to 256 px)
  host/src/RezPad.Host/tray-on.ico        tray, device connected
  host/src/RezPad.Host/tray-off.ico       tray, not connected (grey screen)
  web/img/favicon.png                     64 px
"""

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "host" / "src" / "RezPad.Host"
SS = 8  # supersampling

ACCENT = (79, 195, 247)  # default theme accent #4FC3F7
GREY = (120, 128, 140)
BODY = (32, 36, 44)
EDGE = (70, 78, 92)
KNOB = (200, 206, 214)


def draw(size: int, screen_color) -> Image.Image:
    n = size * SS
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = n / 32.0  # design grid: 32 units

    def box(x0, y0, x1, y1):
        return [round(x0 * u), round(y0 * u), round(x1 * u), round(y1 * u)]

    # Body: a slightly wide rounded slab with a light edge so it reads on dark taskbars.
    d.rounded_rectangle(box(1, 5, 31, 27), radius=round(5 * u), fill=EDGE)
    d.rounded_rectangle(box(2.2, 6.2, 29.8, 25.8), radius=round(4 * u), fill=BODY)
    # Screen on the left.
    d.rounded_rectangle(box(4.5, 8.5, 20.5, 23.5), radius=round(2 * u), fill=screen_color)
    # Two "widget" bars on the screen.
    dark = tuple(int(c * 0.55) for c in screen_color)
    d.rounded_rectangle(box(6.5, 11, 18.5, 13.5), radius=round(1 * u), fill=dark)
    d.rounded_rectangle(box(6.5, 16, 14, 21), radius=round(1 * u), fill=dark)
    # Knob on the right with a pointer notch.
    d.ellipse(box(22.3, 11.3, 28.7, 17.7), fill=KNOB)
    d.ellipse(box(23.6, 12.6, 27.4, 16.4), fill=(150, 156, 166))
    d.rounded_rectangle(box(25.1, 11.8, 25.9, 14.2), radius=round(0.4 * u), fill=BODY)
    # Small second knob below.
    d.ellipse(box(23.5, 19.5, 27.5, 23.5), fill=KNOB)

    return img.resize((size, size), Image.LANCZOS)


def save_ico(path: Path, screen_color, sizes):
    images = [draw(s, screen_color) for s in sizes]
    images[-1].save(path, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])
    print(f"wrote {path.relative_to(ROOT)}")


def main():
    save_ico(HOST / "rez-pad.ico", ACCENT, [16, 20, 24, 32, 40, 48, 64, 128, 256])
    save_ico(HOST / "tray-on.ico", ACCENT, [16, 20, 24, 32, 40, 48])
    save_ico(HOST / "tray-off.ico", GREY, [16, 20, 24, 32, 40, 48])
    fav = ROOT / "web" / "img" / "favicon.png"
    draw(64, ACCENT).save(fav)
    print(f"wrote {fav.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
