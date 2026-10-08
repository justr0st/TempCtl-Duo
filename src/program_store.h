// Program files on LittleFS: /duo/progN.json (N = 0 .. PROG_MAX-1), the same
// JSON as the web API (spec 9.3.5). An empty slot has no file. Safe save:
// progN.tmp, then rename. The example programs (spec 9.9) are created when no
// program file exists at all (first start).
#pragma once
#include <stddef.h>
#include "duo_program.h"
#include "run_engine.h"

// What the program lists show without loading every program
struct ProgSummary {
  char name[PROG_NAME_LEN];
  uint8_t used;     // the slot has a program
  uint8_t type;     // ProgType
  uint8_t uses;     // bit 0: bottom has phases/steps, bit 1: top
  uint8_t waits;    // has "wait for bottom" phases
  uint16_t items;   // phases (Sequence) / steps of both channels (Timeline)
  uint16_t steps[NUM_CHANNELS];  // Timeline: steps per channel
  float total;      // planned seconds (waits and pauses not counted)
};

// Mounts LittleFS (formats it on first use) and creates /duo. Safe to call
// more than once. False = no file system (settings and programs not persistent).
bool storageBegin();
bool fsExists(const char* path);  // without the error log of LittleFS.exists()

// Reads the summaries of all slots; creates the examples on the first start.
void programStoreBegin();
bool programLoad(int idx, DuoProgram& p, char* err, size_t n);
bool programSave(int idx, const DuoProgram& p, char* err, size_t n);
bool programDelete(int idx);
void programSummaries(ProgSummary* out);   // copy of all PROG_MAX summaries
bool programSummary(int idx, ProgSummary& out);
uint32_t programStoreGen();                // changes whenever a program is saved or deleted
void programSummarize(const DuoProgram& p, ProgSummary& s);

// "Run interrupted by a reset" (spec 9.3.5). /duo/run.json: the running copy and what resuming needs
// (written at the start and after each run edit, removed at the end of the run); /duo/runpos.json:
// where the run was (written when the phase / step changes and every minute; the exact position is
// kept in RTC memory, which survives resets but not a power loss).
struct RunFileInfo {
  int8_t slot;                          // program slot
  uint32_t id;                          // run id (matches the RTC record)
  uint8_t edited;                       // the running copy has unsaved run edits
  uint8_t rampSaved[NUM_CHANNELS];      // controller gradient to restore at the end
  int16_t rampOrig[NUM_CHANNELS];
};
bool runFileSave(const RunFileInfo& i, const DuoProgram& p);
bool runPosSave(uint32_t id, const RunPos& r);
// false: no interrupted run. havePos: runpos.json of this run was there
bool runFileLoad(RunFileInfo& i, DuoProgram& p, RunPos& r, bool& havePos);
void runFileClear();
