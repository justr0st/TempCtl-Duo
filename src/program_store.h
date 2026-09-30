// Program files on LittleFS: /duo/progN.json (N = 0 .. PROG_MAX-1), the same
// JSON as the web API (spec 9.3.5). An empty slot has no file. Safe save:
// progN.tmp, then rename. The example programs (spec 9.9) are created when no
// program file exists at all (first start).
#pragma once
#include <stddef.h>
#include "duo_program.h"

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

// Reads the summaries of all slots; creates the examples on the first start.
void programStoreBegin();
bool programLoad(int idx, DuoProgram& p, char* err, size_t n);
bool programSave(int idx, const DuoProgram& p, char* err, size_t n);
bool programDelete(int idx);
void programSummaries(ProgSummary* out);   // copy of all PROG_MAX summaries
bool programSummary(int idx, ProgSummary& out);
uint32_t programStoreGen();                // changes whenever a program is saved or deleted
void programSummarize(const DuoProgram& p, ProgSummary& s);
