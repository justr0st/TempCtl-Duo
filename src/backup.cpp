// Backup of the settings and all programs on the SD card (backup.h)
#include "backup.h"
#include "config.h"

#if HAS_SD
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "app_state.h"
#include "program_store.h"
#include "settings_store.h"
#include "syslog.h"
#include "version.h"

// one backup operation at a time (web / console: loop task, touch: display task); the statics below
// (large settings copies) rely on it
static portMUX_TYPE bmux = portMUX_INITIALIZER_UNLOCKED;
static bool busy = false;
struct Busy {
  bool got;
  Busy() {
    portENTER_CRITICAL(&bmux);
    got = !busy;
    busy = true;
    portEXIT_CRITICAL(&bmux);
  }
  ~Busy() {
    if (got) busy = false;
  }
};

static bool runIsActive() {
  Guard g;
  return gLive.state == RunState::Running || gLive.state == RunState::Paused;
}

bool backupCreate(char* name, size_t nn, char* err, size_t n) {
  if (nn) name[0] = 0;
  Busy b;
  if (!b.got) return snprintf(err, n, "another backup is being made or restored - try again"), false;
  JsonDocument d;
  d["fmt"] = 1;
  d["kind"] = "tempctl-duo-backup";
  d["fw"] = FW_VERSION;
  d["board"] = BOARD_ID;
  char base[40], when[24] = "";
  const uint32_t ep = syslogEpoch();
  if (ep) {  // local time (time zone setting)
    const time_t t = ep;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(base, sizeof(base), "%Y-%m-%d_%H%M%S", &tm);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
  } else {  // no clock yet: the start number
    static uint32_t k = 0;
    snprintf(base, sizeof(base), "start%lu_%lu", (unsigned long)syslogBoot(), (unsigned long)++k);
  }
  d["time"] = when;
  {
    static Settings S;  // (large; one caller at a time: Busy)
    {
      Guard g;
      S = gSettings;
    }
    d["host"] = S.hostname;
    JsonObject s = d["settings"].to<JsonObject>();
    settingsToJson(S, s, true);
    JsonObject w = s["wifi"];
    if (w) {
      w.remove("passSet");
      w.remove("apPassSet");
    }
  }
  JsonArray a = d["programs"].to<JsonArray>();
  ScratchProg sp;
  if (!sp) return snprintf(err, n, "out of memory"), false;
  int count = 0;
  for (int i = 0; i < PROG_MAX; ++i) {
    char e[96];
    if (!programLoad(i, *sp, e, sizeof(e))) continue;  // (empty slot)
    JsonObject o = a.add<JsonObject>();
    o["i"] = i;
    progToJson(*sp, o["program"].to<JsonObject>());
    ++count;
  }
  if (d.overflowed()) return snprintf(err, n, "out of memory"), false;
  const size_t len = measureJsonPretty(d);
  char* buf = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return snprintf(err, n, "out of memory"), false;
  serializeJsonPretty(d, buf, len + 1);
  d.clear();
  if (!sdlogBackupWrite(buf, len, base, name, nn, err, n)) return false;  // (buf taken over)
  logEvent("Backup on the SD card: backup/%s (settings and %d program%s)", name, count, count == 1 ? "" : "s");
  return true;
}

// the backup file, parsed (d) and checked
static bool loadBackup(const char* name, JsonDocument& d, char* err, size_t n) {
  size_t len = 0;
  char* data = sdlogLoad(name, len, err, n, SD_BACKUPS);
  if (!data) return false;
  const DeserializationError e = deserializeJson(d, (const char*)data, len);
  free(data);
  if (e) return snprintf(err, n, "not readable (%s)", e.c_str()), false;
  if (strcmp(d["kind"] | "", "tempctl-duo-backup")) return snprintf(err, n, "not a TempCtl Duo backup"), false;
  return true;
}

bool backupInfo(const char* name, BackupInfo& i, char* err, size_t n) {
  memset(&i, 0, sizeof(i));
  JsonDocument d;
  if (!loadBackup(name, d, err, n)) return false;
  strlcpy(i.time, d["time"] | "", sizeof(i.time));
  strlcpy(i.fw, d["fw"] | "", sizeof(i.fw));
  i.settings = d["settings"].is<JsonObjectConst>();
  for (JsonObjectConst o : d["programs"].as<JsonArrayConst>()) {
    const int k = o["i"] | -1;
    if (k < 0 || k >= PROG_MAX || !o["program"].is<JsonObjectConst>()) continue;
    char s[8];
    snprintf(s, sizeof(s), "%s%d", i.programs ? ", " : "", k + 1);
    if (strlen(i.slots) + strlen(s) + 4 < sizeof(i.slots)) strlcat(i.slots, s, sizeof(i.slots));
    else if (!strstr(i.slots, "...")) strlcat(i.slots, " ...", sizeof(i.slots));
    i.mask |= 1u << k;
    ++i.programs;
  }
  return true;
}

// like POST /api/config with the whole document; net: the network settings changed (used after a restart)
static bool restoreSettings(JsonObjectConst o, bool& net, char* msg, size_t n) {
  static Settings S, Old;
  {
    Guard g;
    S = Old = gSettings;
  }
  if (!settingsFromJson(o, S, msg, n)) return false;
  if (!settingsValidate(S, boardLimits(), msg, n)) return false;
  {
    JsonDocument a, b;
    settingsToJson(Old, a.to<JsonObject>(), true);
    settingsToJson(S, b.to<JsonObject>(), true);
    String x, y;
    serializeJson(a["wifi"], x);
    serializeJson(b["wifi"], y);
    net = x != y;
  }
  {
    Guard g;
    gSettings = S;
  }
  saveSettingsAndNotify();
  return true;
}

bool backupRestore(const char* name, uint8_t what, char* msg, size_t n) {
  msg[0] = 0;
  if (!(what & BACKUP_ALL)) return snprintf(msg, n, "nothing chosen to restore"), false;
  if (runIsActive()) return snprintf(msg, n, "a program is running - stop it first"), false;
  Busy b;
  if (!b.got) return snprintf(msg, n, "another backup is being made or restored - try again"), false;
  JsonDocument d;
  char err[160];
  if (!loadBackup(name, d, err, sizeof(err))) return snprintf(msg, n, "%s", err), false;
  bool net = false;
  const bool sets = what & BACKUP_SETTINGS;
  if (sets) {
    if (!d["settings"].is<JsonObjectConst>()) return snprintf(msg, n, "the backup holds no settings"), false;
    if (!restoreSettings(d["settings"].as<JsonObjectConst>(), net, err, sizeof(err)))
      return snprintf(msg, n, "Settings not restored: %s", err), false;
  }
  int done = 0;
  char bad[200] = "";
  if (what & BACKUP_PROGRAMS) {
    ScratchProg sp;
    if (!sp) return snprintf(msg, n, "%sout of memory", sets ? "Settings restored; programs: " : ""), false;
    const char* names[NUM_CHANNELS];
    float lo[NUM_CHANNELS], hi[NUM_CHANNELS];
    static char nameBuf[NUM_CHANNELS][CH_NAME_LEN];
    {
      Guard g;
      for (int c = 0; c < NUM_CHANNELS; ++c) {
        strlcpy(nameBuf[c], gSettings.ch[c].name, sizeof(nameBuf[c]));
        names[c] = nameBuf[c];
        lo[c] = gSettings.ch[c].spMin;
        hi[c] = gSettings.ch[c].spMax;
      }
    }
    for (JsonObjectConst o : d["programs"].as<JsonArrayConst>()) {
      const int k = o["i"] | -1;
      if (k < 0 || k >= PROG_MAX || !o["program"].is<JsonObjectConst>()) continue;
      char e[128];
      const bool good = progFromJson(o["program"].as<JsonObjectConst>(), *sp, e, sizeof(e), names) &&
                        progCheckLimits(*sp, lo, hi, e, sizeof(e), names) && programSave(k, *sp, e, sizeof(e));
      if (good) {
        ++done;
      } else {
        char x[160];
        snprintf(x, sizeof(x), "%sslot %d: %s", bad[0] ? "; " : "", k + 1, e);
        strlcat(bad, x, sizeof(bad));
      }
    }
  }
  char p[24];
  snprintf(p, sizeof(p), "%d program%s", done, done == 1 ? "" : "s");
  const bool progs = what & BACKUP_PROGRAMS;
  snprintf(msg, n, "%s%s%s restored.%s%s%s", sets ? "Settings" : "", sets && progs ? " and " : "", progs ? p : "",
           bad[0] ? " Not restored: " : "", bad, net ? (bad[0] ? ". Network settings: used after a restart." : " Network settings: used after a restart.") : bad[0] ? "." : "");
  const bool ok = !bad[0];
  logEvent("Restored from the SD card backup/%s: %s", name, msg);
  return ok;
}

#else  // ------------------------------------------------------------ no SD slot

bool backupCreate(char* name, size_t nn, char* err, size_t n) {
  if (nn) name[0] = 0;
  snprintf(err, n, "no SD slot on this board");
  return false;
}
bool backupInfo(const char*, BackupInfo& i, char* err, size_t n) {
  memset(&i, 0, sizeof(i));
  snprintf(err, n, "no SD slot on this board");
  return false;
}
bool backupRestore(const char*, uint8_t, char* msg, size_t n) {
  snprintf(msg, n, "no SD slot on this board");
  return false;
}
#endif
