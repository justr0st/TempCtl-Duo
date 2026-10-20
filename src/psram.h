// Large variables that are not used in a time-critical path live in PSRAM on boards that have it (S3): the
// internal RAM is kept for what needs it (display DMA buffers, task stacks, Wi-Fi, mbedTLS fallback). The S3
// core can't place static variables in PSRAM (no CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY), so such a
// variable becomes a reference to a block taken from PSRAM when it is first used (function-local static) or
// at start-up (file scope; PSRAM is part of the heap before the global constructors run). Without PSRAM (CYD)
// it stays an ordinary static variable.
//   static PSRAM_VAR(Settings, S);              instead of   static Settings S;
//   static PSRAM_VAR(char[1200], scanNames);    instead of   static char scanNames[1200];   (sizeof still works)
// The block is zero-filled and the type's constructor runs, as for a static variable. It is never freed.
#pragma once
#include <new>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

#if defined(BOARD_HAS_PSRAM) && defined(ESP_PLATFORM)
#include <esp_heap_caps.h>

template <class T>
using PsramRef = T&;

// (C++11: the constructor through overloads; plain types are already zero-filled)
template <class T>
void psramConstruct(void*, std::true_type) {}
template <class T>
void psramConstruct(void* p, std::false_type) {
  static_assert(!std::is_array<T>::value, "PSRAM_VAR: arrays only of plain types");
  new (p) T();
}

template <class T>
T& psramNew() {
  void* p = heap_caps_calloc(1, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = calloc(1, sizeof(T));  // (PSRAM full: internal RAM)
  if (!p) abort();                   // (as a static variable that does not fit: no way to go on)
  psramConstruct<T>(p, typename std::is_trivially_default_constructible<T>::type());
  return *static_cast<T*>(p);
}
#define PSRAM_VAR(T, name) PsramRef<T> name = psramNew<T>()

#else

template <class T>
using PsramPlain = T;
#define PSRAM_VAR(T, name) PsramPlain<T> name

#endif
