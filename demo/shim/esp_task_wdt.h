#pragma once
inline int esp_task_wdt_reset() { return 0; }
inline int esp_task_wdt_init(int, bool) { return 0; }
inline int esp_task_wdt_add(void*) { return 0; }
inline int esp_task_wdt_delete(void*) { return 0; }
