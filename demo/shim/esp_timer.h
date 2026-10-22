#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
struct DemoTimer;
typedef DemoTimer* esp_timer_handle_t;
typedef enum { ESP_TIMER_TASK } esp_timer_dispatch_t;
typedef struct {
  void (*callback)(void*);
  void* arg;
  esp_timer_dispatch_t dispatch_method;
  const char* name;
  bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* h);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us);
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us);
esp_err_t esp_timer_stop(esp_timer_handle_t h);
int64_t esp_timer_get_time();
