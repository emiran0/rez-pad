// Now Playing widgets: album_art, track_info, media_controls, progress, now_playing, volume.
// Every widget accepts any card size above its minimum (lib/config_logic) and picks fonts and
// parts to fit; web/preview.js mirrors these layouts.
#include <initializer_list>

#include "common.h"
#include "media/media.h"
#include "media_widgets.h"

namespace rp {
namespace widgets {

using ui::Changed;
using ui::Model;
using ui::PlayState;

namespace {

// After a track change keep the previous image this long while the new one is on its way, so the
// card does not flash to the placeholder for a few hundred ms.
constexpr uint32_t kArtGraceMs = 1000;

int lineH(const lv_font_t* f) { return lv_font_get_line_height(f); }

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_scrollable(b, false);
  return b;
}

}  // namespace

// ---- ArtView ----

void ArtView::build(lv_obj_t* parent, int x, int y, int side, bool setsArtSize) {
  side_ = side;
  // The host sends one art size for the whole layout: the biggest any widget wants (decoder
  // buffers hold up to 240 px). Smaller views scale it down.
  if (setsArtSize && side > media::artSize()) media::setArtSize(side > 240 ? 240 : side);
  box_ = box(parent, x, y, side, side);
  lv_obj_set_style_radius(box_, side >= 80 ? 6 : 4, 0);
  lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(box_, LV_GRAD_DIR_VER, 0);
  icon_ = makeLabel(box_, side >= 70 ? ui::font::large() : ui::font::body(), ui::color::text(), LV_SYMBOL_AUDIO);
  lv_obj_center(icon_);

  img_ = lv_image_create(parent);
  lv_obj_set_pos(img_, x, y);
  lv_obj_set_size(img_, side, side);
  lv_image_set_inner_align(img_, LV_IMAGE_ALIGN_CENTER);
  lv_obj_set_style_radius(img_, side >= 80 ? 6 : 4, 0);
  lv_obj_set_style_clip_corner(img_, true, 0);
  lv_obj_set_hidden(img_, true);
}

void ArtView::showPlaceholder(const Model& m) {
  if (shownPx_) {
    lv_obj_set_hidden(img_, true);
    lv_obj_set_hidden(box_, false);
    shownPx_ = nullptr;
  }
  const bool none = m.np.state == PlayState::kNone;
  const uint32_t tint = none ? 0 : m.np.trackId;
  if (tint == tint_) return;
  tint_ = tint;
  const uint16_t hue = static_cast<uint16_t>(m.np.trackId % 360);
  lv_obj_set_style_bg_color(box_, none ? ui::color::faint() : lv_color_hsv_to_rgb(hue, 55, 55), 0);
  lv_obj_set_style_bg_grad_color(box_, none ? ui::color::card() : lv_color_hsv_to_rgb((hue + 40) % 360, 70, 25), 0);
  lv_obj_set_style_text_color(icon_, none ? ui::color::dim() : ui::color::text(), 0);
}

void ArtView::refresh(const Model& m, uint32_t nowMs) {
  if (!img_) return;
  const bool playing = m.np.state != PlayState::kNone;
  const bool match = playing && m.art.id == m.np.trackId;
  const bool grace = playing && !match && shownPx_ && shownPx_ == m.art.px && nowMs - m.np.trackSinceMs < kArtGraceMs;
  if (!m.art.px || !(match || grace)) {
    showPlaceholder(m);
    return;
  }
  if (shownPx_ == m.art.px && shownId_ == m.art.id) return;
  dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
  dsc_.header.w = m.art.w;
  dsc_.header.h = m.art.h;
  dsc_.header.stride = m.art.w * 2;
  dsc_.data = reinterpret_cast<const uint8_t*>(m.art.px);
  dsc_.data_size = static_cast<uint32_t>(m.art.w) * m.art.h * 2;
  // Same descriptor, new pixels: clear the source first so LVGL re-reads the header and redraws.
  // (No image cache to drop: LV_CACHE_DEF_SIZE is 0, raw RGB565 is drawn straight from PSRAM.)
  lv_image_set_src(img_, nullptr);
  lv_image_set_src(img_, &dsc_);
  // Scale to this view when the layout's art size is bigger (another album art view).
  const int scale = m.art.w > side_ ? side_ * LV_SCALE_NONE / m.art.w : LV_SCALE_NONE;
  lv_image_set_scale(img_, static_cast<uint32_t>(scale));
  lv_obj_set_hidden(img_, false);
  lv_obj_set_hidden(box_, true);
  shownPx_ = m.art.px;
  shownId_ = m.art.id;
  tint_ = 0xFFFFFFFFu;
}

// ---- TextView ----

void TextView::build(lv_obj_t* parent, int x, int y, int w, int h, bool center) {
  const lv_font_t* titleF = h >= 120 ? ui::font::large() : h >= 52 ? ui::font::body() : ui::font::small();
  const lv_font_t* artistF = h >= 120 ? ui::font::body() : ui::font::small();
  int left = h - lineH(titleF);
  const bool artist = left >= lineH(artistF);
  if (artist) left -= lineH(artistF) + 2;
  const bool album = h >= 100 && left >= lineH(ui::font::small()) + 2;
  if (album) left -= lineH(ui::font::small()) + 2;
  const bool app = left >= lineH(ui::font::small()) + 2;

  lv_obj_t* col = box(parent, x, y, w, h);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  const lv_flex_align_t cross = center ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START;
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, cross, cross);
  lv_obj_set_style_pad_row(col, h >= 120 ? 6 : 2, 0);
  // Only the title scrolls; scrolling text redraws every frame over SPI, so keep it to one label.
  title_ = makeMarquee(col, titleF, ui::color::text(), w);
  if (artist) artist_ = makeEllipsis(col, artistF, ui::color::text(), w);
  if (album) album_ = makeEllipsis(col, ui::font::small(), ui::color::dim(), w);
  if (app) app_ = makeEllipsis(col, ui::font::small(), ui::color::accent(), w);
  for (lv_obj_t* l : {title_, artist_, album_, app_}) {
    if (l && center) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
}

void TextView::refresh(const Model& m) {
  if (!title_) return;
  const bool none = m.np.state == PlayState::kNone;
  setText(title_, none ? (m.hostConnected ? "Nothing playing" : "No host connected") : m.np.title);
  if (artist_) setText(artist_, none ? "" : m.np.artist);
  if (album_) setText(album_, none ? "" : m.np.album);
  if (app_) setText(app_, none ? "" : m.np.app);
}

// ---- ControlsView ----

void ControlsView::build(lv_obj_t* parent, int x, int y, int w, int h, bool withButtons) {
  lv_obj_t* area = box(parent, x, y, w, h);
  const bool times = h >= (withButtons ? 70 : 30);
  const bool bar = !withButtons || h >= 44;
  int rowH = 0;
  if (withButtons) {
    rowH = times ? 34 : bar ? h - 12 : h;
    lv_obj_t* row = box(area, 0, 0, w, rowH);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const bool big = w >= 120 && rowH >= 30;
    prev_ = makeLabel(row, w >= 90 ? ui::font::body() : ui::font::small(), ui::color::text(), LV_SYMBOL_PREV);
    play_ = makeLabel(row, big ? ui::font::large() : ui::font::body(), ui::color::accent(), LV_SYMBOL_PLAY);
    next_ = makeLabel(row, w >= 90 ? ui::font::body() : ui::font::small(), ui::color::text(), LV_SYMBOL_NEXT);
  }
  if (bar) {
    bar_ = lv_bar_create(area);
    lv_obj_set_size(bar_, w, 6);
    if (times) lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -20);
    else if (withButtons) lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    else lv_obj_align(bar_, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(bar_, 0, 1000);
    lv_obj_set_style_bg_color(bar_, ui::color::faint(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, ui::color::accent(), LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(bar_, 0, 0);
  }
  if (times) {
    elapsed_ = makeLabel(area, ui::font::small(), ui::color::dim(), "0:00");
    lv_obj_align(elapsed_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    duration_ = makeLabel(area, ui::font::small(), ui::color::dim(), "0:00");
    lv_obj_align(duration_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  }
}

void ControlsView::refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  const auto& np = m.np;
  if (play_ && (changed & ui::kChangedMedia)) {
    const bool none = np.state == PlayState::kNone;
    setText(play_, np.state == PlayState::kPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_, none ? ui::color::faint() : ui::color::accent(), 0);
    lv_obj_set_style_text_color(prev_, np.canPrev ? ui::color::text() : ui::color::faint(), 0);
    lv_obj_set_style_text_color(next_, np.canNext ? ui::color::text() : ui::color::faint(), 0);
  }
  const uint32_t pos = np.positionAt(nowMs);
  if (bar_) {
    const int32_t permille = np.durMs ? static_cast<int32_t>(static_cast<uint64_t>(pos) * 1000 / np.durMs) : 0;
    if (permille != lv_bar_get_value(bar_)) lv_bar_set_value(bar_, permille, LV_ANIM_OFF);
  }
  if (elapsed_) {
    char buf[16];
    fmtDuration(buf, sizeof(buf), pos);
    setText(elapsed_, np.state == PlayState::kNone ? "" : buf);
    fmtDuration(buf, sizeof(buf), np.durMs);
    setText(duration_, np.state == PlayState::kNone ? "" : buf);
  }
}

// ---- album_art ----

void AlbumArt::build(lv_obj_t* card, int w, int h) {
  // Use the card's padding too (7 px each side), keeping a 2 px margin inside the border.
  const int side = (w < h ? w : h) + 14 - 4;
  art_.build(card, (w - side) / 2, (h - side) / 2, side);
}

uint32_t AlbumArt::interests() const { return ui::kChangedMedia | ui::kChangedArt | ui::kChangedTick; }

void AlbumArt::refresh(const Model& m, uint32_t, uint32_t nowMs) { art_.refresh(m, nowMs); }

// ---- track_info ----

void TrackInfo::build(lv_obj_t* card, int w, int h) { text_.build(card, 0, 0, w, h); }

uint32_t TrackInfo::interests() const { return ui::kChangedMedia | ui::kChangedHost; }

void TrackInfo::refresh(const Model& m, uint32_t, uint32_t) { text_.refresh(m); }

// ---- media_controls / progress ----

void MediaControls::build(lv_obj_t* card, int w, int h) { controls_.build(card, 0, 0, w, h, withButtons_); }

uint32_t MediaControls::interests() const { return ui::kChangedMedia | ui::kChangedTick; }

void MediaControls::refresh(const Model& m, uint32_t changed, uint32_t nowMs) { controls_.refresh(m, changed, nowMs); }

// ---- now_playing ----

void NowPlayingWidget::build(lv_obj_t* card, int w, int h) {
  if (w * 2 >= h * 3) {
    // Wide: art on the left (full height), text and controls on the right.
    const int side = h;
    const int x = side + 10;
    const int cw = w - x;
    const int ctrlH = h >= 110 ? 64 : h >= 76 ? 44 : 30;
    hasArt_ = true;
    art_.build(card, 0, 0, side);
    text_.build(card, x, 0, cw, h - ctrlH - 4);
    controls_.build(card, x, h - ctrlH, cw, ctrlH, true);
  } else {
    const int ctrlH = h >= 200 ? 64 : 44;
    const int textH = h >= 200 ? 46 : 40;
    // Stacked: art on top, centered, then text and controls (tall cards with room for big art).
    int stacked = h - ctrlH - textH - 8;
    if (stacked > w) stacked = w;
    // Side by side: art top left, text to its right, controls below across the card. Wins when the
    // stacked art would be small next to empty space (square-ish cards, narrow portrait screens).
    const int topH = h - ctrlH - 4;
    int beside = w / 2 < topH ? w / 2 : topH;
    if (w - beside - 8 < 80) beside = 0;  // the text needs 80 px
    if (beside >= 40 && beside * 115 > stacked * 100) {
      hasArt_ = true;
      art_.build(card, 0, 0, beside);
      text_.build(card, beside + 8, 0, w - beside - 8, topH);
      controls_.build(card, 0, h - ctrlH, w, ctrlH, true);
    } else {
      hasArt_ = stacked >= 40;
      const int top = hasArt_ ? stacked + 6 : 0;
      if (hasArt_) art_.build(card, (w - stacked) / 2, 0, stacked);
      text_.build(card, 0, top, w, h - top - ctrlH - 2, true);
      controls_.build(card, 0, h - ctrlH, w, ctrlH, true);
    }
  }
}

uint32_t NowPlayingWidget::interests() const {
  return ui::kChangedMedia | ui::kChangedArt | ui::kChangedTick | ui::kChangedHost;
}

void NowPlayingWidget::refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  if (hasArt_ && (changed & (ui::kChangedMedia | ui::kChangedArt | ui::kChangedTick))) art_.refresh(m, nowMs);
  if (changed & (ui::kChangedMedia | ui::kChangedHost)) text_.refresh(m);
  controls_.refresh(m, changed, nowMs);
}

// ---- volume ----

void VolumeWidget::build(lv_obj_t* card, int w, int h) {
  const int side = (w < h ? w : h);
  const int arcW = side >= 120 ? 10 : side >= 70 ? 7 : 5;
  arc_ = lv_arc_create(card);
  lv_obj_set_size(arc_, side, side);
  lv_obj_center(arc_);
  lv_arc_set_rotation(arc_, 135);
  lv_arc_set_bg_angles(arc_, 0, 270);
  lv_arc_set_range(arc_, 0, 100);
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);
  lv_obj_set_style_arc_width(arc_, arcW, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, arcW, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, ui::color::faint(), LV_PART_MAIN);
  const bool icon = side >= 64;
  value_ = makeLabel(card, side >= 120 ? ui::font::large() : side >= 64 ? ui::font::body() : ui::font::small(),
                     ui::color::text(), "0");
  lv_obj_align(value_, LV_ALIGN_CENTER, 0, icon ? -(side / 16) - 2 : 0);
  if (spec_.appTarget && side >= 90) {
    name_ = makeEllipsis(card, ui::font::small(), ui::color::dim(), side * 3 / 5);
    lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name_, LV_ALIGN_CENTER, 0, side / 8 + 6);
  } else if (icon) {
    icon_ = makeLabel(card, ui::font::small(), ui::color::dim(), LV_SYMBOL_VOLUME_MAX);
    lv_obj_align(icon_, LV_ALIGN_CENTER, 0, side / 8 + 6);
  }
}

uint32_t VolumeWidget::interests() const { return ui::kChangedVolume; }

void VolumeWidget::refresh(const Model& m, uint32_t, uint32_t) {
  const bool app = spec_.appTarget;
  const bool valid = m.volume.valid && (!app || m.volume.appValid);
  if (name_) setText(name_, !m.volume.valid ? "" : (m.volume.appValid ? m.volume.appName : "No media app"));
  if (!valid) {
    // Offline the PC's volume is unknown (the knob still sends volume keys); "app" also needs a
    // media app that is playing through Windows' mixer.
    lv_arc_set_value(arc_, 0);
    setText(value_, "--");
    if (icon_) setText(icon_, LV_SYMBOL_VOLUME_MAX);
    return;
  }
  const uint8_t level = app ? m.volume.appLevel : m.volume.level;
  const bool muted = app ? m.volume.appMuted : m.volume.muted;
  lv_arc_set_value(arc_, level);
  lv_obj_set_style_arc_color(arc_, muted ? ui::color::dim() : ui::color::accent(), LV_PART_INDICATOR);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", level);
  setText(value_, buf);
  if (icon_) setText(icon_, muted ? LV_SYMBOL_MUTE : (level < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
}

}  // namespace widgets
}  // namespace rp
