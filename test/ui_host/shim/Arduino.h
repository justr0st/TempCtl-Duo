// Host stand-in for the Arduino core (UI screenshots only)
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
using std::min; using std::max;
uint32_t millis();
void delay(uint32_t ms);
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  char operator[](unsigned i) const { return s[i]; }
  void reserve(unsigned) {}
};
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) write(b[i]); return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s = "") { return print(s) + print("\n"); }
  template <typename... A> size_t printf(const char* f, A... a) { char b[512]; snprintf(b, sizeof(b), f, a...); return print(b); }
};
struct SerialT { template <typename... A> void printf(const char* f, A... a) { ::printf(f, a...); } void println(const char* s) { puts(s); } void print(const char* s) { fputs(s, stdout); } };
extern SerialT Serial;
struct EspT { void restart() { puts("[host] restart"); } uint32_t getFreeHeap() { return 210000; } uint32_t getFreePsram() { return 7900000; } uint32_t getMaxAllocHeap() { return 100000; } };
extern EspT ESP;
class HardwareSerial {};
#define constrain(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
