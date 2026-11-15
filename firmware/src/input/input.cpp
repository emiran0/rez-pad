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
constexpr uint32_t kQuietAfterLearnMs = 800;
constexpr uint32_t kSettleMs = 150;      // count unchanged this long = the knob rests in a click

struct EncoderHw {
  pcnt_unit_handle_t unit = nullptr;
  int8_t pinA, pinB, pinSw;
};

// Physical ports.
EncoderHw gEnc[kMaxKnobs] = {{nullptr, pins::kEnc1A, pins::kEnc1B, pins::kEnc1Sw},
                             {nullptr, pins::kEnc2A, pins::kEnc2B, pins::kEnc2Sw},
                             {nullptr, pins::kEnc3A, pins::kEnc3B, pins::kEnc3Sw}};
const int8_t kBtnPins[kMaxButtons] = {pins::kBtn1, pins::kBtn2, pins::kBtn3, pins::kBtn4};

Config gCfg;
// Port -> number (0-based), -1 = no number. Built from gCfg by applyConfigLocked().
int8_t gKnobOf[kMaxKnobs] = {0, 1, -1};
int8_t gButtonOf[kMaxButtons] = {-1, -1, -1, -1};

DetentAccumulator gDetent[kMaxKnobs];
GestureDetector gEncSw[kMaxKnobs];
GestureDetector gBtn[kMaxButtons];
bool gEncSwPrev[kMaxKnobs] = {};
bool gBtnPrev[kMaxButtons] = {};
QueueHandle_t gQueue = nullptr;
esp_timer_handle_t gTimer = nullptr;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

// Written by the tick, read by snapshot().
volatile int32_t gRaw[kMaxKnobs] = {};
volatile int32_t gDetentTotal[kMaxKnobs] = {};
volatile bool gSwDown[kMaxKnobs] = {};
volatile bool gBtnDown[kMaxButtons] = {};
volatile uint32_t gDropped = 0;
volatile bool gConfigDirty = false;
uint32_t gLastCountMs[kMaxKnobs] = {};
bool gSettled[kMaxKnobs] = {};

// Learn mode (set by startLearn from the link task, run by the tick).
volatile bool gLearning = false;
volatile uint32_t gLearnUntil = 0;
volatile uint32_t gQuietUntil = 0;
bool gLearnedReady = false;
Learned gLearned;

constexpr int kTraceLen = 256;
TraceEntry gTrace[kTraceLen];
int gTraceHead = 0;   // next write
int gTraceCount = 0;

void trace(uint32_t now, int p, int32_t count, int steps, bool snapped) {
  TraceEntry& t = gTrace[gTraceHead];
  t.ms = now;
  t.port = static_cast<uint8_t>(p);
  t.level = static_cast<uint8_t>((gpio_get_level(static_cast<gpio_num_t>(gEnc[p].pinA)) ? 1 : 0) |
                                 (gpio_get_level(static_cast<gpio_num_t>(gEnc[p].pinB)) ? 2 : 0) |
                                 (snapped ? 4 : 0));
  t.steps = static_cast<int8_t>(steps);
  t.count = count;
  gTraceHead = (gTraceHead + 1) % kTraceLen;
  if (gTraceCount < kTraceLen) ++gTraceCount;
}

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

  // KY-040 has pull-ups to its + pin; internal ones also help if a module lacks them (and keep an
  // empty port quiet).
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
  for (int p = 0; p < kMaxKnobs; ++p) gKnobOf[p] = -1;
  for (int p = 0; p < kMaxButtons; ++p) gButtonOf[p] = -1;
  for (int n = 0; n < kMaxKnobs; ++n) {
    const int port = gCfg.knobPorts[n];
    if (port >= 1 && port <= kMaxKnobs && gKnobOf[port - 1] < 0) gKnobOf[port - 1] = static_cast<int8_t>(n);
  }
  for (int n = 0; n < kMaxButtons; ++n) {
    const int port = gCfg.buttonPorts[n];
    if (port >= 1 && port <= kMaxButtons && gButtonOf[port - 1] < 0) gButtonOf[port - 1] = static_cast<int8_t>(n);
  }
  // Each port takes the settings of the number it has (detents and direction belong to the knob
  // number in the config); a port without a number counts with the defaults (learn mode only).
  for (int p = 0; p < kMaxKnobs; ++p) {
    const EncoderConfig ec = gKnobOf[p] >= 0 ? gCfg.enc[gKnobOf[p]] : EncoderConfig();
    gDetent[p].configure(ec.countsPerDetent, ec.invert);
    gDetent[p].reset(gRaw[p]);
    GestureConfig gc;
    gc.doubleEnabled = gKnobOf[p] >= 0 && ec.doubleEnabled;
    gEncSw[p].setConfig(gc);
  }
  for (int p = 0; p < kMaxButtons; ++p) {
    GestureConfig bc;
    bc.doubleEnabled = gButtonOf[p] >= 0 && gCfg.btnDoubleEnabled[gButtonOf[p]];
    gBtn[p].setConfig(bc);
  }
}

bool due(uint32_t now, uint32_t until) { return static_cast<int32_t>(now - until) >= 0; }

void finishLearn(bool knob, uint8_t port, uint32_t now) {
  taskENTER_CRITICAL(&gLock);
  gLearned.knob = knob;
  gLearned.port = port;
  gLearnedReady = true;
  gLearning = false;
  taskEXIT_CRITICAL(&gLock);
  gQuietUntil = now + kQuietAfterLearnMs;
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
  if (gLearning && due(now, gLearnUntil)) finishLearn(false, 0, now);
  const bool learn = gLearning;
  const bool quiet = !learn && !due(now, gQuietUntil);

  for (int p = 0; p < kMaxKnobs; ++p) {
    int count = 0;
    int steps = 0;
    if (gEnc[p].unit && pcnt_unit_get_count(gEnc[p].unit, &count) == ESP_OK) {
      const bool changed = count != gRaw[p];
      gRaw[p] = count;
      steps = gDetent[p].update(count);
      bool snapped = false;
      if (changed) {
        gLastCountMs[p] = now;
        gSettled[p] = false;
      } else if (!gSettled[p] && now - gLastCountMs[p] >= kSettleMs) {
        gSettled[p] = true;
        const int32_t before = gDetent[p].pending(count);
        steps += gDetent[p].settle(count);
        snapped = gDetent[p].pending(count) != before;
      }
      if (steps != 0) gDetentTotal[p] = gDetentTotal[p] + steps;
      if (changed || snapped) {
        taskENTER_CRITICAL(&gLock);
        trace(now, p, count, steps, snapped);
        taskEXIT_CRITICAL(&gLock);
      }
    }
    const bool held = gEncSw[p].pressed();
    const bool sw = digitalRead(gEnc[p].pinSw) == LOW;
    const Gesture g = gEncSw[p].update(sw, now);
    const bool down = gEncSw[p].pressed();
    const bool pressEdge = down && !gEncSwPrev[p];
    gEncSwPrev[p] = down;
    gSwDown[p] = down;

    if (learn) {
      if (steps != 0 || pressEdge) {
        if (down) gEncSw[p].cancelPress();  // this press only picked the knob
        finishLearn(true, static_cast<uint8_t>(p + 1), now);
      }
      continue;
    }
    if (quiet) {
      if (down) gEncSw[p].cancelPress();
      continue;
    }
    const int n = gKnobOf[p];
    if (n < 0) continue;
    const Source src = static_cast<Source>(static_cast<int>(Source::kEnc1) + n);
    if (steps != 0) {
      if (held) gEncSw[p].cancelPress();  // this press is a modifier now, not a click
      push(src, held ? Kind::kRotatePressed : Kind::kRotate, static_cast<int16_t>(steps), now);
    }
    pushGesture(src, g, now);
  }

  for (int p = 0; p < kMaxButtons; ++p) {
    const bool raw = digitalRead(kBtnPins[p]) == LOW;
    const Gesture g = gBtn[p].update(raw, now);
    const bool down = gBtn[p].pressed();
    const bool pressEdge = down && !gBtnPrev[p];
    gBtnPrev[p] = down;
    gBtnDown[p] = down;
    if (learn) {
      if (pressEdge) {
        gBtn[p].cancelPress();
        finishLearn(false, static_cast<uint8_t>(p + 1), now);
      }
      continue;
    }
    if (quiet) {
      if (down) gBtn[p].cancelPress();
      continue;
    }
    const int n = gButtonOf[p];
    if (n < 0) continue;
    pushGesture(static_cast<Source>(static_cast<int>(Source::kBtn1) + n), g, now);
  }
}

}  // namespace

const char* sourceName(Source s) {
  switch (s) {
    case Source::kEnc1: return "enc1";
    case Source::kEnc2: return "enc2";
    case Source::kEnc3: return "enc3";
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

void startLearn(uint32_t timeoutMs) {
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  taskENTER_CRITICAL(&gLock);
  gLearnedReady = false;
  gLearnUntil = now + timeoutMs;
  gLearning = timeoutMs > 0;
  taskEXIT_CRITICAL(&gLock);
}

bool learning() { return gLearning; }

bool takeLearned(Learned& out) {
  taskENTER_CRITICAL(&gLock);
  const bool ready = gLearnedReady;
  if (ready) {
    out = gLearned;
    gLearnedReady = false;
  }
  taskEXIT_CRITICAL(&gLock);
  return ready;
}

Snapshot snapshot() {
  Snapshot s;
  for (int i = 0; i < kMaxKnobs; ++i) {
    s.rawCount[i] = gRaw[i];
    s.detents[i] = gDetentTotal[i];
    s.switchDown[i] = gSwDown[i];
  }
  for (int i = 0; i < kMaxButtons; ++i) s.buttonDown[i] = gBtnDown[i];
  s.dropped = gDropped;
  return s;
}

int takeTrace(TraceEntry* out, int max) {
  taskENTER_CRITICAL(&gLock);
  const int n = gTraceCount < max ? gTraceCount : max;
  const int start = (gTraceHead - gTraceCount + kTraceLen) % kTraceLen;
  for (int i = 0; i < n; ++i) out[i] = gTrace[(start + i) % kTraceLen];
  gTraceCount = 0;
  taskEXIT_CRITICAL(&gLock);
  return n;
}

}  // namespace input
}  // namespace rp
