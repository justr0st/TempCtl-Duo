// Browser demo (demo/README.md): the firmware's control code with simulated hardware, compiled to
// WebAssembly. This file is the "board": the clock and the tasks (one thread, stepped by the page), the
// pins (SSRs, SSR supply relay, MAX31855 / MAX6675 frames, E-stop, buzzer), the flash file system in
// memory, and the heat model: a medium-sized GPU board over an 1100 W IR preheater with a 450 W IR head
// above its GPU.
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <deque>
#include <vector>
#include "app_state.h"
#include "config.h"
#include "console.h"
#include "local_ctrl.h"
#include "program_store.h"
#include "safety.h"
#include "syslog.h"
#include "web.h"

#ifdef __wasm__
#define EXPORT(n) extern "C" __attribute__((export_name(n)))
#else
#define EXPORT(n) extern "C"
#endif

// ================================================================== clock
static uint64_t nowUs = 1000000;  // (starts at 1 s: millis() 0 means "never" in places)
uint32_t millis() { return (uint32_t)(nowUs / 1000); }
uint32_t micros() { return (uint32_t)nowUs; }
void delay(uint32_t ms) { nowUs += (uint64_t)ms * 1000; }
void delayMicroseconds(uint32_t us) { nowUs += us; }
void yield() {}
int64_t esp_timer_get_time() { return (int64_t)nowUs; }

// the wall clock: the page's time at the start + the simulated time since (runs fast with the speed-up)
static double wallBaseMs = 0;
static double wallMs() { return wallBaseMs + nowUs / 1000.0; }
EXPORT("demo_wall_ms") double demo_wall_ms() { return wallMs(); }
extern "C" int settimeofday(const struct timeval* tv, const struct timezone*) {
  if (tv) wallBaseMs = tv->tv_sec * 1000.0 + tv->tv_usec / 1000.0 - nowUs / 1000.0;
  return 0;
}
#ifdef __wasi__
extern "C" __attribute__((weak)) void tzset(void) {}  // (WASI: UTC only)
#endif

// ================================================================== FreeRTOS stand-in
static bool inControl = false;
static void controlYield() {  // a wait for the control task: let it run once
  if (inControl) return;
  inControl = true;
  delay(10);
  controlTaskStep();
  inControl = false;
}

struct DemoSem {
  bool mutex;
  int count;
};
SemaphoreHandle_t xSemaphoreCreateMutex() { return new DemoSem{true, 1}; }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new DemoSem{true, 1}; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return new DemoSem{false, 0}; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) {
  if (!s) return pdFALSE;
  if (s->mutex) return pdTRUE;  // (one thread: nobody else holds it)
  for (uint32_t waited = 0; s->count == 0 && waited < wait && waited < 60000; waited += 10) controlYield();
  if (s->count == 0) return pdFALSE;
  --s->count;
  return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  if (s && !s->mutex) s->count = 1;
  return pdTRUE;
}

struct DemoQueue {
  size_t size, cap;
  std::deque<std::vector<uint8_t>> q;
};
QueueHandle_t xQueueCreate(UBaseType_t n, UBaseType_t size) { return new DemoQueue{size, n, {}}; }
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait) {
  for (uint32_t waited = 0; q->q.size() >= q->cap && waited < wait; waited += 10) controlYield();
  if (q->q.size() >= q->cap) return pdFALSE;
  const uint8_t* p = (const uint8_t*)item;
  q->q.emplace_back(p, p + q->size);
  return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t) {
  if (q->q.empty()) return pdFALSE;
  memcpy(item, q->q.front().data(), q->size);
  q->q.pop_front();
  return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return (UBaseType_t)q->q.size(); }
// the tasks are stepped by demoAdvance() below; their own loops never run
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t* h, BaseType_t) {
  if (h) *h = (TaskHandle_t)1;
  return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t f, const char* n, uint32_t s, void* a, UBaseType_t p, TaskHandle_t* h) {
  return xTaskCreatePinnedToCore(f, n, s, a, p, h, 0);
}
void vTaskDelay(TickType_t t) { delay(t); }
void vTaskDelayUntil(TickType_t* wake, TickType_t t) { *wake += t; }
TickType_t xTaskGetTickCount() { return millis(); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4000; }
void vTaskDelete(TaskHandle_t) {}

struct DemoTimer {
  void (*cb)(void*);
  void* arg;
  uint64_t period, next;
  bool on;
};
static std::vector<DemoTimer*> timers;
esp_err_t esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* h) {
  *h = new DemoTimer{a->callback, a->arg, 0, 0, false};
  timers.push_back(*h);
  return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) {
  h->period = us;
  h->next = nowUs + us;
  h->on = true;
  return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t us) {
  h->period = 0;
  h->next = nowUs + us;
  h->on = true;
  return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
  h->on = false;
  return ESP_OK;
}

// ================================================================== ESP / serial
static bool restartAsked = false, wasRestart = false;
EspClass ESP;
void EspClass::restart() { restartAsked = true; }
void esp_restart() { restartAsked = true; }
esp_reset_reason_t esp_reset_reason() { return wasRestart ? ESP_RST_SW : ESP_RST_POWERON; }
static uint32_t rng = 0x2545F491;
static uint32_t rnd() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
uint32_t esp_random() { return rnd(); }
long random(long n) { return n > 0 ? (long)(rnd() % (uint32_t)n) : 0; }
long random(long a, long b) { return b > a ? a + random(b - a) : a; }
static float noise() { return ((rnd() & 0xFFFF) / 65535.0f - 0.5f); }  // -0.5 ... 0.5

// the serial log: to the browser's console (stdout), line by line
HardwareSerial Serial, Serial1, Serial2;
size_t HardwareSerial::write(uint8_t c) { return write(&c, 1); }
size_t HardwareSerial::write(const uint8_t* b, size_t n) {
  if (this == &Serial) fwrite(b, 1, n, stdout);
  return n;
}
WiFiClass WiFi;

// ================================================================== flash file system (in memory)
fs::FS LittleFS;
static uint32_t fsGen = 0;  // changes: the page stores the file system
fs::File fs::FS::open(const char* path, const char* mode, bool) {
  const bool w = mode[0] == 'w' || mode[0] == 'a';
  auto it = files.find(path);
  if (!w) return it == files.end() ? File() : File(it->second, false, false, path);
  ++fsGen;
  if (mode[0] == 'w' || it == files.end()) {
    auto d = std::make_shared<std::vector<uint8_t>>();
    files[path] = d;
    return File(d, true, false, path);
  }
  return File(it->second, true, true, path);
}
bool fs::FS::rename(const char* a, const char* b) {
  auto it = files.find(a);
  if (it == files.end()) return false;
  auto d = it->second;
  files.erase(it);
  files[b] = d;
  ++fsGen;
  return true;
}
size_t fs::FS::usedBytes() {
  size_t n = 0;
  for (auto& f : files) n += (f.second->size() + 4095) / 4096 * 4096;
  return n + 8192;
}
int demo_stat(const char* path, struct stat* st) {
  const char* p = !strncmp(path, "/littlefs", 9) ? path + 9 : path;
  memset(st, 0, sizeof(*st));
  auto it = LittleFS.files.find(p);
  if (it != LittleFS.files.end()) {
    st->st_size = (off_t)it->second->size();
    st->st_mode = S_IFREG;
    return 0;
  }
  if (LittleFS.dirs.count(p) || !strcmp(p, "/")) {
    st->st_mode = S_IFDIR;
    return 0;
  }
  return -1;
}
// [u16 path length][path][u32 size][data] ..., little endian
static std::vector<uint8_t> blob;
EXPORT("demo_fs_dump") uint8_t* demo_fs_dump() {
  blob.clear();
  auto put = [](uint32_t v, int n) { for (int i = 0; i < n; ++i) blob.push_back((uint8_t)(v >> (8 * i))); };
  for (auto& f : LittleFS.files) {
    put((uint32_t)f.first.size(), 2);
    blob.insert(blob.end(), f.first.begin(), f.first.end());
    put((uint32_t)f.second->size(), 4);
    blob.insert(blob.end(), f.second->begin(), f.second->end());
  }
  return blob.data();
}
EXPORT("demo_fs_size") uint32_t demo_fs_size() { return (uint32_t)blob.size(); }
EXPORT("demo_fs_gen") uint32_t demo_fs_gen() { return fsGen; }
EXPORT("demo_fs_load") void demo_fs_load(const uint8_t* p, uint32_t n) {
  LittleFS.files.clear();
  LittleFS.dirs["/duo"] = 1;
  auto get = [&](uint32_t& at, int k) { uint32_t v = 0; for (int i = 0; i < k; ++i) v |= (uint32_t)p[at++] << (8 * i); return v; };
  for (uint32_t at = 0; at + 6 <= n;) {
    const uint32_t pl = get(at, 2);
    if (at + pl + 4 > n) break;
    std::string path((const char*)p + at, pl);
    at += pl;
    const uint32_t dl = get(at, 4);
    if (at + dl > n) break;
    LittleFS.files[path] = std::make_shared<std::vector<uint8_t>>(p + at, p + at + dl);
    at += dl;
  }
}
// buffers the page writes into
EXPORT("demo_alloc") void* demo_alloc(uint32_t n) { return malloc(n); }
EXPORT("demo_free") void demo_free(void* p) { free(p); }

// ================================================================== the heat model
// Four bodies, temperatures in degC: the bottom IR emitter, the board, the top IR emitter and the GPU zone
// (the chip with the board around it under the top head). Radiation between emitter and target (emissivity
// 0.9, effective areas), convection, conduction from the GPU zone into the board.
// Bottom thermocouple: on the board's underside next to the GPU; top: on the GPU. Both with a little lag
// (taped on) and some noise.
struct Heat {
  float hb, b, ht, z;  // bodies
  float tcB, tcT;      // what the thermocouples see
  float cj;            // converter (cold junction) temperature
};
static constexpr float AMB = 23.0f;
static Heat heat{AMB, AMB, AMB, AMB, AMB, AMB, 26.0f};
static float powB = 0, powT = 0;     // heater power, W (averaged over the last step, for the demo bar)
static bool tcOpen[2] = {false, false};
static bool estopPressed = false;
static float speedUp = 1;

static inline float rad(float a, float b, float area) {
  const float A = a + 273.15f, B = b + 273.15f;
  return 0.9f * 5.67e-8f * area * (A * A * A * A - B * B * B * B);
}
static void heatStep(float dt, float pb, float pt) {
  Heat& h = heat;
  const float qHbB = rad(h.hb, h.b, 0.035f) + 0.4f * (h.hb - h.b);
  const float qHbA = rad(h.hb, AMB, 0.035f) + 0.6f * (h.hb - AMB);
  const float qHtZ = rad(h.ht, h.z, 0.018f) + 0.15f * (h.ht - h.z);
  const float qHtB = rad(h.ht, h.b, 0.004f);
  const float qHtA = rad(h.ht, AMB, 0.010f) + 0.25f * (h.ht - AMB);
  const float qBA = 0.56f * (h.b - AMB) + rad(h.b, AMB, 0.035f);
  const float qZA = 0.25f * (h.z - AMB) + rad(h.z, AMB, 0.004f);
  const float qZB = 0.8f * (h.z - h.b);
  h.hb += dt * (pb - qHbB - qHbA) / 260.0f;
  h.ht += dt * (pt - qHtZ - qHtB - qHtA) / 60.0f;
  h.b += dt * (qHbB + qHtB + qZB - qBA) / 320.0f;
  h.z += dt * (qHtZ - qZB - qZA) / 60.0f;
  const float k = dt / 1.5f;  // thermocouple lag 1.5 s
  h.tcB += (h.b - h.tcB) * (k > 1 ? 1 : k);
  h.tcT += (h.z - h.tcT) * (k > 1 ? 1 : k);
  // the converters sit in the controller box: warm a little while the heaters run
  const float cjWant = 26.0f + (h.hb - AMB) * 0.012f + (h.ht - AMB) * 0.006f;
  h.cj += (cjWant - h.cj) * dt / 300.0f;
}

// ================================================================== pins
static uint8_t level[64];
static int ledcDuty[16];
static int buzzerCh = -1;
static int frameCh = -1, frameBit = 0, frameBits = 32;
static uint32_t frame = 0;

static uint32_t frame31855(float t, float cj, bool open) {
  if (open) return 0x00010001u | ((uint32_t)(int32_t)lroundf(cj / 0.0625f) & 0xFFF) << 4;  // fault + OC
  const int32_t tc = (int32_t)lroundf(t / 0.25f);
  const int32_t ic = (int32_t)lroundf(cj / 0.0625f);
  return ((uint32_t)tc & 0x3FFF) << 18 | ((uint32_t)ic & 0xFFF) << 4;
}
static uint16_t frame6675(float t, bool open) {
  if (open) return 0x0004;
  int32_t v = (int32_t)lroundf(t / 0.25f);
  if (v < 0) v = 0;
  if (v > 4095) v = 4095;
  return (uint16_t)(v << 3);
}
// ch 2: the monitor channel's thermocouple, at the board's edge (well away from the heads: about half of
// the board's rise)
static void frameStart(int ch) {
  const float t = (ch == 0 ? heat.tcB : ch == 1 ? heat.tcT : AMB + (heat.b - AMB) * 0.55f) + noise() * 0.3f;
  const bool open = ch < 2 && tcOpen[ch];
  frameCh = ch;
  frameBit = 0;
  if ((ch < 2 ? gSettings.ch[ch].loc.sensor : gSettings.mon.sensor) == SENSOR_MAX6675) {
    frameBits = 16;
    frame = frame6675(t, open);
  } else {
    frameBits = 32;
    frame = frame31855(t, heat.cj, open);
  }
}
void pinMode(int pin, int mode) {
  if (pin >= 0 && pin < 64 && (mode == INPUT_PULLUP)) level[pin] = 1;
}
void digitalWrite(int pin, int v) {
  if (pin < 0 || pin >= 64) return;
  const uint8_t was = level[pin];
  level[pin] = v ? 1 : 0;
  if (pin == PIN_TC_CS_BOTTOM || pin == PIN_TC_CS_TOP || pin == PIN_TC_CS_MON) {
    const int ch = pin == PIN_TC_CS_BOTTOM ? 0 : pin == PIN_TC_CS_TOP ? 1 : 2;
    if (was && !v) frameStart(ch);
    else if (v) frameCh = -1;
  } else if (pin == PIN_TC_SCK && was && !v && frameCh >= 0) {
    ++frameBit;  // falling edge: the next bit
  }
}
int digitalRead(int pin) {
  if (pin == PIN_TC_SO) {
    if (frameCh < 0) return 1;  // (pull-up: no converter)
    if (frameBit >= frameBits) return 0;
    return (frame >> (frameBits - 1 - frameBit)) & 1;
  }
  if (pin == PIN_ESTOP) return estopPressed ? HIGH : LOW;  // NC contact to GND: open = E-stop
  return pin >= 0 && pin < 64 ? level[pin] : 0;
}
void ledcSetup(int, uint32_t, int) {}
void ledcAttachPin(int pin, int ch) {
  if (pin == PIN_BUZZER) buzzerCh = ch;
}
void ledcWrite(int ch, uint32_t duty) {
  if (ch >= 0 && ch < 16) ledcDuty[ch] = (int)duty;
}
float temperatureRead() { return 38.0f + (heat.cj - 26.0f) * 0.5f; }

// ================================================================== the device: boot and steps
static bool booted = false;
static uint64_t stepAt = 0;  // the last 10 ms boundary handled
static uint32_t restartAt = 0;

static void tick10() {
  // pins -> heater power for the last 10 ms (the SSRs switch at 10 ms steps, ssrTick)
  const bool relay = level[PIN_SSR_RELAY];
  const float pb = relay && level[PIN_SSR_BOTTOM] ? 1100.0f : 0, pt = relay && level[PIN_SSR_TOP] ? 450.0f : 0;
  heatStep(0.01f, pb, pt);
  powB += (pb - powB) * 0.01f;  // (shown averaged over about a second)
  powT += (pt - powT) * 0.01f;
  for (DemoTimer* t : timers)
    if (t->on && t->next <= nowUs) {
      t->cb(t->arg);
      if (t->period) t->next += t->period;
      else t->on = false;
    }
  if ((stepAt / 10000) % 5 == 0) localCtrlTick();  // the local task: every 50 ms
  inControl = true;
  controlTaskStep();
  inControl = false;
  // the loop task
  syslogLoop();
  webLoop();
  if (appRestartPending() && !restartAt) restartAt = millis();
}

// first = no file system from the page (first visit / after Reset demo): the demo's own starting setup
EXPORT("demo_boot") int demo_boot(double wallNow, int restarted) {
  wallBaseMs = wallNow - nowUs / 1000.0;
  wasRestart = restarted != 0;
  rng ^= (uint32_t)wallNow;
  localCtrlEarlyInit();
  safetyBegin();
  syslogBegin();
  LogSerial.printf("\n[boot] " PRODUCT_NAME " %s - browser demo (simulated hardware)\n", FW_VERSION);
  const bool first = !LittleFS.files.count("/duo/settings.json");
  // settings saved by a demo before the monitor channel (1.0.60): switch it on once, as on a first start
  bool noMon = false;
  if (!first) {
    const auto& f = *LittleFS.files["/duo/settings.json"];
    noMon = std::string(f.begin(), f.end()).find("\"monitor\"") == std::string::npos;
  }
  appInit();
  if (first) {  // the demo's first start: PID values from an autotune of the simulated heaters (the firmware's
                // defaults are placeholders until the user runs one)
    Guard g;
    LocalChannelSettings& b = gSettings.ch[CH_BOTTOM].loc;
    LocalChannelSettings& t = gSettings.ch[CH_TOP].loc;
    // (the "faster" rule, switch on: the slow one leaves the PV a few degrees short for minutes)
    b.kp = 8.7f, b.ki = 0.126f, b.kd = 134.0f;
    t.kp = 5.7f, t.ki = 0.166f, t.kd = 44.0f;
    b.tuneFast = t.tuneFast = 1;
  }
  if (first || noMon) {
    Guard g;
    gSettings.mon.on = 1;  // the third thermocouple at the board's edge
    strlcpy(gSettings.mon.name, "Board edge", sizeof(gSettings.mon.name));
  }
  if (first || noMon) saveSettingsQuiet();
  controlTaskBegin();
  webBegin();
  stepAt = nowUs / 50000 * 50000 - 10000;  // (the first step reads the thermocouples, then the control task runs)
  booted = true;
  return 0;
}

// advance the simulation by ms (the page calls this every frame, ms = real time x speed-up)
EXPORT("demo_run") int demo_run(uint32_t ms) {
  if (!booted) return -1;
  const uint64_t until = nowUs + (uint64_t)ms * 1000;
  while (stepAt + 10000 <= until) {
    stepAt += 10000;
    if (nowUs < stepAt) nowUs = stepAt;
    tick10();
  }
  if (nowUs < until) nowUs = until;
  if (restartAt && millis() - restartAt > 400) {
    syslogFlush();
    return 1;  // restart: the page saves the file system and starts the module again
  }
  return 0;
}

// a request line of the serial api ({"id","m","p","b"}): the answer lines
static std::string apiBuf;
void apiOut(const char* s, size_t n) { apiBuf.append(s, n); }
void apiFlush(size_t) {}
size_t apiQueued() { return 0; }
EXPORT("demo_request") const char* demo_request(const char* line) {
  apiBuf.clear();
  if (booted) webApiRequest(line);
  return apiBuf.c_str();
}
EXPORT("demo_request_len") uint32_t demo_request_len() { return (uint32_t)apiBuf.size(); }

// the demo bar: simulated faults and what the hardware does
// what: 1 E-stop pressed, 2 thermocouple open (ch), 3 speed-up (for the log)
EXPORT("demo_set") void demo_set(int what, int ch, float v) {
  if (what == 1) estopPressed = v != 0;
  else if (what == 2 && ch >= 0 && ch < 2) tcOpen[ch] = v != 0;
  else if (what == 3) speedUp = v;
}
static char stateBuf[512];
EXPORT("demo_state") const char* demo_state() {
  const bool buzz = buzzerCh >= 0 && ledcDuty[buzzerCh] > 0;
  snprintf(stateBuf, sizeof(stateBuf),
           "{\"ms\":%lu,\"buzz\":%d,\"relay\":%d,\"estop\":%d,\"tcOpen\":[%d,%d],\"pow\":[%.0f,%.0f],"
           "\"emit\":[%.1f,%.1f],\"board\":%.1f,\"gpu\":%.1f,\"fs\":%lu}",
           (unsigned long)millis(), buzz ? 1 : 0, level[PIN_SSR_RELAY], estopPressed ? 1 : 0, tcOpen[0], tcOpen[1], powB,
           powT, heat.hb, heat.ht, heat.b, heat.z, (unsigned long)fsGen);
  return stateBuf;
}
// the heat model's state, kept by the page over a restart and a reload: hb b ht z tcB tcT cj
EXPORT("demo_heat") float* demo_heat() { return &heat.hb; }
// the time the page was closed: the bodies cool down (heaters off)
EXPORT("demo_coast") void demo_coast(float sec) {
  if (sec > 4 * 3600) sec = 4 * 3600;
  for (float t = 0; t < sec; t += 0.5f) heatStep(0.5f, 0, 0);
}
