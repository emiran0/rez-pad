// Now Playing widgets: album_art, track_info, media_controls, progress, volume.
#include "common.h"
#include "media/media.h"
#include "media_widgets.h"

namespace rp {
namespace widgets {

using ui::Changed;
using ui::Model;
using ui::PlayState;

// ---- album_art ----
// Album art from the host (decoded by the media module). Until it arrives, or for tracks without
// art, a placeholder square tinted by the track.

namespace {
// After a track change keep the previous image this long while the new one is on its way, so the
// card does not flash to the placeholder for a few hundred ms.
constexpr uint32_t kArtGraceMs = 1000;
}  // namespace

void AlbumArt::build(lv_obj_t* card, int w, int h) {
  // Use the card's padding too (7 px each side), keeping a 2 px margin inside the border.
  const int side = (w < h ? w : h) + 14 - 4;
  media::setArtSize(side);
  box_ = lv_obj_create(card);
  lv_obj_remove_style_all(box_);
  lv_obj_set_size(box_, side, side);
  lv_obj_center(box_);
  lv_obj_set_style_radius(box_, 6, 0);
  lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(box_, LV_GRAD_DIR_VER, 0);
  icon_ = makeLabel(box_, ui::font::large(), ui::color::text(), LV_SYMBOL_AUDIO);
  lv_obj_center(icon_);

  img_ = lv_image_create(card);
  lv_obj_set_size(img_, side, side);
  lv_obj_center(img_);
  lv_image_set_inner_align(img_, LV_IMAGE_ALIGN_CENTER);  // a different size is centered and cropped
  lv_obj_set_style_radius(img_, 6, 0);
  lv_obj_set_style_clip_corner(img_, true, 0);
  lv_obj_set_hidden(img_, true);
}

uint32_t AlbumArt::interests() const { return ui::kChangedMedia | ui::kChangedArt | ui::kChangedTick; }

void AlbumArt::showPlaceholder(const Model& m) {
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

void AlbumArt::refresh(const Model& m, uint32_t, uint32_t nowMs) {
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
  lv_obj_set_hidden(img_, false);
  lv_obj_set_hidden(box_, true);
  shownPx_ = m.art.px;
  shownId_ = m.art.id;
  tint_ = 0xFFFFFFFFu;
}

// ---- track_info ----

void TrackInfo::build(lv_obj_t* card, int w, int h) {
  const bool tall = h >= 150;
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, tall ? 6 : 2, 0);
  title_ = makeMarquee(card, tall ? ui::font::large() : ui::font::body(), ui::color::text(), w);
  // Only the title scrolls; scrolling text redraws every frame over SPI, so keep it to one label.
  artist_ = makeEllipsis(card, tall ? ui::font::body() : ui::font::small(), ui::color::text(), w);
  if (tall) album_ = makeEllipsis(card, ui::font::small(), ui::color::dim(), w);
  app_ = makeLabel(card, ui::font::small(), ui::color::accent());
}

uint32_t TrackInfo::interests() const { return ui::kChangedMedia | ui::kChangedHost; }

void TrackInfo::refresh(const Model& m, uint32_t, uint32_t) {
  if (m.np.state == PlayState::kNone) {
    setText(title_, m.hostConnected ? "Nothing playing" : "No host connected");
    setText(artist_, "");
    if (album_) setText(album_, "");
    setText(app_, "");
    return;
  }
  setText(title_, m.np.title);
  setText(artist_, m.np.artist);
  if (album_) setText(album_, m.np.album);
  setText(app_, m.np.app);
}

// ---- media_controls / progress ----

void MediaControls::build(lv_obj_t* card, int w, int h) {
  (void)h;
  if (withButtons_) {
    lv_obj_t* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, w, 34);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    prev_ = makeLabel(row, ui::font::body(), ui::color::text(), LV_SYMBOL_PREV);
    play_ = makeLabel(row, ui::font::large(), ui::color::accent(), LV_SYMBOL_PLAY);
    next_ = makeLabel(row, ui::font::body(), ui::color::text(), LV_SYMBOL_NEXT);
  }
  bar_ = lv_bar_create(card);
  lv_obj_set_size(bar_, w, 6);
  lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_bar_set_range(bar_, 0, 1000);
  lv_obj_set_style_bg_color(bar_, ui::color::faint(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar_, ui::color::accent(), LV_PART_INDICATOR);
  lv_obj_set_style_anim_duration(bar_, 0, 0);
  elapsed_ = makeLabel(card, ui::font::small(), ui::color::dim(), "0:00");
  lv_obj_align(elapsed_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  duration_ = makeLabel(card, ui::font::small(), ui::color::dim(), "0:00");
  lv_obj_align(duration_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

uint32_t MediaControls::interests() const { return ui::kChangedMedia | ui::kChangedTick; }

void MediaControls::refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  const auto& np = m.np;
  if (withButtons_ && (changed & ui::kChangedMedia)) {
    const bool none = np.state == PlayState::kNone;
    setText(play_, np.state == PlayState::kPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(play_, none ? ui::color::faint() : ui::color::accent(), 0);
    lv_obj_set_style_text_color(prev_, np.canPrev ? ui::color::text() : ui::color::faint(), 0);
    lv_obj_set_style_text_color(next_, np.canNext ? ui::color::text() : ui::color::faint(), 0);
  }
  const uint32_t pos = np.positionAt(nowMs);
  const int32_t permille = np.durMs ? static_cast<int32_t>(static_cast<uint64_t>(pos) * 1000 / np.durMs) : 0;
  if (permille != lv_bar_get_value(bar_)) lv_bar_set_value(bar_, permille, LV_ANIM_OFF);
  char buf[16];
  fmtDuration(buf, sizeof(buf), pos);
  setText(elapsed_, np.state == PlayState::kNone ? "" : buf);
  fmtDuration(buf, sizeof(buf), np.durMs);
  setText(duration_, np.state == PlayState::kNone ? "" : buf);
}

// ---- volume ----

void VolumeWidget::build(lv_obj_t* card, int w, int h) {
  const int side = (w < h ? w : h);
  arc_ = lv_arc_create(card);
  lv_obj_set_size(arc_, side, side);
  lv_obj_center(arc_);
  lv_arc_set_rotation(arc_, 135);
  lv_arc_set_bg_angles(arc_, 0, 270);
  lv_arc_set_range(arc_, 0, 100);
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);
  lv_obj_set_style_arc_width(arc_, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, ui::color::faint(), LV_PART_MAIN);
  value_ = makeLabel(card, ui::font::body(), ui::color::text(), "0");
  lv_obj_align(value_, LV_ALIGN_CENTER, 0, -4);
  icon_ = makeLabel(card, ui::font::small(), ui::color::dim(), LV_SYMBOL_VOLUME_MAX);
  lv_obj_align(icon_, LV_ALIGN_CENTER, 0, 16);
}

uint32_t VolumeWidget::interests() const { return ui::kChangedVolume; }

void VolumeWidget::refresh(const Model& m, uint32_t, uint32_t) {
  if (!m.volume.valid) {
    // Offline the PC's volume is unknown; the knob still sends volume keys.
    lv_arc_set_value(arc_, 0);
    setText(value_, "--");
    setText(icon_, LV_SYMBOL_VOLUME_MAX);
    return;
  }
  lv_arc_set_value(arc_, m.volume.level);
  lv_obj_set_style_arc_color(arc_, m.volume.muted ? ui::color::dim() : ui::color::accent(), LV_PART_INDICATOR);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", m.volume.level);
  setText(value_, buf);
  setText(icon_, m.volume.muted ? LV_SYMBOL_MUTE : (m.volume.level < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
}

}  // namespace widgets
}  // namespace rp
