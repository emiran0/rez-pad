#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

class WeatherNow : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedWeather | ui::kChangedClock; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool tall_ = false;
  int iconPx_ = 48;
  lv_obj_t* loc_ = nullptr;
  lv_obj_t* age_ = nullptr;
  lv_obj_t* icon_ = nullptr;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* cond_ = nullptr;
  lv_obj_t* details_ = nullptr;
};

// mode "daily" (default): today and the next 4 days. mode "hourly": every 2 h from the next hour.
class WeatherForecast : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedWeather | ui::kChangedClock; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  static constexpr int kCols = 5;
  bool hourly_ = false;
  lv_obj_t* label_[kCols] = {};
  lv_obj_t* icon_[kCols] = {};
  lv_obj_t* temps_[kCols] = {};
};

// Room temperature, humidity, pressure and trend from the device's own sensor.
class RoomClimate : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedRoom | ui::kChangedWeather; }  // units come with WEATHER
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool tall_ = false;
  lv_obj_t* temp_ = nullptr;
  lv_obj_t* hum_ = nullptr;
  lv_obj_t* press_ = nullptr;
  lv_obj_t* trend_ = nullptr;
};

// Local time and date (1x1, 2x1).
class Clock : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedClock; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  bool wide_ = false;
  lv_obj_t* time_ = nullptr;
  lv_obj_t* date_ = nullptr;
};

}  // namespace widgets
}  // namespace rp
