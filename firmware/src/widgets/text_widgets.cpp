#include "text_widgets.h"

#include "common.h"

namespace rp {
namespace widgets {

void Label::build(lv_obj_t* card, int w, int h) {
  int top = 0;
  if (spec_.label[0]) {
    lv_obj_t* title = makeEllipsis(card, ui::font::small(), ui::color::dim(), w);
    lv_label_set_text(title, spec_.label);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    top = 18;
  }
  // Big text in a big card when it fits on one line, otherwise the body font, wrapped (the card
  // clips anything that does not fit).
  lv_obj_t* text = makeLabel(card, ui::font::large(), ui::color::text(), spec_.text);
  lv_obj_update_layout(text);
  if (h - top < 60 || lv_obj_get_width(text) > w) lv_obj_set_style_text_font(text, ui::font::body(), 0);
  lv_obj_set_width(text, w);
  lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(text, LV_ALIGN_CENTER, 0, top / 2);
}

}  // namespace widgets
}  // namespace rp
