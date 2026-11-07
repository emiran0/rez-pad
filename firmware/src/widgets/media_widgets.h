#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

class AlbumArt : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* box_ = nullptr;
  lv_obj_t* icon_ = nullptr;
  uint32_t shownTrack_ = 0xFFFFFFFFu;
};

class TrackInfo : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* title_ = nullptr;
  lv_obj_t* artist_ = nullptr;
  lv_obj_t* album_ = nullptr;
  lv_obj_t* app_ = nullptr;
};

// media_controls (buttons + progress) and progress (progress only).
class MediaControls : public Widget {
 public:
  MediaControls(const Spec& s, bool withButtons) : Widget(s), withButtons_(withButtons) {}
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool withButtons_;
  lv_obj_t* prev_ = nullptr;
  lv_obj_t* play_ = nullptr;
  lv_obj_t* next_ = nullptr;
  lv_obj_t* bar_ = nullptr;
  lv_obj_t* elapsed_ = nullptr;
  lv_obj_t* duration_ = nullptr;
};

class VolumeWidget : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* arc_ = nullptr;
  lv_obj_t* value_ = nullptr;
  lv_obj_t* icon_ = nullptr;
};

}  // namespace widgets
}  // namespace rp
