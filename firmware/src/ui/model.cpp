#include "model.h"

#include <string.h>

namespace rp {
namespace ui {

namespace {
Model gModel;
}

Model& model() { return gModel; }

const Stat* findStat(const char* id) {
  for (const Stat& s : gModel.stats) {
    if (s.valid && s.id[0] && strcmp(s.id, id) == 0) return &s;
  }
  return nullptr;
}

}  // namespace ui
}  // namespace rp
