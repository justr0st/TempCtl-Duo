// Breadcrumbs for watchdog resets and crashes (crumbs.h)
#include "crumbs.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <string.h>

namespace {
struct Crumbs {
  uint32_t magic;
  uint8_t code[CR_N];
  int16_t arg[CR_N];
  uint32_t ms[CR_N];  // when set
  uint32_t last;      // newest of ms[]
  char web[28];
  uint32_t sum;
};
constexpr uint32_t MAGIC = 0x43524D42;  // "CRMB"
}  // namespace

RTC_NOINIT_ATTR static Crumbs rc;
static Crumbs prev;          // the previous run's, read once at the first call
static bool prevRead = false;

static uint32_t sumOf(const Crumbs& c) {
  const uint8_t* p = (const uint8_t*)&c;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < offsetof(Crumbs, sum); ++i) h = (h ^ p[i]) * 16777619u;
  return h;
}

static void readPrev() {
  if (prevRead) return;
  prevRead = true;
  if (rc.magic == MAGIC && rc.sum == sumOf(rc)) prev = rc;
  else memset(&prev, 0, sizeof(prev));
  memset(&rc, 0, sizeof(rc));
  rc.magic = MAGIC;
}

void crumb(CrumbSlot s, CrumbCode c, int16_t arg) {
  if (!prevRead) readPrev();
  const uint32_t now = millis();
  rc.code[s] = c;
  rc.arg[s] = arg;
  rc.ms[s] = now;
  rc.last = now;
  rc.sum = sumOf(rc);  // (a reset in between: the sum does not match and nothing is reported)
}

void crumbWeb(const char* path) {
  if (!prevRead) readPrev();
  strlcpy(rc.web, path ? path : "", sizeof(rc.web));
  crumb(CR_LOOP, L_WEB_REQ);
}

CrumbCode crumbGet(CrumbSlot s) { return (CrumbCode)rc.code[s]; }

static const char* name(uint8_t c) {
  switch (c) {
    case L_SYSLOG: return "event save";
    case L_UI: return "UI";
    case L_SCREENCHECK: return "screen check";
    case L_LVGL: return "LVGL draw";
    case L_LCD_WAIT: return "LCD SPI wait";
    case L_CONSOLE: return "console";
    case L_WEB: return "web";
    case L_WEB_REQ: return "web";
    case L_END: return "loop end";
    case C_WAIT: return "waiting";
    case C_CMD: return "cmd";
    case C_REQUEST: return "request";
    case C_POLL: return "poll";
    case C_CYCLE: return "cycle";
    case S_IDLE: return "idle";
    case S_LOCK: return "bus wait";
    case S_MOUNT: return "mount";
    case S_WORK: return "read/write";
    case S_UNMOUNT: return "unmount";
    default: return "?";
  }
}

bool crumbReport(char* b, size_t n) {
  readPrev();
  if (!prev.magic) return false;
  static const char* const WHO[CR_N] = {"loop", "control", "SD"};
  size_t k = 0;
  if (n) b[0] = 0;
  for (int s = 0; s < CR_N && k < n; ++s) {
    if (!prev.code[s]) continue;
    const uint8_t c = prev.code[s];
    char extra[40] = "";
    if (c == L_WEB_REQ) snprintf(extra, sizeof(extra), " %s", prev.web);
    else if (c == C_CMD) snprintf(extra, sizeof(extra), " %d", prev.arg[s]);
    k += snprintf(b + k, n - k, "%s%s: %s%s %.1fs", k ? "; " : "", WHO[s], name(c), extra,
                  (prev.last - prev.ms[s]) / 1000.0f);
  }
  memset(&prev, 0, sizeof(prev));
  return true;
}
