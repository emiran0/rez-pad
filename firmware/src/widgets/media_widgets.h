#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// Building blocks shared by the media widgets. Each fills a box (x, y, w, h) inside the card's
// content area and adapts what it shows to that size.

// Album art, or a placeholder tinted by the track.
class ArtView {
 public:
  // `setsArtSize`: this view asks the host for art of its size (layout widgets do; the knob popup
  // only shows what the layout already gets).
  void build(lv_obj_t* parent, int x, int y, int side, bool setsArtSize = true);
  void refresh(const ui::Model& m, uint32_t nowMs);

 private:
  void showPlaceholder(const ui::Model& m);
  int side_ = 0;
  lv_obj_t* box_ = nullptr;   // placeholder: gradient tinted by the track, music icon
  lv_obj_t* icon_ = nullptr;
  lv_obj_t* img_ = nullptr;   // decoded album art
  lv_image_dsc_t dsc_ = {};
  const uint16_t* shownPx_ = nullptr;  // art currently on screen (nullptr = placeholder)
  uint32_t shownId_ = 0;
  uint32_t tint_ = 0xFFFFFFFFu;        // track the placeholder is tinted for
};

// Title (scrolling), artist, album and app, as many lines as fit.
class TextView {
 public:
  void build(lv_obj_t* parent, int x, int y, int w, int h, bool center = false);
  void refresh(const ui::Model& m);

 private:
  lv_obj_t* title_ = nullptr;
  lv_obj_t* artist_ = nullptr;
  lv_obj_t* album_ = nullptr;
  lv_obj_t* app_ = nullptr;
};

// Previous / play / next buttons and the progress bar with times, as much as fits.
class ControlsView {
 public:
  void build(lv_obj_t* parent, int x, int y, int w, int h, bool withButtons);
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs);
  bool hasBar() const { return bar_ != nullptr; }

 private:
  lv_obj_t* prev_ = nullptr;
  lv_obj_t* play_ = nullptr;
  lv_obj_t* next_ = nullptr;
  lv_obj_t* bar_ = nullptr;
  lv_obj_t* elapsed_ = nullptr;
  lv_obj_t* duration_ = nullptr;
};

class AlbumArt : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  ArtView art_;
};

class TrackInfo : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  TextView text_;
};

// media_controls (buttons + progress) and progress (progress only).
class MediaControls : public Widget {
 public:
  MediaControls(const Spec& s, bool withButtons) : Widget(s), withButtons_(withButtons) {}
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t shows() const override { return controls_.hasBar() ? kShowsSeek : 0; }
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool withButtons_;
  ControlsView controls_;
};

// now_playing: album art, track text and controls in one card. Art on the left when the card is
// wide, on top when it is tall.
class NowPlayingWidget : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t shows() const override { return controls_.hasBar() ? kShowsSeek : 0; }
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool hasArt_ = false;
  ArtView art_;
  TextView text_;
  ControlsView controls_;
};

class VolumeWidget : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t shows() const override { return spec_.appTarget ? kShowsAppVolume : kShowsVolume; }
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* arc_ = nullptr;
  lv_obj_t* value_ = nullptr;
  lv_obj_t* icon_ = nullptr;
  lv_obj_t* name_ = nullptr;  // target "app" from 90 px: the app's name instead of the icon
};

}  // namespace widgets
}  // namespace rp
