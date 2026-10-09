// Host stand-in for the Arduino core: serial console test (console.cpp)
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <string>
#include <deque>
#include <algorithm>
using std::min; using std::max;
uint32_t millis();
void delay(uint32_t ms);
inline size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
inline size_t strlcat(char* d, const char* s, size_t n) { size_t l = strlen(d); return l + strlcpy(d + l, s, n > l ? n - l : 0); }
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(int v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  void replace(const char* a, const char* b) { size_t p; while ((p = s.find(a)) != std::string::npos) s.replace(p, strlen(a), b); }
  friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
};
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) write(b[i]); return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s = "") { return print(s) + print("\r\n"); }
  size_t printf(const char* f, ...) __attribute__((format(printf, 2, 3))) {
    char b[512]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof(b), f, ap); va_end(ap); return print(b);
  }
};
class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
};
// the console's port: input queued by the test, output to stdout
struct SerialT : Stream {
  std::deque<char> in;
  void feed(const char* s) { for (; *s; ++s) in.push_back(*s); }
  int available() override { return (int)in.size(); }
  int read() override { if (in.empty()) return -1; char c = in.front(); in.pop_front(); return (uint8_t)c; }
  size_t write(uint8_t c) override { fputc(c, stdout); return 1; }
  size_t write(const uint8_t* b, size_t n) override { fwrite(b, 1, n, stdout); return n; }
  void begin(int) {}
};
extern SerialT Serial;
class HardwareSerial {};
struct EspT { void restart() {} uint32_t getFreeHeap() { return 50000; } };
extern EspT ESP;
#define constrain(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
