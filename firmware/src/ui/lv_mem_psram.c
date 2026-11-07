/* LVGL memory in PSRAM (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM in include/lv_conf.h).
 * With plain malloc every LVGL object went to internal RAM: eight full pages (96 widgets) left
 * 3 KB of it. Objects, styles and labels now live in PSRAM (internal RAM only as a fallback); the
 * draw buffers are allocated separately in internal DMA RAM (lvgl_port.cpp). */
#include <esp_heap_caps.h>

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define RP_LV_PREFER (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define RP_LV_FALLBACK (MALLOC_CAP_DEFAULT)

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) { return heap_caps_malloc_prefer(size, 2, RP_LV_PREFER, RP_LV_FALLBACK); }

void* lv_realloc_core(void* p, size_t new_size) {
  return heap_caps_realloc_prefer(p, new_size, 2, RP_LV_PREFER, RP_LV_FALLBACK);
}

void lv_free_core(void* p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, RP_LV_PREFER);
  mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
  mon->free_size = info.total_free_bytes;
  mon->free_biggest_size = info.largest_free_block;
  mon->free_cnt = info.free_blocks;
  mon->max_used = info.total_allocated_bytes + info.total_free_bytes - info.minimum_free_bytes;
  mon->used_pct = mon->total_size ? (uint8_t)(100 * info.total_allocated_bytes / mon->total_size) : 0;
  mon->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
