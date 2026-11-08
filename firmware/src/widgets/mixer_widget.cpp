#include "mixer_widget.h"

#include "common.h"
#include "audio/mixer.h"
#include "ui/theme.h"

namespace rp {
namespace widgets {

namespace {

constexpr int kRowH = 30;
constexpr int kColW = 60;
constexpr int kLevelW = 40;
constexpr int kFaderMinH = 100;  // below this a "faders" mixer falls back to rows
constexpr int kLineH = 19;      // small font line height

lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_clickable(o, false);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

}  // namespace

void MixerWidget::build(lv_obj_t* card, int w, int h) {
  card_ = card;
  faders_ = spec_.faders && h >= kFaderMinH;
  slotCount_ = faders_ ? w / kColW : h / kRowH;
  if (slotCount_ < 1) slotCount_ = 1;
  if (slotCount_ > kMaxSlots) slotCount_ = kMaxSlots;

  for (int i = 0; i < slotCount_; ++i) {
    Slot& s = slots_[i];
    s.bg = box(card);
    lv_obj_set_style_radius(s.bg, 4, 0);
    lv_obj_set_style_bg_color(s.bg, ui::color::accent(), 0);
    lv_obj_set_style_bg_opa(s.bg, LV_OPA_30, 0);
    s.marker = box(card);
    lv_obj_set_style_radius(s.marker, 2, 0);
    lv_obj_set_style_bg_color(s.marker, ui::color::accent(), 0);
    s.track = box(card);
    lv_obj_set_style_bg_color(s.track, ui::color::faint(), 0);
    lv_obj_set_style_radius(s.track, 2, 0);
    s.fill = box(s.track);
    lv_obj_set_style_bg_color(s.fill, ui::color::accent(), 0);
    lv_obj_set_style_radius(s.fill, 2, 0);

    if (faders_) {
      // Column: level on top, vertical bar, name at the bottom.
      const int colW = w / slotCount_;
      const int x = i * colW;
      lv_obj_set_pos(s.bg, x + 1, 0);
      lv_obj_set_size(s.bg, colW - 2, h);
      lv_obj_set_pos(s.marker, x + 1, 0);
      lv_obj_set_size(s.marker, colW - 2, 3);
      s.level = makeLabel(card, ui::font::small(), ui::color::text(), "");
      lv_obj_set_width(s.level, colW);
      lv_obj_set_style_text_align(s.level, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(s.level, x, 4);
      // Name on up to two lines with a fixed height: a one-line label this narrow either wraps on
      // its own or ends in "..." after a few letters.
      s.name = makeLabel(card, ui::font::small(), ui::color::text(), "");
      lv_label_set_long_mode(s.name, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_size(s.name, colW - 4, 2 * kLineH - 2);
      lv_obj_set_style_text_line_space(s.name, -2, 0);
      lv_obj_set_style_text_align(s.name, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(s.name, x + 2, h - 2 * kLineH);
      trackLen_ = h - 3 * kLineH - 12;
      lv_obj_set_pos(s.track, x + (colW - 8) / 2, 6 + kLineH);
      lv_obj_set_size(s.track, 8, trackLen_);
    } else {
      // Row: name and level, a thin bar below.
      const int rowH = h / slotCount_;
      const int y = i * rowH;
      lv_obj_set_pos(s.bg, 0, y);
      lv_obj_set_size(s.bg, w, rowH - 2);
      lv_obj_set_pos(s.marker, 0, y);
      lv_obj_set_size(s.marker, 3, rowH - 2);
      const int ty = y + (rowH - 2 - 6 - kLineH) / 2;
      s.name = makeEllipsis(card, ui::font::small(), ui::color::text(), w - kLevelW - 14);
      lv_obj_set_pos(s.name, 8, ty);
      s.level = makeLabel(card, ui::font::small(), ui::color::text(), "");
      lv_obj_set_width(s.level, kLevelW);
      lv_obj_set_style_text_align(s.level, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(s.level, w - kLevelW - 4, ty);
      trackLen_ = w - 16;
      lv_obj_set_pos(s.track, 8, y + rowH - 2 - 7);
      lv_obj_set_size(s.track, trackLen_, 3);
    }
  }
  message_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  lv_obj_set_width(message_, w);
  lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(message_, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_center(message_);
}

void MixerWidget::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const ui::Mixer& mx = m.mixer;
  const bool focused = mx.focused && mx.focusWidget == &spec_;
  uint8_t order[ui::kMixerApps];
  const int shown = mixer::view(mx, spec_.apps, spec_.onlyApps, order);
  // Focus ring as an outline (drawn in the gap around the card): a wider border would shrink the
  // content area and move every child.
  lv_obj_set_style_outline_color(card_, ui::color::accent(), 0);
  lv_obj_set_style_outline_width(card_, focused ? 2 : 0, 0);
  lv_obj_set_style_outline_pad(card_, 0, 0);

  const char* message = !m.hostConnected ? "No host connected"
                         : !mx.received  ? ""
                         : mx.count == 0 ? "No apps with sound"
                         : shown == 0    ? "None of its apps is playing"
                                         : nullptr;
  setText(message_, message ? message : "");
  lv_obj_set_hidden(message_, message == nullptr);

  // Keep the selected app in view while focused; unfocused, the list starts at the beginning.
  if (!focused) first_ = 0;
  else if (mx.selected < first_) first_ = mx.selected;
  else if (mx.selected >= first_ + slotCount_) first_ = mx.selected - slotCount_ + 1;
  if (first_ > shown - slotCount_) first_ = shown - slotCount_ > 0 ? shown - slotCount_ : 0;

  for (int i = 0; i < slotCount_; ++i) {
    Slot& s = slots_[i];
    const int idx = first_ + i;
    const bool show = message == nullptr && idx < shown;
    const bool selected = show && focused && idx == mx.selected;
    lv_obj_set_hidden(s.name, !show);
    lv_obj_set_hidden(s.level, !show);
    lv_obj_set_hidden(s.track, !show);
    lv_obj_set_hidden(s.bg, !selected);
    lv_obj_set_hidden(s.marker, !selected);
    if (!show) continue;
    const ui::MixerApp& a = mx.apps[order[idx]];
    setText(s.name, a.name[0] ? a.name : a.id);
    // While focused the selected app stands out and the others step back; unfocused, apps that
    // are playing sound are bright and the rest dimmed.
    const bool bright = focused ? selected : a.active;
    lv_obj_set_style_text_color(s.name, bright ? ui::color::text() : ui::color::dim(), 0);
    char buf[8];
    if (a.muted) snprintf(buf, sizeof(buf), "%s", LV_SYMBOL_MUTE);
    else snprintf(buf, sizeof(buf), "%u", a.level);
    setText(s.level, buf);
    const bool dimLevel = a.muted || (focused && !selected);
    lv_obj_set_style_text_color(s.level, dimLevel ? ui::color::dim() : ui::color::text(), 0);
    const int len = trackLen_ * a.level / 100;
    if (faders_) {
      lv_obj_set_size(s.fill, 8, len);
      lv_obj_set_pos(s.fill, 0, trackLen_ - len);
    } else {
      lv_obj_set_size(s.fill, len, 3);
    }
    lv_obj_set_style_bg_color(s.fill, a.muted ? ui::color::dim() : ui::color::accent(), 0);
  }
}

}  // namespace widgets
}  // namespace rp
