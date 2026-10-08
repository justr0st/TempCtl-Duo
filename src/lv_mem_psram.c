// LVGL memory in PSRAM (S3, see LV_USE_STDLIB_MALLOC in include/lv_conf.h). LVGL allocates many small
// blocks (every object, style, label text, draw task); malloc() keeps blocks under 4 kB in the internal
// RAM, so all screens together used up most of it. Here they go to PSRAM; internal RAM only when PSRAM
// is full.
#include <lvgl.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
#include <esp_heap_caps.h>
#include <stdlib.h>

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}
void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(size);
}

void* lv_realloc_core(void* p, size_t new_size) {
  void* q = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q ? q : realloc(p, new_size);
}

void lv_free_core(void* p) { free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) {
  LV_UNUSED(mon_p);  // not supported
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
#endif
