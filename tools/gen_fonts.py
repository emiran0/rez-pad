"""Generate the firmware's LVGL fonts (firmware/src/fonts/rp_font_*.c).

Sources come from the LVGL package PlatformIO installs (firmware/.pio/libdeps/rezpad/lvgl/
scripts/generators/built_in_font/), so nothing else needs downloading:
  - Montserrat-Medium.ttf (OFL-1.1): Basic Latin, Latin-1 Supplement, Latin Extended-A, Cyrillic
  - DejaVuSans.ttf (Bitstream Vera / public domain changes): Greek
  - FontAwesome5-Solid+Brands+Regular.woff (OFL-1.1 font, CC BY 4.0 icons): LVGL's LV_SYMBOL_* set

Needs Node.js; runs lv_font_conv through npx (pinned version).
Usage (from the repo root): python tools/gen_fonts.py
Build the firmware once first so the LVGL package is installed.
"""

import pathlib
import shutil
import subprocess
import sys

LV_FONT_CONV = "lv_font_conv@1.5.3"

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / ".pio" / "libdeps" / "rezpad" / "lvgl" / "scripts" / "generators" / "built_in_font"
OUT = ROOT / "firmware" / "src" / "fonts"

MONTSERRAT = SRC / "Montserrat-Medium.ttf"
DEJAVU = SRC / "DejaVuSans.ttf"
AWESOME = SRC / "FontAwesome5-Solid+Brands+Regular.woff"

# LVGL's LV_SYMBOL_* code points (same list as LVGL's built_in_font_gen.py).
SYMBOLS = (
    "61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,"
    "61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,"
    "61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,"
    "61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"
)

LATIN = ["0x20-0x7E", "0xA0-0x17F"]  # Basic Latin, Latin-1 Supplement, Latin Extended-A
CYRILLIC = ["0x400-0x45F"]           # basic Cyrillic (Russian, Ukrainian, Bulgarian, Serbian)
GREEK = ["0x370-0x3FF"]

# name, size, include Cyrillic + Greek, include symbols
FONTS = [
    ("rp_font_14", 14, True, True),
    ("rp_font_20", 20, True, True),
    ("rp_font_28", 28, True, True),
    ("rp_font_48", 48, False, False),  # big numbers: ASCII + Latin-1 (degree sign)
]


def ranges(rs):
    out = []
    for r in rs:
        out += ["-r", r]
    return out


def main() -> int:
    for f in (MONTSERRAT, DEJAVU, AWESOME):
        if not f.exists():
            print(f"missing {f}; build the firmware once so PlatformIO installs LVGL", file=sys.stderr)
            return 1
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        print("npx not found; install Node.js", file=sys.stderr)
        return 1
    OUT.mkdir(parents=True, exist_ok=True)

    for name, size, intl, symbols in FONTS:
        cmd = [npx, "--yes", LV_FONT_CONV, "--size", str(size), "--bpp", "4", "--format", "lvgl",
               "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", name,
               "--font", str(MONTSERRAT)] + ranges(LATIN + (CYRILLIC if intl else []))
        if name == "rp_font_48":
            cmd = [npx, "--yes", LV_FONT_CONV, "--size", str(size), "--bpp", "4", "--format", "lvgl",
                   "--no-compress", "--lv-include", "lvgl.h", "--lv-font-name", name,
                   "--font", str(MONTSERRAT)] + ranges(["0x20-0x7E", "0xB0"])
        if intl:
            cmd += ["--font", str(DEJAVU)] + ranges(GREEK)
        if symbols:
            cmd += ["--font", str(AWESOME), "-r", SYMBOLS]
        out = OUT / f"{name}.c"
        cmd += ["-o", str(out)]
        print(f"generating {out.name} ({size} px)")
        subprocess.run(cmd, check=True)
        # Generated files use the source path in a comment; keep the output machine-independent.
        text = out.read_text(encoding="utf-8")
        for base in (SRC, OUT):
            text = text.replace(str(base) + "\\", "").replace(str(base) + "/", "")
        out.write_text(text, encoding="utf-8", newline="\n")
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
