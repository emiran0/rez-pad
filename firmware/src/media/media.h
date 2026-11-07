// Now Playing data from the host (docs/protocol.md): NOW_PLAYING, VOLUME and album art.
// The link task feeds messages in (on*), the UI task takes the latest state out (take*).
// Album art is reassembled, CRC-checked and JPEG-decoded on the link task into one of two PSRAM
// buffers; the UI swaps to it when it takes the art, so neither side waits on the other.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui/model.h"

namespace rp {
namespace media {

// Largest art edge the device accepts (and allocates for).
constexpr int kMaxArtSide = 240;

bool begin();

// ---- link task ----
void onNowPlaying(const uint8_t* body, size_t len);
void onVolume(const uint8_t* body, size_t len);
void onArtBegin(const uint8_t* body, size_t len);
void onArtChunk(const uint8_t* body, size_t len);
void onArtEnd(const uint8_t* body, size_t len);
void onArtNone(const uint8_t* body, size_t len);
// Host gone: drop any transfer in progress.
void onHostLost();

// ---- UI task ----
// Each returns true (and fills `out`) when something new arrived since the last call.
bool takeNowPlaying(ui::NowPlaying& out);
bool takeVolume(ui::Volume& out);
bool takeArt(ui::Art& out);
// The UI no longer shows any art (host lost): both buffers are free again.
void releaseArt();

// ---- any task ----
bool artTransferActive();
// Art edge in px the active layout wants (0 = no album_art widget). Reported in HELLO_ACK.
void setArtSize(int px);
int artSize();

struct Stats {
  uint32_t nowPlaying;
  uint32_t artOk;
  uint32_t artFailed;   // bad chunk, incomplete, CRC or decode error
  uint32_t badJson;
  uint32_t lastDecodeUs;
  uint32_t lastArtBytes;
};
Stats stats();

}  // namespace media
}  // namespace rp
