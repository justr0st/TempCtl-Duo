// Run logs on the SD card (S3 only, spec 9.9): one CSV file per program run in /runs on the card,
// a row every second (both channels: PV, active SP, program SP, output, state; phase / step) plus a
// row for each event during the run. Rows collect in a PSRAM buffer and are written every 30 s and at
// the end of the run. The card shares the SPI bus with the display, whose screen check frees the bus
// for its status read: the card is mounted only while it is used, under lcdBusLock().
// Boards without an SD slot (CYD: its SD pins carry the thermocouples): everything is a no-op.
#pragma once
#include <stddef.h>
#include <stdint.h>

bool sdlogSlot();   // the board has an SD slot
void sdlogBegin();  // starts the writer task (after the display)

// the run (control task)
void sdlogRunStart(const char* prog, bool timeline, bool resumed);
struct SdRow {
  float runS;          // run time
  char where[40];      // "P3 ramp" / "B2 hold / T4 ramp"
  float pv[2], sp[2], prog[2], out[2];  // NAN = none
  const char* state[2];
};
void sdlogRow(const SdRow& r);
void sdlogRunEnd();
// an event (logEvent / logRun, any task): a row while a run is logged; kind: LogKind
void sdlogEvent(uint8_t kind, const char* text);

struct SdState {
  bool slot;         // board has a slot
  int8_t card;       // 1 found at the last access, 0 not found, -1 not tried yet
  bool logging;      // a run is being logged
  char file[80];     // its file ("runs/...csv")
  char err[64];      // last problem ("" = none)
  uint32_t buffered; // bytes not written yet
};
void sdlogState(SdState& s);

// web (System page): the run files, newest first; size, file time (UTC s, 0 = unknown)
struct SdFile {
  char name[64];
  uint32_t size, mtime;
};
// fills up to max entries; returns the count (-1: no card / error -> err). total/free in kB (0 = unknown)
int sdlogList(SdFile* out, int max, uint32_t& totalKb, uint32_t& freeKb, char* err, size_t n);
// the whole file into a PSRAM buffer (free() it); null + err if missing / too big / no card
char* sdlogLoad(const char* name, size_t& len, char* err, size_t n);
bool sdlogDelete(const char* name, char* err, size_t n);

// firmware update from the card (S3, spec 9.11): /firmware.bin in the card's root
struct SdFirmware {
  bool found;
  uint32_t size;
  char board[32], version[16];
  char problem[96];  // "" = this device can install it
};
bool sdlogFirmwareCheck(SdFirmware& f, char* err, size_t n);  // reads the whole file (a few seconds)
bool sdlogFirmwareInstall(char* err, size_t n);              // starts it in the SD task (progress: otaStatus)
