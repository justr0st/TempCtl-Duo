#include "release.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int versionCompare(const char* a, const char* b) {
  a = a ? a : "";
  b = b ? b : "";
  while (*a || *b) {
    char *ea, *eb;
    const long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
    if (x != y) return x < y ? -1 : 1;
    a = *ea == '.' ? ea + 1 : ea;
    b = *eb == '.' ? eb + 1 : eb;
    if (a == ea && *a) ++a;  // (not a number: skip the character)
    if (b == eb && *b) ++b;
  }
  return 0;
}

static bool isHex32(const char* s) {
  if (strlen(s) != 32) return false;
  for (const char* p = s; *p; ++p)
    if (!isxdigit((unsigned char)*p)) return false;
  return true;
}

bool releaseParse(const char* json, size_t len, const char* board, ReleaseInfo& r, char* err, size_t n) {
  r = ReleaseInfo{};
  JsonDocument filter;
  filter["version"] = true;
  filter["date"] = true;
  filter["firmware"][board]["url"] = true;
  filter["firmware"][board]["size"] = true;
  filter["firmware"][board]["md5"] = true;
  JsonDocument d;
  if (deserializeJson(d, json, len, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
    return snprintf(err, n, "the release list is not valid JSON"), false;
  const char* v = d["version"] | "";
  if (!*v || strlen(v) >= sizeof(r.version)) return snprintf(err, n, "the release list has no version"), false;
  snprintf(r.version, sizeof(r.version), "%s", v);
  snprintf(r.date, sizeof(r.date), "%s", d["date"] | "");
  JsonObject f = d["firmware"][board];
  if (f.isNull()) return snprintf(err, n, "release %s has no firmware for this board (%s)", r.version, board), false;
  const char* url = f["url"] | "";
  r.size = f["size"] | 0u;
  const char* md5 = f["md5"] | "";
  if (*md5 && !isHex32(md5)) return snprintf(err, n, "release %s: bad checksum", r.version), false;
  for (int i = 0; md5[i] && i < 32; ++i) r.md5[i] = (char)tolower((unsigned char)md5[i]);
  if (url[0] == '/') {  // (on the release site, under its base path)
    snprintf(r.host, sizeof(r.host), "%s", RELEASE_HOST);
    if (strlen(RELEASE_BASE) + strlen(url) >= sizeof(r.path))
      return snprintf(err, n, "release %s: file name too long", r.version), false;
    snprintf(r.path, sizeof(r.path), "%s%s", RELEASE_BASE, url);
  } else if (!strncmp(url, "https://", 8)) {
    const char* h = url + 8;
    const char* s = strchr(h, '/');
    if (!s || s == h || (size_t)(s - h) >= sizeof(r.host) || strlen(s) >= sizeof(r.path))
      return snprintf(err, n, "release %s: bad file address", r.version), false;
    memcpy(r.host, h, s - h);
    r.host[s - h] = 0;
    snprintf(r.path, sizeof(r.path), "%s", s);
  } else {
    return snprintf(err, n, "release %s: bad file address (https only)", r.version), false;
  }
  if (r.size < 65536) return snprintf(err, n, "release %s: the firmware size is missing", r.version), false;
  return true;
}
