#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

class WeatherNow : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedWeather; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool tall_ = false;
  lv_obj_t* loc_ = nullptr;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* cond_ = nullptr;
  lv_obj_t* details_ = nullptr;
};

class WeatherForecast : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedWeather; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  static constexpr int kCols = 5;
  lv_obj_t* day_[kCols] = {};
  lv_obj_t* cond_[kCols] = {};
  lv_obj_t* temps_[kCols] = {};
};

// Room temperature, humidity, pressure and trend from the device's own sensor.
class RoomClimate : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedRoom; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool tall_ = false;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* hum_ = nullptr;
  lv_obj_t* press_ = nullptr;
  lv_obj_t* trend_ = nullptr;
};

}  // namespace widgets
}  // namespace rp
