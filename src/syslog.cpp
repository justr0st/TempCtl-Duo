// System log: RAM ring of all log lines + events saved to flash (syslog.h)
#include "syslog.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include <time.h>
#include "app_state.h"
#include "program_store.h"

// ------------------------------------------------------------ kinds
static bool hasWord(const char* t, const char* const* words) {
  for (const char* const* w = words; *w; ++w)
    if (strcasestr(t, *w)) return true;
  return false;
}

LogKind logClassify(const char* t, LogKind def) {
  static const char* const ERR[] = {"error",   "failed", "fault",      "crash",          "panic",       "assert",
                                    "watchdog", "brown-out", "no reading", "out of memory", "not found",
                                    "does not answer", "does NOT answer", "runaway", "E-stop", nullptr};
  static const char* const WARN[] = {"warn",       "lost",        "timeout",     "timed out", "disconnected",
                                     "not connected", "trying again", "retry",    "dropped",   "interrupted",
                                     "did not help", "stuck",       "restarted automatically", "cannot",
                                     "not possible", "no answer",   "check off",  "not readable", nullptr};
  if (hasWord(t, ERR)) return LK_ERROR;
  if (hasWord(t, WARN)) return LK_WARN;
  return def;
}

const char* logKindName(LogKind k) {
  switch (k) {
    case LK_EVENT: return "event";
    case LK_RUN: return "run";
    case LK_WARN: return "warning";
    case LK_ERROR: return "error";
    default: return "info";
  }
}

size_t utf8Fit(const char* s, size_t n) {
  size_t k = 0;  // continuation bytes (10xxxxxx) at the end
  while (k < n && k < 3 && ((uint8_t)s[n - 1 - k] & 0xC0) == 0x80) ++k;
  if (k == n) return 0;
  const uint8_t lead = (uint8_t)s[n - 1 - k];
  const size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (want == 1) return n - k;  // (stray continuation bytes after an ASCII character)
  return k + 1 < want ? n - 1 - k : n;
}

// ------------------------------------------------------------ the ring
// Records [len u8][kind u8][up u32][epoch u32][text len], written and read with wrap-around; the oldest
// ones make room for new ones.
#ifdef BOARD_HAS_PSRAM
static constexpr size_t RING = 256 * 1024;
#else
static constexpr size_t RING = 4 * 1024;  // CYD: DRAM is short (about 50 lines)
#endif
static constexpr size_t HDR = 10;
static uint8_t* ring = nullptr;
static size_t head = 0, tail = 0, used = 0;  // oldest record, next write, bytes in use
static uint32_t seqFirst = 1, seqNext = 1;   // seq of the oldest record, of the next one
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t bootNo = 0;

static bool ringAlloc() {
  if (ring) return true;
#ifdef BOARD_HAS_PSRAM
  ring = (uint8_t*)heap_caps_malloc(RING, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  ring = (uint8_t*)malloc(RING);
#endif
  return ring != nullptr;
}

static void put(size_t at, const void* src, size_t n) {
  const uint8_t* s = (const uint8_t*)src;
  for (size_t i = 0; i < n; ++i) ring[(at + i) % RING] = s[i];
}
static void get(size_t at, void* dst, size_t n) {
  uint8_t* d = (uint8_t*)dst;
  for (size_t i = 0; i < n; ++i) d[i] = ring[(at + i) % RING];
}

uint32_t syslogEpoch() {
  const time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;  // (before: the clock was not set)
}

void syslogSetEpoch(uint32_t t) {
  if (t < 1700000000 || syslogEpoch()) return;  // NTP (or an earlier page) set it already
  timeval tv = {(time_t)t, 0};
  settimeofday(&tv, nullptr);
}

void syslogStartNtp() { configTime(0, 0, "pool.ntp.org", "time.google.com"); }

void syslogAdd(LogKind k, const char* text, size_t n) {
  while (n && ((uint8_t)*text & 0xC0) == 0x80) ++text, --n;  // (the rest of a character cut off before)
  while (n && (text[n - 1] == '\r' || text[n - 1] == '\n' || text[n - 1] == ' ')) --n;
  n = utf8Fit(text, n > 199 ? 199 : n);  // (also a long serial line split by syslogTee)
  if (!n) return;
  const uint32_t up = millis() / 1000, ep = syslogEpoch();
  if (!ring && !ringAlloc()) return;  // (no malloc inside the critical section)
  portENTER_CRITICAL(&mux);
  if (ring) {
    const size_t need = HDR + n;
    while (used && RING - used < need) {  // drop the oldest
      uint8_t l;
      get(head, &l, 1);
      head = (head + HDR + l) % RING;
      used -= HDR + l;
      ++seqFirst;
    }
    uint8_t h[HDR];
    h[0] = (uint8_t)n;
    h[1] = (uint8_t)k;
    memcpy(h + 2, &up, 4);
    memcpy(h + 6, &ep, 4);
    put(tail, h, HDR);
    put(tail + HDR, text, n);
    tail = (tail + need) % RING;
    used += need;
    ++seqNext;
  }
  portEXIT_CRITICAL(&mux);
}

// the serial log, put together line by line (logEvent's "[event]" lines are stored by logEvent itself)
static char teeLine[200];
static size_t teeLen = 0;
static portMUX_TYPE teeMux = portMUX_INITIALIZER_UNLOCKED;

void syslogTee(const uint8_t* b, size_t n) {
  char done[sizeof(teeLine)];
  for (size_t i = 0; i < n; ++i) {
    size_t emit = 0;
    portENTER_CRITICAL(&teeMux);
    if (b[i] == '\n' || teeLen >= sizeof(teeLine) - 1) {
      emit = teeLen;
      memcpy(done, teeLine, teeLen);
      teeLen = 0;
    }
    if (b[i] != '\n' && b[i] != '\r') teeLine[teeLen++] = (char)b[i];
    portEXIT_CRITICAL(&teeMux);
    if (emit) {
      done[emit] = 0;
      if (strncmp(done, "[event] ", 8)) syslogAdd(logClassify(done, LK_INFO), done, emit);
    }
  }
}

uint32_t syslogSeq() { return seqNext - 1; }
uint32_t syslogFirst() { return seqFirst; }
uint32_t syslogBoot() { return bootNo; }

int syslogRead(uint32_t since, size_t maxBytes, void (*fn)(const SysLine& l, void* user), void* user) {
  SysLine* l = (SysLine*)malloc(sizeof(SysLine));
  if (!l) return 0;
  int count = 0;
  size_t bytes = 0;
  uint32_t want = since + 1;
  size_t pos = 0;      // offset of record posSeq (records keep their place until they are dropped)
  uint32_t posSeq = 0;  // 0: not known yet
  for (;;) {
    bool got = false;
    portENTER_CRITICAL(&mux);
    if (ring && want < seqNext) {
      if (want < seqFirst) want = seqFirst;
      if (!posSeq || posSeq < seqFirst || posSeq > want) {  // (start, or dropped meanwhile): from the oldest
        pos = head;
        posSeq = seqFirst;
      }
      while (posSeq < want) {
        uint8_t n;
        get(pos, &n, 1);
        pos = (pos + HDR + n) % RING;
        ++posSeq;
      }
      uint8_t h[HDR];
      get(pos, h, HDR);
      l->seq = want;
      l->kind = (LogKind)h[1];
      memcpy(&l->up, h + 2, 4);
      memcpy(&l->epoch, h + 6, 4);
      get(pos + HDR, l->text, h[0]);
      l->text[h[0]] = 0;
      got = true;
    }
    portEXIT_CRITICAL(&mux);
    if (!got) break;
    fn(*l, user);
    ++count;
    ++want;
    bytes += strlen(l->text) + 32;
    if (bytes >= maxBytes) break;
  }
  free(l);
  return count;
}

// ------------------------------------------------------------ events to flash
static const char* const FILE_EV = "/duo/events.txt";
static const char* const FILE_TMP = "/duo/events.tmp";
static const char* const FILE_BOOT = "/duo/boot.txt";
static constexpr size_t FILE_MAX = 40 * 1024;  // then the oldest are dropped (300 events are kept)
static constexpr int FILE_KEEP = 300;
static uint32_t savedSeq = 0, savedAt = 0;
static bool fsOk = false;

const char* syslogFile() { return FILE_EV; }

void syslogBegin() {
  ringAlloc();
  fsOk = storageBegin();
  if (!fsOk) return;
  if (fsExists(FILE_BOOT)) {
    File f = LittleFS.open(FILE_BOOT, "r");
    if (f) {
      bootNo = f.readString().toInt();
      f.close();
    }
  }
  ++bootNo;
  File f = LittleFS.open(FILE_BOOT, "w");
  if (f) {
    f.print(bootNo);
    f.close();
  }
}

static void trimFile() {  // keep the newest FILE_KEEP lines
  File in = LittleFS.open(FILE_EV, "r");
  if (!in) return;
  int lines = 0;
  while (in.available())
    if (in.read() == '\n') ++lines;
  in.seek(0);
  int skip = lines - FILE_KEEP;
  File out = LittleFS.open(FILE_TMP, "w");
  if (!out) return in.close();
  char buf[256];
  while (in.available()) {
    const size_t n = in.readBytesUntil('\n', buf, sizeof(buf) - 1);
    if (skip > 0) {
      --skip;
      continue;
    }
    out.write((const uint8_t*)buf, n);
    out.write('\n');
  }
  in.close();
  out.close();
  LittleFS.remove(FILE_EV);
  LittleFS.rename(FILE_TMP, FILE_EV);
}

void syslogFlush() {
  if (!fsOk || logSeq() <= savedSeq) return;
  LogEntry* e = (LogEntry*)malloc(sizeof(LogEntry) * LOG_N);
  if (!e) return;
  const int n = logSince(savedSeq, e, LOG_N);
  File f = LittleFS.open(FILE_EV, "a");
  if (f) {
    for (int i = 0; i < n; ++i) {
      char line[LOG_LEN + 48];
      const int k = snprintf(line, sizeof(line), "%lu\t%lu\t%lu\t%u\t%s\n", (unsigned long)bootNo,
                             (unsigned long)e[i].epoch, (unsigned long)e[i].upSec, (unsigned)e[i].kind, e[i].msg);
      f.write((const uint8_t*)line, k);
      savedSeq = e[i].seq;
    }
    const size_t size = f.size();
    f.close();
    if (size > FILE_MAX) trimFile();
  }
  free(e);
  savedAt = millis();
  if (!savedAt) savedAt = 1;
}

// the time zone setting for localtime() (SD run logs): applied when it changes (checked every second)
static void tzApply() {
  static uint32_t at = 0;
  static char now[sizeof(gSettings.tzName)] = "\x01";  // (nothing applied yet)
  if (now[0] != 1 && millis() - at < 1000) return;
  at = millis();
  char name[sizeof(gSettings.tzName)];
  {
    Guard g;
    strlcpy(name, gSettings.tzName, sizeof(name));
  }
  if (!strcmp(name, now)) return;
  strlcpy(now, name, sizeof(now));
  char rule[48];
  if (!tzLookup(name, rule, sizeof(rule))) strlcpy(rule, "UTC0", sizeof(rule));
  setenv("TZ", rule, 1);
  tzset();
}

void syslogLoop() {
  tzApply();
  if (!fsOk || logSeq() <= savedSeq) return;
  if (savedAt && millis() - savedAt < 30000) return;
  syslogFlush();
}
