#include "input.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "pins.h"

namespace rp {
namespace input {

namespace {

constexpr int kPcntLimit = 10000;        // wraps are accumulated (accum_count)
constexpr uint32_t kGlitchNs = 1000;     // PCNT glitch filter
constexpr uint64_t kTickUs = 1000;       // switch sampling period
constexpr UBaseType_t kQueueLen = 32;

struct EncoderHw {
  pcnt_unit_handle_t unit = nullptr;
  int8_t pinA, pinB, pinSw;
};

EncoderHw gEnc[2] = {{nullptr, pins::kEnc1A, pins::kEnc1B, pins::kEnc1Sw},
                     {nullptr, pins::kEnc2A, pins::kEnc2B, pins::kEnc2Sw}};
const int8_t kBtnPins[kMaxButtons] = {pins::kBtn1, pins::kBtn2, pins::kBtn3, pins::kBtn4};
const Source kBtnSources[kMaxButtons] = {Source::kBtn1, Source::kBtn2, Source::kBtn3, Source::kBtn4};

Config gCfg;
DetentAccumulator gDetent[2];
GestureDetector gEncSw[2];
GestureDetector gBtn[kMaxButtons];
QueueHandle_t gQueue = nullptr;
esp_timer_handle_t gTimer = nullptr;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

// Written by the tick, read by snapshot().
volatile int32_t gRaw[2] = {0, 0};
volatile int32_t gDetentTotal[2] = {0, 0};
volatile bool gSwDown[2] = {false, false};
volatile bool gBtnDown[kMaxButtons] = {};
volatile uint32_t gDropped = 0;
volatile bool gConfigDirty = false;

bool setupEncoder(EncoderHw& e) {
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -kPcntLimit;
  ucfg.high_limit = kPcntLimit;
  ucfg.flags.accum_count = 1;
  if (pcnt_new_unit(&ucfg, &e.unit) != ESP_OK) return false;

  pcnt_glitch_filter_config_t fcfg = {};
  fcfg.max_glitch_ns = kGlitchNs;
  pcnt_unit_set_glitch_filter(e.unit, &fcfg);

  // Full quadrature (x4): each channel counts edges of one signal, direction from the other.
  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = e.pinA;
  ca.level_gpio_num = e.pinB;
  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = e.pinB;
  cb.level_gpio_num = e.pinA;
  pcnt_channel_handle_t chA = nullptr;
  pcnt_channel_handle_t chB = nullptr;
  if (pcnt_new_channel(e.unit, &ca, &chA) != ESP_OK) return false;
  if (pcnt_new_channel(e.unit, &cb, &chB) != ESP_OK) return false;
  pcnt_channel_set_edge_action(chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  // accum_count needs the limits as watch points.
  pcnt_unit_add_watch_point(e.unit, kPcntLimit);
  pcnt_unit_add_watch_point(e.unit, -kPcntLimit);

  // KY-040 has pull-ups to its + pin; internal ones also help if a module lacks them.
  gpio_set_pull_mode(static_cast<gpio_num_t>(e.pinA), GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(static_cast<gpio_num_t>(e.pinB), GPIO_PULLUP_ONLY);

  if (pcnt_unit_enable(e.unit) != ESP_OK) return false;
  pcnt_unit_clear_count(e.unit);
  return pcnt_unit_start(e.unit) == ESP_OK;
}

void push(Source s, Kind k, int16_t v, uint32_t now) {
  const Event e{s, k, v, now};
  if (xQueueSend(gQueue, &e, 0) != pdTRUE) gDropped = gDropped + 1;
}

void pushGesture(Source s, Gesture g, uint32_t now) {
  switch (g) {
    case Gesture::kClick: push(s, Kind::kClick, 0, now); break;
    case Gesture::kDouble: push(s, Kind::kDouble, 0, now); break;
    case Gesture::kLong: push(s, Kind::kLong, 0, now); break;
    default: break;
  }
}

void applyConfigLocked() {
  for (int i = 0; i < 2; ++i) {
    gDetent[i].configure(gCfg.enc[i].countsPerDetent, gCfg.enc[i].invert);
    gDetent[i].reset(gRaw[i]);
    GestureConfig gc;
    gc.doubleEnabled = gCfg.enc[i].doubleEnabled;
    gEncSw[i].setConfig(gc);
  }
  for (int i = 0; i < kMaxButtons; ++i) {
    GestureConfig bc;
    bc.doubleEnabled = gCfg.btnDoubleEnabled[i];
    gBtn[i].setConfig(bc);
  }
}

// Runs every 1 ms in the esp_timer task.
void tick(void*) {
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (gConfigDirty) {
    taskENTER_CRITICAL(&gLock);
    applyConfigLocked();
    gConfigDirty = false;
    taskEXIT_CRITICAL(&gLock);
  }

  for (int i = 0; i < 2; ++i) {
    int count = 0;
    if (gEnc[i].unit && pcnt_unit_get_count(gEnc[i].unit, &count) == ESP_OK) {
      gRaw[i] = count;
      const int steps = gDetent[i].update(count);
      if (steps != 0) {
        gDetentTotal[i] = gDetentTotal[i] + steps;
        const bool held = gEncSw[i].pressed();
        if (held) gEncSw[i].cancelPress();  // this press is a modifier now, not a click
        push(i == 0 ? Source::kEnc1 : Source::kEnc2, held ? Kind::kRotatePressed : Kind::kRotate,
             static_cast<int16_t>(steps), now);
      }
    }
    const bool sw = digitalRead(gEnc[i].pinSw) == LOW;
    pushGesture(i == 0 ? Source::kEnc1 : Source::kEnc2, gEncSw[i].update(sw, now), now);
    gSwDown[i] = gEncSw[i].pressed();
  }

  for (int i = 0; i < kMaxButtons; ++i) {
    if (i >= gCfg.buttons) {
      gBtnDown[i] = false;
      continue;
    }
    const bool down = digitalRead(kBtnPins[i]) == LOW;
    pushGesture(kBtnSources[i], gBtn[i].update(down, now), now);
    gBtnDown[i] = gBtn[i].pressed();
  }
}

}  // namespace

const char* sourceName(Source s) {
  switch (s) {
    case Source::kEnc1: return "enc1";
    case Source::kEnc2: return "enc2";
    case Source::kBtn1: return "btn1";
    case Source::kBtn2: return "btn2";
    case Source::kBtn3: return "btn3";
    case Source::kBtn4: return "btn4";
  }
  return "?";
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::kRotate: return "rotate";
    case Kind::kRotatePressed: return "press+rotate";
    case Kind::kClick: return "click";
    case Kind::kDouble: return "double";
    case Kind::kLong: return "long";
  }
  return "?";
}

bool begin(const Config& cfg) {
  gCfg = cfg;
  gQueue = xQueueCreate(kQueueLen, sizeof(Event));
  if (!gQueue) return false;

  bool ok = true;
  for (auto& e : gEnc) {
    pinMode(e.pinSw, INPUT_PULLUP);
    ok = setupEncoder(e) && ok;
  }
  for (int8_t p : kBtnPins) pinMode(p, INPUT_PULLUP);
  applyConfigLocked();

  esp_timer_create_args_t args = {};
  args.callback = &tick;
  args.name = "input";
  args.dispatch_method = ESP_TIMER_TASK;
  if (esp_timer_create(&args, &gTimer) != ESP_OK) return false;
  return esp_timer_start_periodic(gTimer, kTickUs) == ESP_OK && ok;
}

void setConfig(const Config& cfg) {
  taskENTER_CRITICAL(&gLock);
  gCfg = cfg;
  gConfigDirty = true;
  taskEXIT_CRITICAL(&gLock);
}

const Config& config() { return gCfg; }

bool poll(Event& e) { return gQueue && xQueueReceive(gQueue, &e, 0) == pdTRUE; }

Snapshot snapshot() {
  Snapshot s;
  for (int i = 0; i < 2; ++i) {
    s.rawCount[i] = gRaw[i];
    s.detents[i] = gDetentTotal[i];
    s.switchDown[i] = gSwDown[i];
  }
  for (int i = 0; i < kMaxButtons; ++i) s.buttonDown[i] = gBtnDown[i];
  s.dropped = gDropped;
  return s;
}

}  // namespace input
}  // namespace rp
