#include "media_keys.h"

#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "link/usb_device.h"

namespace rp {
namespace hid {

namespace {

constexpr int kMaxRepeat = 20;  // cap for one volume turn (each key = one Windows volume step)

struct KeyJob {
  bool keyboard;   // false: consumer usage, true: keyboard shortcut
  uint16_t usage;  // consumer usage, or keyboard usage
  uint8_t mods;    // keyboard modifier bits
  uint8_t repeat;
};

constexpr uint32_t kKeyHoldMs = 15;  // between press and release, so every PC registers the shortcut

USBHIDConsumerControl gConsumer;
USBHIDKeyboard gKeyboard;
QueueHandle_t gQueue = nullptr;
volatile uint32_t gSent = 0;
volatile uint32_t gDropped = 0;

void task(void*) {
  KeyJob job;
  for (;;) {
    if (xQueueReceive(gQueue, &job, portMAX_DELAY) != pdTRUE) continue;
    if (job.keyboard) {
      if (!usb::mounted()) continue;
      KeyReport down = {};
      down.modifiers = job.mods;
      down.keys[0] = static_cast<uint8_t>(job.usage);
      gKeyboard.sendReport(&down);
      vTaskDelay(pdMS_TO_TICKS(kKeyHoldMs));
      KeyReport up = {};
      gKeyboard.sendReport(&up);
      gSent = gSent + 1;
      continue;
    }
    for (uint8_t i = 0; i < job.repeat; ++i) {
      if (!usb::mounted()) break;
      // press() and release() each send a report and wait for the previous one to finish.
      gConsumer.press(job.usage);
      gConsumer.release();
      gSent = gSent + 1;
    }
  }
}

bool queue(uint16_t usage, int repeat) {
  if (!gQueue || !usb::mounted()) return false;
  if (repeat < 1) repeat = 1;
  if (repeat > kMaxRepeat) repeat = kMaxRepeat;
  const KeyJob job{false, usage, 0, static_cast<uint8_t>(repeat)};
  if (xQueueSend(gQueue, &job, 0) != pdTRUE) {
    gDropped = gDropped + 1;
    return false;
  }
  return true;
}

}  // namespace

void begin() {
  gConsumer.begin();
  gKeyboard.begin();
  gQueue = xQueueCreate(8, sizeof(KeyJob));
  xTaskCreatePinnedToCore(task, "hid", 3072, nullptr, 3, nullptr, 0);
}

bool sendMediaAction(actions::Action a, int value) {
  using actions::Action;
  switch (a) {
    case Action::kPlayPause: return queue(CONSUMER_CONTROL_PLAY_PAUSE, 1);
    case Action::kNext: return queue(CONSUMER_CONTROL_SCAN_NEXT, 1);
    case Action::kPrev: return queue(CONSUMER_CONTROL_SCAN_PREVIOUS, 1);
    case Action::kMute: return queue(CONSUMER_CONTROL_MUTE, 1);
    case Action::kVolume:
      if (value == 0) return false;
      return queue(value > 0 ? CONSUMER_CONTROL_VOLUME_INCREMENT : CONSUMER_CONTROL_VOLUME_DECREMENT,
                   value > 0 ? value : -value);
    default:
      return false;  // media.seek has no reliable consumer key; ignored offline
  }
}

bool sendKeys(uint8_t mods, uint8_t key) {
  if (!gQueue || !usb::mounted() || (mods == 0 && key == 0)) return false;
  const KeyJob job{true, key, mods, 1};
  if (xQueueSend(gQueue, &job, 0) != pdTRUE) {
    gDropped = gDropped + 1;
    return false;
  }
  return true;
}

uint32_t sentCount() { return gSent; }
uint32_t droppedCount() { return gDropped; }

}  // namespace hid
}  // namespace rp
