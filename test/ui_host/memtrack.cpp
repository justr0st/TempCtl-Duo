// MEMTEST: malloc tracking with --wrap (allocations made while tracking is on and still alive)
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __real_calloc(size_t, size_t);
struct Rec { size_t n; void* bt[16]; int depth; };
static bool on = false, inside = false;
static std::map<void*, Rec>* live;
static void add(void* p, size_t n) {
  if (!on || inside || !p) return;
  inside = true;
  if (!live) live = new std::map<void*, Rec>;
  Rec r; r.n = n; r.depth = backtrace(r.bt, 16);
  (*live)[p] = r;
  inside = false;
}
static void del(void* p) {
  if (!live || inside || !p) return;
  inside = true;
  live->erase(p);
  inside = false;
}
extern "C" void* __wrap_malloc(size_t n) { void* p = __real_malloc(n); add(p, n); return p; }
extern "C" void* __wrap_calloc(size_t a, size_t b) { void* p = __real_calloc(a, b); add(p, a * b); return p; }
extern "C" void __wrap_free(void* p) { del(p); __real_free(p); }
extern "C" void* __wrap_realloc(void* p, size_t n) { del(p); void* q = __real_realloc(p, n); add(q, n); return q; }
void trackStart() { if (live) live->clear(); on = true; }
void trackReport() {
  on = false;
  inside = true;
  std::map<std::string, std::pair<size_t, int>> by;
  for (auto& kv : *live) {
    char** s = backtrace_symbols(kv.second.bt, kv.second.depth);
    std::string k;
    for (int i = 3; i < kv.second.depth && i < 14; ++i) { k += s[i]; k += " | "; }
    free(s);
    auto& e = by[k]; e.first += kv.second.n; e.second++;
  }
  for (auto& kv : by) printf("%6zu bytes %3d x  %s\n", kv.second.first, kv.second.second, kv.first.c_str());
  inside = false;
}
void trackSummary() {  // allocations alive since trackStart: count, bytes, by size class
  inside = true;
  size_t n = 0, b = 0, small = 0;
  for (auto& kv : *live) { ++n; b += kv.second.n; if (kv.second.n <= 32) ++small; }
  printf("[mem] alive: %zu blocks, %zu bytes (%zu blocks <= 32 bytes)\n", n, b, small);
  if (getenv("MEMBY")) {  // by the first caller that is not an allocator
    std::map<std::string, std::pair<size_t, int>> by;
    for (auto& kv : *live) {
      char** sy = backtrace_symbols(kv.second.bt, kv.second.depth);
      std::string k = "?";
      for (int i = 1; i < kv.second.depth; ++i) {
        std::string f = sy[i];
        if (f.find("malloc") != std::string::npos || f.find("realloc") != std::string::npos || f.find("calloc") != std::string::npos || f.find("+0x") == f.find("(+")) continue;
        k = f.substr(f.find('(') + 1, f.find('+') - f.find('(') - 1);
        if (k.empty()) continue;
        if (getenv("MEMBY")[0] == '2' && (k == "lv_obj_allocate_spec_attr" || k == "lv_obj_set_local_style_prop" || k == "lv_style_set_prop"))
          for (int j = i + 1; j < kv.second.depth && j < i + 4; ++j) {
            std::string g = sy[j];
            size_t a = g.find('('), b2 = g.find('+');
            if (a != std::string::npos && b2 > a + 1) k += " < " + g.substr(a + 1, b2 - a - 1);
          }
        break;
      }
      free(sy);
      auto& e = by[k]; e.first += kv.second.n; e.second++;
    }
    for (auto& kv : by) printf("   %6zu bytes %4d x  %s\n", kv.second.first, kv.second.second, kv.first.c_str());
  }
  inside = false;
}
