#include "program_store.h"
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
  if (!fsOk || !LittleFS.exists(path)) {
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
  if (!ok || (LittleFS.exists(path) && !LittleFS.remove(path)) || !LittleFS.rename(tmp, path)) {
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
  char path[24];
  pathFor(idx, path, sizeof(path));
  if (LittleFS.exists(path) && !LittleFS.remove(path)) return false;
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
    char path[24];
    pathFor(i, path, sizeof(path));
    ProgSummary s;
    memset(&s, 0, sizeof(s));
    if (fsOk && LittleFS.exists(path)) {
      any = true;
      if (programLoad(i, *p, err, sizeof(err))) {
        programSummarize(*p, s);
      } else {
        Serial.printf("[prog] %s\n", err);  // the slot shows as empty; the file is kept
      }
    }
    SumGuard g;
    sums[i] = s;
  }
  if (!any && fsOk) {
    for (int i = 0; i < PROG_EXAMPLES; ++i) {
      progExample(i, *p);
      if (!programSave(i, *p, err, sizeof(err))) Serial.printf("[prog] example %d: %s\n", i + 1, err);
    }
    Serial.println("[prog] example programs created");
  }
  free(p);
}
