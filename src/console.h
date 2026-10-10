// Serial console (spec 7a): human commands for manual control, Wi-Fi setup, programs and run control.
// S3: the native USB port (TinyUSB CDC, control only; the log stays on the UART port).
// CYD: its only USB serial, shared with the log.
#pragma once
#include <Arduino.h>

// The log port. Log lines go through it so that on a shared port (CYD) a line that arrives while
// someone types appears on its own line and the typed text is shown again afterwards.
class LogPort : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override;
};
extern LogPort LogSerial;

void consoleBegin();  // after the network and the web server
void consoleLoop();   // loop task: reads commands, prints results and run events (never blocks)

// api mode (spec 7b), for the web API's serial answers (web.cpp): queue output lines; apiFlush() writes
// until at most `keep` bytes are queued (waits for the port, feeds the loop watchdog; for long
// answers sent piece by piece)
void apiOut(const char* s, size_t n);
void apiFlush(size_t keep);
size_t apiQueued();
