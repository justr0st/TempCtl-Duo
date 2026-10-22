// Demo (browser simulation, demo/README.md): stand-in for the Arduino core. Time is the simulation's clock
// (demo/sim.cpp), pins go to the simulated hardware.
#pragma once
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <algorithm>
#include <string>
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
using std::max;
using std::min;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();

extern "C" size_t strlcpy(char* d, const char* s, size_t n);
extern "C" size_t strlcat(char* d, const char* s, size_t n);

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(const String& o) = default;
  String(String&& o) = default;
  explicit String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(long long v) : s(std::to_string(v)) {}
  String(unsigned long long v) : s(std::to_string(v)) {}
  String(float v, int dec = 2) { fmt(v, dec); }
  String(double v, int dec = 2) { fmt(v, dec); }
  String& operator=(const String& o) = default;
  String& operator=(String&& o) = default;
  String& operator=(const char* c) { s = c ? c : ""; return *this; }
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return (unsigned)s.size(); }
  bool isEmpty() const { return s.empty(); }
  void reserve(size_t n) { s.reserve(n); }
  char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }
  char& operator[](unsigned i) { return s[i]; }
  char charAt(unsigned i) const { return (*this)[i]; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { if (o) s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(int v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned v) { s += std::to_string(v); return *this; }
  String& operator+=(long v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s += std::to_string(v); return *this; }
  String& operator+=(long long v) { s += std::to_string(v); return *this; }
  String& operator+=(unsigned long long v) { s += std::to_string(v); return *this; }
  String& operator+=(float v) { return *this += String(v); }
  String& operator+=(double v) { return *this += String(v); }
  bool concat(const String& o) { s += o.s; return true; }
  bool concat(const char* o) { if (o) s += o; return true; }
  bool concat(const char* o, unsigned n) { if (o) s.append(o, n); return true; }
  bool concat(char c) { s += c; return true; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == (o ? o : ""); }
  bool operator!=(const String& o) const { return s != o.s; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return s < o.s; }
  bool equals(const String& o) const { return s == o.s; }
  bool equalsIgnoreCase(const String& o) const { return !strcasecmp(c_str(), o.c_str()); }
  bool startsWith(const String& p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  bool endsWith(const String& p) const { return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0; }
  int indexOf(char c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& x, unsigned from = 0) const { size_t p = s.find(x.s, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(unsigned a, unsigned b) const { if (b > s.size()) b = (unsigned)s.size(); return a >= b ? String() : String(s.substr(a, b - a)); }
  void remove(unsigned i) { if (i < s.size()) s.erase(i); }
  void remove(unsigned i, unsigned n) { if (i < s.size()) s.erase(i, n); }
  void replace(const String& a, const String& b) { if (a.s.empty()) return; size_t p = 0; while ((p = s.find(a.s, p)) != std::string::npos) { s.replace(p, a.s.size(), b.s); p += b.s.size(); } }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  void toLowerCase() { for (auto& c : s) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (auto& c : s) c = (char)toupper((unsigned char)c); }
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  float toFloat() const { return strtof(c_str(), nullptr); }
  double toDouble() const { return strtod(c_str(), nullptr); }
  // ArduinoJson's String support
  size_t write(uint8_t c) { s += (char)c; return 1; }
  size_t write(const uint8_t* b, size_t n) { s.append((const char*)b, n); return n; }
  friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
  friend String operator+(const String& a, const char* b) { return String(a.s + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b.s); }
  friend String operator+(const String& a, char b) { return String(a.s + b); }
  friend String operator+(const String& a, int b) { return String(a.s + std::to_string(b)); }
  friend String operator+(const String& a, unsigned b) { return String(a.s + std::to_string(b)); }
  friend String operator+(const String& a, long b) { return String(a.s + std::to_string(b)); }
  friend String operator+(const String& a, unsigned long b) { return String(a.s + std::to_string(b)); }
  friend String operator+(const String& a, float b) { return a + String(b); }
  friend String operator+(const String& a, double b) { return a + String(b); }

 private:
  void fmt(double v, int dec) { char b[48]; snprintf(b, sizeof(b), "%.*f", dec, v); s = b; }
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; for (size_t i = 0; i < n; ++i) k += write(b[i]); return k; }
  size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t write(const char* s, size_t n) { return write((const uint8_t*)s, n); }
  virtual int availableForWrite() { return 0; }
  virtual void flush() {}
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const String& s) { return print(s.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { return printf("%d", v); }
  size_t print(unsigned v) { return printf("%u", v); }
  size_t print(long v) { return printf("%ld", v); }
  size_t print(unsigned long v) { return printf("%lu", v); }
  size_t print(double v, int d = 2) { return printf("%.*f", d, v); }
  size_t println() { return print("\r\n"); }
  template <typename T>
  size_t println(const T& v) { return print(v) + print("\r\n"); }
  size_t printf(const char* f, ...) __attribute__((format(printf, 2, 3))) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    if (n < (int)sizeof(b)) return write((const uint8_t*)b, n < 0 ? 0 : (size_t)n);
    std::string big((size_t)n + 1, 0);
    va_start(ap, f);
    vsnprintf(&big[0], big.size(), f, ap);
    va_end(ap);
    return write((const uint8_t*)big.data(), (size_t)n);
  }
};

class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() { return -1; }
  void setTimeout(unsigned long) {}
  size_t readBytes(char* b, size_t n) { size_t k = 0; while (k < n) { int c = read(); if (c < 0) break; b[k++] = (char)c; } return k; }
  size_t readBytes(uint8_t* b, size_t n) { return readBytes((char*)b, n); }
  size_t readBytesUntil(char t, char* b, size_t n) { size_t k = 0; while (k < n) { int c = read(); if (c < 0 || c == t) break; b[k++] = (char)c; } return k; }
  String readString() { String o; int c; while ((c = read()) >= 0) o += (char)c; return o; }
  String readStringUntil(char t) { String o; int c; while ((c = read()) >= 0 && c != t) o += (char)c; return o; }
};

// the serial port: output goes to the browser's console (demo/sim.cpp), no input
class HardwareSerial : public Stream {
 public:
  void begin(unsigned long, int = 0, int = -1, int = -1) {}
  void end() {}
  void setRxBufferSize(size_t) {}
  void setTxBufferSize(size_t) {}
  int available() override { return 0; }
  int read() override { return -1; }
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* b, size_t n) override;
  int availableForWrite() override { return 4096; }
  operator bool() const { return true; }
};
extern HardwareSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial2;

struct EspClass {
  void restart();
  uint32_t getFreeHeap() { return 180000; }
  uint32_t getPsramSize() { return 8u << 20; }
  uint32_t getFreePsram() { return 6u << 20; }
  uint64_t getEfuseMac() { return 0x0000DE30A0D0ULL; }
  const char* getChipModel() { return "Demo"; }
};
extern EspClass ESP;

// pins: the simulated hardware (demo/sim.cpp)
#define LOW 0
#define HIGH 1
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
void pinMode(int pin, int mode);
void digitalWrite(int pin, int v);
int digitalRead(int pin);
void ledcSetup(int ch, uint32_t hz, int bits);
void ledcAttachPin(int pin, int ch);
void ledcWrite(int ch, uint32_t duty);
float temperatureRead();
long random(long n);
long random(long a, long b);

#define SERIAL_8N1 0x800001c
#define SERIAL_8N2 0x800003c
#define SERIAL_8E1 0x800001e
#define SERIAL_8E2 0x800003e
#define SERIAL_8O1 0x800001f
#define SERIAL_8O2 0x800003f
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}
#define constrain(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
inline void feedLoopWDT() {}
inline void enableLoopWDT() {}
#define SET_LOOP_TASK_STACK_SIZE(n)
