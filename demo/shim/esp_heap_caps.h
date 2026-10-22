#pragma once
#include <stdlib.h>
#include <stddef.h>
#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
inline void* heap_caps_calloc(size_t a, size_t b, unsigned) { return calloc(a, b); }
inline void* heap_caps_realloc(void* p, size_t n, unsigned) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(unsigned c) { return c & MALLOC_CAP_SPIRAM ? 6u << 20 : 120000; }
inline size_t heap_caps_get_minimum_free_size(unsigned) { return 100000; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return 90000; }
inline void heap_caps_malloc_extmem_enable(size_t) {}
inline size_t iramPureFreeDemo() { return 60000; }
