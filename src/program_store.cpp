#include "program_store.h"
#include <Arduino.h>
#include <LittleFS.h>

static constexpr uint32_t PROG_MAGIC = 0x50524731;  // "PRG1"
static bool fsOk = false;

struct FileHeader {
  uint32_t magic;
  char name[PROG_NAME_LEN];
  uint8_t count;
  uint8_t reserved[3];
};

static void pathFor(int idx, char* out, size_t n) { snprintf(out, n, "/prog%d.bin", idx); }

static void defaultName(int idx, char* out, size_t n) { snprintf(out, n, "Program %d", idx + 1); }

// Example for the first start: lead-free (SAC305) BGA desolder profile for a GPU
// core on a rework station: preheat, soak, ramp to reflow, peak for removal.
// Targets in degrees C, times in seconds. Adapt to the station, the board and
// where the thermocouple sits.
void programExample(Program& p) {
  programClear(p, "GPU desolder (LeadFree)");
  const Step steps[] = {
      {160, 60, 30},  // preheat
      {190, 40, 40},  // soak
      {225, 30, 20},  // ramp towards liquidus (217 C)
      {245, 20, 40},  // peak: lift the chip
  };
  for (const Step& s : steps) programInsertStep(p, p.count, s);
}

static bool loadOne(int idx, Program& p) {
  char path[20];
  pathFor(idx, path, sizeof(path));
  if (!fsOk || !LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  FileHeader h;
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == PROG_MAGIC && h.count <= MAX_STEPS;
  if (ok) {
    programClear(p, nullptr);
    memcpy(p.name, h.name, sizeof(p.name));
    p.name[PROG_NAME_LEN - 1] = 0;
    const size_t len = sizeof(Step) * h.count;
    ok = f.read((uint8_t*)p.s, len) == len;
    p.count = ok ? h.count : 0;
  }
  f.close();
  return ok;
}

bool programStoreSave(int idx, const Program& p) {
  if (!fsOk || idx < 0 || idx >= MAX_PROGRAMS) return false;
  char path[20], tmp[24];
  pathFor(idx, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  FileHeader h{};
  h.magic = PROG_MAGIC;
  memcpy(h.name, p.name, sizeof(h.name));
  h.name[PROG_NAME_LEN - 1] = 0;
  h.count = p.count > MAX_STEPS ? MAX_STEPS : p.count;
  const size_t len = sizeof(Step) * h.count;
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) && f.write((const uint8_t*)p.s, len) == len;
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  // write-then-rename so a power loss never leaves a half-written program
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

bool storageBegin() {
  static bool tried = false;
  if (tried) return fsOk;
  tried = true;
  fsOk = LittleFS.begin(true);  // formats the partition on first use
  if (!fsOk) {
    Serial.println("[fs] LittleFS mount failed: settings and programs are not persistent");
    return false;
  }
  if (!LittleFS.exists("/duo")) LittleFS.mkdir("/duo");
  return true;
}

void programStoreBegin(Program* progs, const Program* firstBoot) {
  storageBegin();
  bool any = false;
  for (int i = 0; i < MAX_PROGRAMS; ++i) {
    if (loadOne(i, progs[i])) {
      any = true;
    } else {
      char n[PROG_NAME_LEN];
      defaultName(i, n, sizeof(n));
      programClear(progs[i], n);
    }
  }
  if (!any && firstBoot) {
    progs[0] = *firstBoot;
    programStoreSave(0, progs[0]);
  }
}
