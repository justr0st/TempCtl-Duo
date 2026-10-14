// Backup of the settings and all programs on the SD card (S3, spec 9.12): one JSON file per backup in
// /backup on the card, named by the local date and time (2026-10-14_153012.json). Made by hand (web
// Tools, touch System, serial "backup sd"); restored as a whole or in part. The list and deleting:
// sdlogList / sdlogDelete with SD_BACKUPS. Boards without an SD slot: everything fails ("no SD slot").
//
// File: {"fmt":1,"kind":"tempctl-duo-backup","fw":"1.0.42","board":"s3","host":"tempctl-duo",
//        "time":"2026-10-14 15:30:12","settings":{as GET /api/config, without limits and password flags},
//        "programs":[{"i":0,"program":{...}}, ...]}   (programs: the used slots; the web's programs file
//        has the same "programs" list, its settings file the same object as "settings")
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sdlog.h"

enum : uint8_t { BACKUP_SETTINGS = 1, BACKUP_PROGRAMS = 2, BACKUP_ALL = 3 };

// writes a new backup; name: its file name
bool backupCreate(char* name, size_t nn, char* err, size_t n);

struct BackupInfo {
  char time[24], fw[16];
  bool settings;
  int programs;
  char slots[64];  // "1, 2, 5"
  uint32_t mask;   // bit k: slot k (0-based)
};
bool backupInfo(const char* name, BackupInfo& i, char* err, size_t n);

// what: BACKUP_SETTINGS | BACKUP_PROGRAMS. Not while a program runs. Settings first (the programs are
// checked against the restored setpoint limits). Programs: the slots the backup holds are replaced, the
// others stay. msg: what was done or why not; false if a part failed (msg says which)
bool backupRestore(const char* name, uint8_t what, char* msg, size_t n);

// "2026-10-14 15:30:12" from "2026-10-14_153012.json" ("_2": " (2)"), "start 12, backup 1 (no clock)" from
// "start12_1.json"; other names without ".json"
inline void backupLabel(const char* name, char* out, size_t n) {
  size_t len = strlen(name);
  if (len > 5 && !strcmp(name + len - 5, ".json")) len -= 5;
  auto dig = [&](int a, int b) {
    for (int i = a; i < b; ++i)
      if (name[i] < '0' || name[i] > '9') return false;
    return true;
  };
  // yyyy-mm-dd_hhmmss[_k]
  if (len >= 17 && dig(0, 4) && name[4] == '-' && dig(5, 7) && name[7] == '-' && dig(8, 10) && name[10] == '_' &&
      dig(11, 17) && (len == 17 || (name[17] == '_' && len > 18 && len < 21 && dig(18, (int)len)))) {
    snprintf(out, n, "%.10s %.2s:%.2s:%.2s", name, name + 11, name + 13, name + 15);
    if (len > 17) {
      const size_t k = strlen(out);
      snprintf(out + k, n > k ? n - k : 0, " (%.*s)", (int)(len - 18), name + 18);
    }
    return;
  }
  unsigned b, k;  // start<b>_<k> (made before the clock was set)
  char rest;
  if (sscanf(name, "start%u_%u%c", &b, &k, &rest) == 3 && rest == '.' && len < 30) {
    snprintf(out, n, "start %u, backup %u (no clock)", b, k);
    return;
  }
  snprintf(out, n, "%.*s", (int)len, name);
}
