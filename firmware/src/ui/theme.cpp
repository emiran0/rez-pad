#include "theme.h"

namespace rp {
namespace ui {

namespace {

// bg, card, border, text, dim, faint, status bar (accent comes from the config).
constexpr Palette kPresets[] = {
    {0x0E1013, 0x1B1F25, 0x2A3038, 0xE8EAED, 0x8A939E, 0x3A414A, 0x15181D, 0},  // dark
    {0x0A0F1E, 0x141C33, 0x24304F, 0xE6ECFF, 0x8C9AC0, 0x33406A, 0x0F1528, 0},  // midnight
    {0x000000, 0x0F0F10, 0x222224, 0xF2F2F2, 0x8C8C8C, 0x333336, 0x000000, 0},  // black
    {0x202226, 0x2E3138, 0x41454E, 0xF0F1F3, 0x9EA3AD, 0x50555F, 0x26292E, 0},  // graphite
};

Palette gCurrent = {0x0E1013, 0x1B1F25, 0x2A3038, 0xE8EAED, 0x8A939E, 0x3A414A, 0x15181D, 0x4FC3F7};

}  // namespace

Palette makePalette(int theme, uint32_t accent) {
  const int n = sizeof(kPresets) / sizeof(kPresets[0]);
  Palette p = kPresets[theme >= 0 && theme < n ? theme : 0];
  p.accent = accent;
  return p;
}

const Palette& palette() { return gCurrent; }

void setPalette(const Palette& p) { gCurrent = p; }

namespace {
int gScreenW = 480, gScreenH = 320;
}  // namespace

void setScreenSize(int w, int h) {
  gScreenW = w;
  gScreenH = h;
}
int screenW() { return gScreenW; }
int screenH() { return gScreenH; }

}  // namespace ui
}  // namespace rp
