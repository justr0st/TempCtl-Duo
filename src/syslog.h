// System log (web System page, spec 7): every log line of the device - the events (logEvent) and the
// technical lines of the serial log ([wifi], [lcd], [mem] ...) - in a RAM ring (S3: 256 kB in PSRAM,
// CYD: 4 kB), each with a kind for the colours. The events are also saved to /duo/events.txt (at most
// every 30 s, and before a requested restart), so they survive a restart; each start is a new "boot".
#pragma once
#include <stddef.h>
#include <stdint.h>

enum LogKind : uint8_t { LK_INFO = 0, LK_EVENT = 1, LK_RUN = 2, LK_WARN = 3, LK_ERROR = 4 };

// kind of a log text by its words (errors, warnings); `def` otherwise
LogKind logClassify(const char* text, LogKind def);
const char* logKindName(LogKind k);  // "info", "event", "run", "warning", "error"
// length of s[0..n) without a UTF-8 character cut at the end (a text shortened to fit a buffer)
size_t utf8Fit(const char* s, size_t n);

void syslogBegin();                                  // after the file system (boot number, ring)
void syslogAdd(LogKind k, const char* text, size_t n);  // one line, any task
void syslogTee(const uint8_t* b, size_t n);          // raw serial log output (LogPort): lines, any task
uint32_t syslogSeq();                                // newest line (0: none)
uint32_t syslogFirst();                              // oldest line still kept
uint32_t syslogBoot();                               // number of this start (1, 2, ...)

struct SysLine {
  uint32_t seq, up, epoch;  // epoch 0: the time was not known
  LogKind kind;
  char text[200];
};
// the lines after `since`, oldest first, until `maxBytes` of text; returns how many were given to `fn`
int syslogRead(uint32_t since, size_t maxBytes, void (*fn)(const SysLine& l, void* user), void* user);

// wall clock (UTC seconds; 0 = not known yet): NTP once on Wi-Fi, or set by the web page
uint32_t syslogEpoch();
void syslogSetEpoch(uint32_t t);
void syslogStartNtp();

// events to flash (loop task): saves new ones at most every 30 s; syslogFlush() at once (restart). Also
// applies the time zone setting (localtime(): the SD run logs).
void syslogLoop();
void syslogFlush();
// the saved events file (all starts): lines "boot\tepoch\tuptime\tkind\ttext"
const char* syslogFile();
