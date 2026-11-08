#include "image_widget.h"

#include <Arduino.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <new>

#include "asset_format.h"
#include "assets/assets.h"
#include "common.h"

namespace rp {
namespace widgets {

namespace {

int gMissing = 0;
size_t gPreDecodedTotal = 0;  // bytes of pre-decoded frames over all image widgets
JPEGDEC* gJpeg = nullptr;  // UI task only (the media module has its own on the link task)

struct Target {
  uint16_t* px;
  int w, h;
};

int drawBlock(JPEGDRAW* d) {
  const auto* t = static_cast<const Target*>(d->pUser);
  if (d->x >= t->w || d->y >= t->h) return 1;
  const int cols = d->x + d->iWidth > t->w ? t->w - d->x : d->iWidth;
  const int rows = d->y + d->iHeight > t->h ? t->h - d->y : d->iHeight;
  for (int r = 0; r < rows; ++r) {
    memcpy(t->px + (d->y + r) * t->w + d->x, d->pPixels + r * d->iWidth, cols * sizeof(uint16_t));
  }
  return 1;
}

bool decodeFrame(const proto::RpaFrame& f, uint16_t* px, int w, int h) {
  if (!gJpeg) {
    void* mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) return false;
    gJpeg = new (mem) JPEGDEC();
  }
  if (!gJpeg->openRAM(const_cast<uint8_t*>(f.jpeg), static_cast<int>(f.len), drawBlock)) return false;
  bool ok = gJpeg->getWidth() == w && gJpeg->getHeight() == h;
  if (ok) {
    Target t{px, w, h};
    gJpeg->setUserPointer(&t);
    gJpeg->setPixelType(RGB565_LITTLE_ENDIAN);
    ok = gJpeg->decode(0, 0, 0) != 0;
  }
  gJpeg->close();
  return ok;
}

}  // namespace

int ImageWidget::missing() { return gMissing; }
void ImageWidget::resetMissing() { gMissing = 0; }

ImageWidget::~ImageWidget() {
  if (timer_) lv_timer_delete(timer_);
  if (pixels_) heap_caps_free(pixels_);
  if (delays_) heap_caps_free(delays_);
  if (jpegs_) heap_caps_free(jpegs_);
  if (file_) heap_caps_free(file_);
  gPreDecodedTotal -= preDecoded_;
}

void ImageWidget::build(lv_obj_t* card, int w, int h) {
  char name[proto::kAssetNameMax];
  uint8_t* file = nullptr;
  size_t len = 0;
  if (assets::nameFor(spec_, w, h, ui::palette().card, name, sizeof(name))) len = assets::load(name, &file);
  proto::RpaReader r;
  if (len == 0 || !r.open(file, len) || r.width() > w || r.height() > h) {
    if (file) heap_caps_free(file);
    ++gMissing;
    lv_obj_t* l = makeLabel(card, ui::font::small(), ui::color::dim(), LV_SYMBOL_IMAGE);
    lv_obj_center(l);
    Serial.printf("[image] %s not on the device yet\n", name);
    return;
  }

  const uint32_t t0 = millis();
  w_ = r.width();
  h_ = r.height();
  const size_t frameBytes = static_cast<size_t>(w_) * h_ * 2;
  const size_t allBytes = frameBytes * r.frames();
  const bool preDecode = r.frames() == 1 ||
                         (allBytes <= kPreDecodeWidget && gPreDecodedTotal + allBytes <= kPreDecodeTotal);
  delays_ = static_cast<uint16_t*>(heap_caps_malloc(sizeof(uint16_t) * r.frames(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (preDecode) {
    pixels_ = static_cast<uint16_t*>(heap_caps_malloc(allBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pixels_ && delays_) {
      proto::RpaFrame f;
      while (frames_ < r.frames() && r.next(f)) {
        if (!decodeFrame(f, pixels_ + frames_ * (frameBytes / 2), w_, h_)) break;
        delays_[frames_++] = f.delayMs < 20 ? 20 : f.delayMs;
      }
      preDecoded_ = frameBytes * frames_;
      gPreDecodedTotal += preDecoded_;
    }
    heap_caps_free(file);
  } else {
    // Too big to decode ahead: keep the JPEG frames and decode each one when it is due.
    pixels_ = static_cast<uint16_t*>(heap_caps_malloc(frameBytes * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    jpegs_ = static_cast<proto::RpaFrame*>(heap_caps_malloc(sizeof(proto::RpaFrame) * r.frames(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (pixels_ && delays_ && jpegs_) {
      file_ = file;
      proto::RpaFrame f;
      while (frames_ < r.frames() && r.next(f)) {
        jpegs_[frames_] = f;
        delays_[frames_++] = f.delayMs < 20 ? 20 : f.delayMs;
      }
      // Check the first frame decodes (it is decoded again when shown).
      if (frames_ > 0 && !decodeFrame(jpegs_[0], pixels_ + static_cast<size_t>(w_) * h_, w_, h_)) frames_ = 0;
    } else {
      heap_caps_free(file);
    }
  }
  if (frames_ == 0) {
    lv_obj_t* l = makeLabel(card, ui::font::small(), ui::color::crit(), LV_SYMBOL_WARNING);
    lv_obj_center(l);
    Serial.printf("[image] %s: decode failed\n", name);
    return;
  }
  img_ = lv_image_create(card);
  lv_obj_center(img_);
  showFrame(0);
  if (frames_ > 1) {
    timer_ = lv_timer_create(onTimer, delays_[0], this);
  }
  Serial.printf("[image] %s: %dx%d, %d frames, %s, %u KB, ready in %lu ms\n", name, w_, h_, frames_,
                file_ ? "decoded per frame" : "decoded ahead",
                static_cast<unsigned>((file_ ? 2 * frameBytes + len : frameBytes * frames_) / 1024),
                static_cast<unsigned long>(millis() - t0));
}

void ImageWidget::showFrame(int i) {
  current_ = i;
  if (file_) {
    // Decoded per frame into the buffer that is not on screen, which then becomes the shown one.
    uint16_t* dst = pixels_ + static_cast<size_t>(back_) * w_ * h_;
    if (!decodeFrame(jpegs_[i], dst, w_, h_)) return;
    showPixels(dst);
    back_ ^= 1;
    return;
  }
  showPixels(pixels_ + static_cast<size_t>(i) * w_ * h_);
}

void ImageWidget::showPixels(const uint16_t* px) {
  dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
  dsc_.header.w = w_;
  dsc_.header.h = h_;
  dsc_.header.stride = w_ * 2;
  dsc_.data = reinterpret_cast<const uint8_t*>(px);
  dsc_.data_size = static_cast<uint32_t>(w_) * h_ * 2;
  lv_image_set_src(img_, nullptr);
  lv_image_set_src(img_, &dsc_);
}

void ImageWidget::onTimer(lv_timer_t* t) {
  auto* self = static_cast<ImageWidget*>(lv_timer_get_user_data(t));
  // Hidden pages do not need new frames; LVGL would not draw them anyway.
  if (lv_obj_is_hidden(lv_obj_get_parent(lv_obj_get_parent(self->img_)))) return;
  const int next = (self->current_ + 1) % self->frames_;
  self->showFrame(next);
  lv_timer_set_period(t, self->delays_[next]);
}

}  // namespace widgets
}  // namespace rp
