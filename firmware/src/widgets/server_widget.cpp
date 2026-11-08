#include "server_widget.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ui/theme.h"

namespace rp {
namespace widgets {

namespace {

constexpr int kHeaderH = 24;
constexpr int kRowH = 21;
constexpr int kFooterH = 19;
constexpr int kLabelW = 40;
constexpr int kValueW = 42;

struct RowInfo {
  const char* metric;
  const char* label;
  float warn, crit;
};
const RowInfo kRowInfo[] = {
    {"cpu_load", "CPU", 80, 95},
    {"mem_used_pct", "MEM", 85, 95},
    {"disk_used_pct", "DISK", 90, 97},
};

void rate(char* out, size_t n, float kbps) {
  if (kbps >= 1000.0f) snprintf(out, n, "%.1fM", kbps / 1000.0f);
  else snprintf(out, n, "%.0fk", kbps);
}

}  // namespace

const ui::Stat* ServerWidget::stat(const char* metric) const {
  char id[56];  // the system id is at most 14 characters (config check), so this fits Stat::id
  snprintf(id, sizeof(id), "bz.%s.%s", spec_.source, metric);
  return ui::findStat(id);
}

void ServerWidget::build(lv_obj_t* card, int w, int h) {
  // Header: the machine's name (the label the editor set, else the system id) and a status dot.
  lv_obj_t* name = makeEllipsis(card, h >= 100 ? ui::font::body() : ui::font::small(), ui::color::text(), w - 16);
  setText(name, spec_.label[0] ? spec_.label : spec_.source);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, h >= 100 ? -2 : 0);
  dot_ = lv_obj_create(card);
  lv_obj_remove_style_all(dot_);
  lv_obj_set_size(dot_, 8, 8);
  lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
  lv_obj_align(dot_, LV_ALIGN_TOP_RIGHT, 0, 6);

  // As many bars as fit; the footer only when there is a line to spare.
  const bool footer = h - kHeaderH >= kRows * kRowH + kFooterH;
  rowCount_ = (h - kHeaderH - (footer ? kFooterH : 0)) / kRowH;
  if (rowCount_ > kRows) rowCount_ = kRows;
  if (rowCount_ < 1) rowCount_ = 1;
  const int rowsH = h - kHeaderH - (footer ? kFooterH : 0);
  const int step = rowsH / rowCount_;
  for (int i = 0; i < rowCount_; ++i) {
    const int y = kHeaderH + i * step + (step - 19) / 2;
    lv_obj_t* label = makeLabel(card, ui::font::small(), ui::color::dim(), kRowInfo[i].label);
    lv_obj_set_pos(label, 0, y);
    Row& r = rows_[i];
    r.bar = lv_bar_create(card);
    lv_obj_set_size(r.bar, w - kLabelW - kValueW - 4, 6);
    lv_obj_set_pos(r.bar, kLabelW, y + 7);
    lv_bar_set_range(r.bar, 0, 1000);
    lv_obj_set_style_bg_color(r.bar, ui::color::faint(), LV_PART_MAIN);
    r.value = makeLabel(card, ui::font::small(), ui::color::text(), "--");
    lv_obj_set_width(r.value, kValueW);
    lv_obj_set_style_text_align(r.value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r.value, w - kValueW, y);
  }
  if (footer) {
    footer_ = makeEllipsis(card, ui::font::small(), ui::color::dim(), w);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 0, 2);
  }
}

void ServerWidget::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const ui::Stat* online = m.hostConnected ? stat("online") : nullptr;
  const bool up = online && online->value >= 0.5f;
  lv_obj_set_style_bg_color(dot_, !online ? ui::color::faint() : up ? ui::color::ok() : ui::color::crit(), 0);

  for (int i = 0; i < rowCount_; ++i) {
    const ui::Stat* s = m.hostConnected ? stat(kRowInfo[i].metric) : nullptr;
    char buf[12] = "--";
    if (s) snprintf(buf, sizeof(buf), "%.0f%%", s->value);
    setText(rows_[i].value, buf);
    lv_bar_set_value(rows_[i].bar, s ? static_cast<int32_t>(s->value * 10) : 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(rows_[i].bar, s ? levelColor(s->value, kRowInfo[i].warn, kRowInfo[i].crit) : ui::color::faint(),
                              LV_PART_INDICATOR);
  }

  if (footer_) {
    // "load 0.12  41°  <down> 590k <up> 339k", whatever is known.
    char out[96] = "";
    size_t len = 0;
    auto put = [&](const char* text) {
      const int k = snprintf(out + len, sizeof(out) - len, "%s%s", len ? "   " : "", text);
      if (k > 0) len += static_cast<size_t>(k) < sizeof(out) - len ? static_cast<size_t>(k) : sizeof(out) - len - 1;
    };
    char part[32];
    if (!m.hostConnected) {
      put("No host connected");
    } else if (online && !up) {
      put("Offline");
    } else {
      if (const ui::Stat* s = stat("load1")) {
        snprintf(part, sizeof(part), "load %.2f", s->value);
        put(part);
      }
      if (const ui::Stat* s = stat("temp")) {
        snprintf(part, sizeof(part), "%.0f\xC2\xB0", s->value);
        put(part);
      }
      const ui::Stat* rx = stat("net_rx_kbps");
      const ui::Stat* tx = stat("net_tx_kbps");
      if (rx || tx) {
        char a[12] = "--", b[12] = "--";
        if (rx) rate(a, sizeof(a), rx->value);
        if (tx) rate(b, sizeof(b), tx->value);
        snprintf(part, sizeof(part), LV_SYMBOL_DOWNLOAD " %s " LV_SYMBOL_UPLOAD " %s", a, b);
        put(part);
      }
    }
    setText(footer_, out);
  }
}

}  // namespace widgets
}  // namespace rp
