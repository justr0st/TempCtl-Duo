// Event log (any task): logEvent / logRun / logWarn / logError / logKind / logAuto, read by logSince().
// Split from app_state.cpp.
#include "app_internal.h"
#include "psram.h"
#include "syslog.h"
#include "sdlog.h"
#include "console.h"
#include <stdarg.h>
#include <string.h>

// ---- event log (any task)

static PSRAM_VAR(LogEntry[LOG_N], logBuf);
static uint32_t logNext = 1;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;
volatile float logRunT = NAN;

// k: the entry's kind as the caller gives it (not guessed from the text: names are user text); auto:
// warnings and errors found by their words (only for texts the firmware writes itself, e.g. reset reasons)
static void logAddV(LogKind k, bool autoKind, const char* fmt, va_list ap) {
  char m[LOG_LEN];
  if (vsnprintf(m, sizeof(m), fmt, ap) >= (int)sizeof(m)) m[utf8Fit(m, sizeof(m) - 1)] = 0;
  const float rt = logRunT;
  if (autoKind) k = logClassify(m, k);
  LogSerial.printf("[event] %s\n", m);
  syslogAdd(k, m, strlen(m));
  sdlogEvent(k, m);  // (a row in the SD run log while a run is logged)
  portENTER_CRITICAL(&logMux);
  LogEntry& e = logBuf[logNext % LOG_N];
  e.seq = logNext++;
  e.upSec = millis() / 1000;
  e.epoch = syslogEpoch();
  e.runT = rt;
  e.kind = k;
  memcpy(e.msg, m, sizeof(e.msg));
  portEXIT_CRITICAL(&logMux);
}

#define LOG_FN(name, kind, autoKind)  \
  void name(const char* fmt, ...) {    \
    va_list ap;                        \
    va_start(ap, fmt);                 \
    logAddV(kind, autoKind, fmt, ap);  \
    va_end(ap);                        \
  }
LOG_FN(logEvent, LK_EVENT, false)
LOG_FN(logRun, LK_RUN, false)
LOG_FN(logWarn, LK_WARN, false)
LOG_FN(logError, LK_ERROR, false)
#undef LOG_FN

void logKind(LogKind k, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logAddV(k, false, fmt, ap);
  va_end(ap);
}

void logAuto(LogKind def, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  logAddV(def, true, fmt, ap);
  va_end(ap);
}

uint32_t logSeq() { return logNext - 1; }

int logSince(uint32_t seq, LogEntry* out, int max) {
  int n = 0;
  portENTER_CRITICAL(&logMux);
  uint32_t first = logNext > LOG_N ? logNext - LOG_N : 1;
  if (seq + 1 > first) first = seq + 1;
  for (uint32_t q = first; q < logNext && n < max; ++q) out[n++] = logBuf[q % LOG_N];
  portEXIT_CRITICAL(&logMux);
  return n;
}
