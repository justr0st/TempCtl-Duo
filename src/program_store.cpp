#include "program_store.h"
#include "console.h"
#include <sys/stat.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "run_engine.h"

static bool fsOk = false;
static ProgSummary sums[PROG_MAX];
static SemaphoreHandle_t sumLock = nullptr;
static volatile uint32_t storeGen = 1;

struct SumGuard {
  SumGuard() { xSemaphoreTake(sumLock, portMAX_DELAY); }
  ~SumGuard() { xSemaphoreGive(sumLock); }
};

static void pathFor(int idx, char* out, size_t n) { snprintf(out, n, "/duo/prog%d.json", idx); }

// LittleFS.exists() of Arduino-ESP32 2.0.x opens the file and logs an error when it is missing
// ("does not exist, no permits for creation"); stat() asks quietly
bool fsExists(const char* path) {
  char full[48];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return stat(full, &st) == 0;
}

// Safe save, step 2: the written temp file replaces the old one. LittleFS renames over an existing file
// in one step, so there is always either the old or the new file; only if the rename is refused, the old
// file is removed first (then a power cut in between leaves just the temp file: fsRecoverTmp() at boot).
static bool replaceFile(const char* tmp, const char* path) {
  if (LittleFS.rename(tmp, path)) return true;
  if (fsExists(path) && !LittleFS.remove(path)) return false;
  return LittleFS.rename(tmp, path);
}

// At boot: a temp file without its target = a save cut off between remove and rename. Taken over if it
// is complete JSON, else removed. A temp file next to its target is a save cut off before the rename: the
// target (the old content) is kept, the temp file removed.
void fsRecoverTmp(const char* path, const char* tmp) {
  if (!fsExists(tmp)) return;
  if (!fsExists(path)) {
    File f = LittleFS.open(tmp, "r");
    JsonDocument d;
    const bool good = f && !deserializeJson(d, f) && d.is<JsonObject>();
    if (f) f.close();
    if (good && LittleFS.rename(tmp, path)) {
      LogSerial.printf("[fs] %s restored from %s (a save was cut off)\n", path, tmp);
      return;
    }
  }
  LittleFS.remove(tmp);
}

bool storageBegin() {
  static bool tried = false;
  if (tried) return fsOk;
  tried = true;
  fsOk = LittleFS.begin(true);  // formats the partition on first use
  if (!fsOk) {
    LogSerial.println("[fs] LittleFS mount failed: settings and programs are not persistent");
    return false;
  }
  if (!fsExists("/duo")) LittleFS.mkdir("/duo");
  return true;
}

void programSummarize(const DuoProgram& p, ProgSummary& s) {
  memset(&s, 0, sizeof(s));
  strlcpy(s.name, p.name, sizeof(s.name));
  s.used = 1;
  s.type = p.type;
  s.items = (uint16_t)progItems(p);
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (progUses(p, ch)) s.uses |= (uint8_t)(1 << ch);
    s.steps[ch] = p.type == PROG_TL ? p.nSt[ch] : 0;
  }
  if (p.type == PROG_SEQ)
    for (int i = 0; i < p.nPh; ++i)
      if (p.ph[i].wait) s.waits = 1;
  s.total = programPreview(p, nullptr, nullptr, nullptr);
}

bool programLoad(int idx, DuoProgram& p, char* err, size_t n) {
  if (idx < 0 || idx >= PROG_MAX) {
    snprintf(err, n, "Program number out of range");
    return false;
  }
  char path[24];
  pathFor(idx, path, sizeof(path));
  if (!fsOk || !fsExists(path)) {
    snprintf(err, n, "Program %d is empty", idx + 1);
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    snprintf(err, n, "Cannot open %s", path);
    return false;
  }
  JsonDocument d;
  const DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) {
    snprintf(err, n, "%s: %s", path, e.c_str());
    return false;
  }
  return progFromJson(d.as<JsonObjectConst>(), p, err, n);
}

bool programSave(int idx, const DuoProgram& p, char* err, size_t n) {
  if (idx < 0 || idx >= PROG_MAX) {
    snprintf(err, n, "Program number out of range");
    return false;
  }
  if (!fsOk) {
    snprintf(err, n, "No file system");
    return false;
  }
  char path[24], tmp[28];
  pathFor(idx, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "/duo/prog%d.tmp", idx);
  JsonDocument d;
  progToJson(p, d.to<JsonObject>());
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    snprintf(err, n, "Cannot write %s", tmp);
    return false;
  }
  const size_t len = measureJson(d);
  const bool ok = serializeJson(d, f) == len;
  f.close();
  // write-then-rename so a power loss never leaves a half-written program
  if (!ok || !replaceFile(tmp, path)) {
    LittleFS.remove(tmp);
    snprintf(err, n, "Writing %s failed (flash full?)", path);
    return false;
  }
  ProgSummary s;
  programSummarize(p, s);
  {
    SumGuard g;
    sums[idx] = s;
  }
  ++storeGen;
  return true;
}

bool programDelete(int idx) {
  if (idx < 0 || idx >= PROG_MAX || !fsOk) return false;
  char path[24], tmp[28];
  pathFor(idx, path, sizeof(path));
  if (fsExists(path) && !LittleFS.remove(path)) return false;
  snprintf(tmp, sizeof(tmp), "/duo/prog%d.tmp", idx);
  if (fsExists(tmp)) LittleFS.remove(tmp);  // (a leftover would come back at the next boot)
  {
    SumGuard g;
    memset(&sums[idx], 0, sizeof(sums[idx]));
  }
  ++storeGen;
  return true;
}

void programSummaries(ProgSummary* out) {
  SumGuard g;
  memcpy(out, sums, sizeof(sums));
}

bool programSummary(int idx, ProgSummary& out) {
  if (idx < 0 || idx >= PROG_MAX) return false;
  SumGuard g;
  out = sums[idx];
  return out.used;
}

uint32_t programStoreGen() { return storeGen; }

void programStoreBegin() {
  if (!sumLock) sumLock = xSemaphoreCreateMutex();
  storageBegin();
  DuoProgram* p = (DuoProgram*)malloc(sizeof(DuoProgram));
  if (!p) return;
  char err[96];
  bool any = false;
  for (int i = 0; i < PROG_MAX; ++i) {
    char path[24], tmp[28];
    pathFor(i, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "/duo/prog%d.tmp", i);
    if (fsOk) fsRecoverTmp(path, tmp);
    ProgSummary s;
    memset(&s, 0, sizeof(s));
    if (fsOk && fsExists(path)) {
      any = true;
      if (programLoad(i, *p, err, sizeof(err))) {
        programSummarize(*p, s);
      } else {
        LogSerial.printf("[prog] %s\n", err);  // the slot shows as empty; the file is kept
      }
    }
    SumGuard g;
    sums[i] = s;
  }
  if (!any && fsOk) {
    for (int i = 0; i < PROG_EXAMPLES; ++i) {
      progExample(i, *p);
      if (!programSave(i, *p, err, sizeof(err))) LogSerial.printf("[prog] example %d: %s\n", i + 1, err);
    }
    LogSerial.println("[prog] example programs created");
  }
  free(p);
}

// ------------------------------------------------------------ run files

static bool saveJson(const char* path, const char* tmp, JsonDocument& d) {
  if (!fsOk) return false;
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const size_t len = measureJson(d);
  const bool ok = serializeJson(d, f) == len;
  f.close();
  if (!ok || !replaceFile(tmp, path)) {
    LittleFS.remove(tmp);
    return false;
  }
  return true;
}

bool runFileSave(const RunFileInfo& i, const DuoProgram& p) {
  JsonDocument d;
  d["slot"] = i.slot;
  d["id"] = i.id;
  d["edited"] = i.edited != 0;
  JsonArray r = d["ramp"].to<JsonArray>();
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    if (i.rampSaved[ch]) r.add(i.rampOrig[ch]);
    else r.add(nullptr);
  }
  progToJson(p, d["program"].to<JsonObject>());
  const bool ok = saveJson("/duo/run.json", "/duo/run.tmp", d);
  if (!ok) LogSerial.println("[run] writing /duo/run.json failed");
  return ok;
}

bool runPosSave(uint32_t id, const RunPos& r) {
  JsonDocument d;
  d["id"] = id;
  d["el"] = r.el;
  JsonArray a = d["ch"].to<JsonArray>();
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    JsonArray c = a.add<JsonArray>();
    c.add(r.i[ch]);
    c.add(r.st[ch]);
    c.add(r.left[ch]);
    c.add(r.slope[ch]);
  }
  return saveJson("/duo/runpos.json", "/duo/runpos.tmp", d);
}

bool runFileLoad(RunFileInfo& i, DuoProgram& p, RunPos& r, bool& havePos) {
  memset(&i, 0, sizeof(i));
  memset(&r, 0, sizeof(r));
  havePos = false;
  if (!fsOk) return false;
  fsRecoverTmp("/duo/run.json", "/duo/run.tmp");
  fsRecoverTmp("/duo/runpos.json", "/duo/runpos.tmp");
  if (!fsExists("/duo/run.json")) return false;
  JsonDocument d;
  File f = LittleFS.open("/duo/run.json", "r");
  if (!f) return false;
  const DeserializationError e = deserializeJson(d, f);
  f.close();
  char err[96];
  if (e || !progFromJson(d["program"].as<JsonObjectConst>(), p, err, sizeof(err))) {
    LogSerial.printf("[run] /duo/run.json unreadable (%s) - removed\n", e ? e.c_str() : err);
    runFileClear();
    return false;
  }
  i.slot = d["slot"] | -1;
  i.id = d["id"] | 0u;
  i.edited = (d["edited"] | false) ? 1 : 0;
  for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
    JsonVariantConst v = d["ramp"][ch];
    i.rampSaved[ch] = v.is<int>() ? 1 : 0;
    i.rampOrig[ch] = v.is<int>() ? (int16_t)v.as<int>() : 0;
  }
  if (fsExists("/duo/runpos.json")) {
    JsonDocument q;
    File g = LittleFS.open("/duo/runpos.json", "r");
    if (g && !deserializeJson(q, g) && (q["id"] | 0u) == i.id) {
      r.el = q["el"] | 0.0f;
      for (int ch = 0; ch < NUM_CHANNELS; ++ch) {
        r.i[ch] = q["ch"][ch][0] | 0;
        r.st[ch] = q["ch"][ch][1] | 0;
        r.left[ch] = q["ch"][ch][2] | 0.0f;
        r.slope[ch] = q["ch"][ch][3] | 0.0f;
      }
      havePos = true;
    }
    if (g) g.close();
  }
  return true;
}

void runFileClear() {
  if (!fsOk) return;
  LittleFS.remove("/duo/run.json");
  LittleFS.remove("/duo/runpos.json");
  if (fsExists("/duo/run.tmp")) LittleFS.remove("/duo/run.tmp");  // (would come back at the next boot)
  if (fsExists("/duo/runpos.tmp")) LittleFS.remove("/duo/runpos.tmp");
}
