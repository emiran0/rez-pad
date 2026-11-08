#include "spectrum_widget.h"

#include "ui/theme.h"

namespace rp {
namespace widgets {

namespace {
constexpr int kMinBarPx = 2;  // silence still shows a thin baseline
}

void SpectrumWidget::build(lv_obj_t* card, int w, int h) {
  int n = spec_.bars ? spec_.bars : w / 10;
  if (n < 4) n = 4;
  if (n > kMaxBars) n = kMaxBars;
  slot_ = w / n;
  const int gap = slot_ >= 6 ? 2 : 1;
  barW_ = slot_ - gap > 1 ? slot_ - gap : 1;
  x0_ = (w - slot_ * n + gap) / 2;  // center the row
  count_ = n;
  h_ = h;
  color_ = ui::color::accent();  // the page's palette is current while it builds
  for (int i = 0; i < n; ++i) heights_[i] = kMinBarPx;

  area_ = lv_obj_create(card);
  lv_obj_remove_style_all(area_);
  lv_obj_set_clickable(area_, false);
  lv_obj_set_scrollable(area_, false);
  lv_obj_set_pos(area_, 0, 0);
  lv_obj_set_size(area_, w, h);
  lv_obj_add_event_cb(area_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

void SpectrumWidget::onDraw(lv_event_t* e) {
  auto* self = static_cast<SpectrumWidget*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords(self->area_, &c);
  lv_draw_fill_dsc_t dsc;
  lv_draw_fill_dsc_init(&dsc);
  dsc.color = self->color_;
  dsc.opa = LV_OPA_COVER;
  dsc.radius = 0;
  for (int i = 0; i < self->count_; ++i) {
    lv_area_t bar;
    bar.x1 = c.x1 + self->x0_ + i * self->slot_;
    bar.x2 = bar.x1 + self->barW_ - 1;
    bar.y2 = c.y1 + self->h_ - 1;
    bar.y1 = bar.y2 - self->heights_[i] + 1;
    // Only bars inside the area being redrawn (each frame redraws a few strips).
    const lv_area_t& clip = layer->_clip_area;
    if (bar.x1 > clip.x2 || bar.x2 < clip.x1 || bar.y1 > clip.y2 || bar.y2 < clip.y1) continue;
    lv_draw_fill(layer, &dsc, &bar);
  }
}

void SpectrumWidget::refresh(const ui::Model& m, uint32_t, uint32_t) {
  if (!area_) return;
  const ui::Spectrum& s = m.spectrum;
  lv_area_t c;
  lv_obj_get_coords(area_, &c);
  const int perGroup = (count_ + kDirtyGroups - 1) / kDirtyGroups;
  for (int g = 0; g * perGroup < count_; ++g) {
    // Rows that change in this group of bars: between each bar's old and new top.
    int top = h_, bottom = -1;
    for (int i = g * perGroup; i < (g + 1) * perGroup && i < count_; ++i) {
      uint8_t level = 0;
      if (m.hostConnected && s.n > 0) {
        // Bins [from, to) of the host's frame belong to this bar; take the loudest.
        int from = i * s.n / count_;
        int to = (i + 1) * s.n / count_;
        if (to <= from) to = from + 1;
        for (int k = from; k < to && k < s.n; ++k) {
          if (s.bins[k] > level) level = s.bins[k];
        }
      }
      const int px = kMinBarPx + level * (h_ - kMinBarPx) / 255;
      const int old = heights_[i];
      if (px == old) continue;
      heights_[i] = static_cast<uint16_t>(px);
      const int hi = px > old ? px : old;
      const int lo = px > old ? old : px;
      if (h_ - hi < top) top = h_ - hi;
      if (h_ - lo - 1 > bottom) bottom = h_ - lo - 1;
    }
    if (bottom < top) continue;
    lv_area_t dirty;
    dirty.x1 = c.x1 + x0_ + g * perGroup * slot_;
    dirty.x2 = c.x1 + x0_ + ((g + 1) * perGroup < count_ ? (g + 1) * perGroup : count_) * slot_ - 1;
    dirty.y1 = c.y1 + top;
    dirty.y2 = c.y1 + bottom;
    lv_obj_invalidate_area(area_, &dirty);
  }
}

}  // namespace widgets
}  // namespace rp
