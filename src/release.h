// Releases on the hosted page (spec 9.13, 9.22), pure C++ (host-tested). Every push to main builds all
// firmware images and the desktop app (.github/workflows/release.yml) and publishes them with the page on
// GitHub Pages (a project page: under RELEASE_BASE):
//   https://<RELEASE_HOST><RELEASE_BASE>/dl/manifest.json
//   {"version":"1.0.52","date":"2026-10-20T09:15:00Z","commit":"abc1234",
//    "firmware":{"s3":{"url":"/dl/tempctl-duo-1.0.52-s3.bin","size":1923456,"md5":"<32 hex>",
//                      "full":{"url":"/dl/tempctl-duo-1.0.52-s3-full.bin","size":...}}, "cyd-2xlocal":{...}},
//    "app":[{"os":"windows","arch":"x64","url":"/dl/...exe","size":...}, ...]}
// A url starting with "/" is on the release site: under RELEASE_BASE (1.0.65 and older, on the Cloudflare page
// at the root, read the same manifest there); "https://host/path" may name another host.
// The device (S3: touch, serial) and the web page (both boards) read it to offer the newer firmware.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define RELEASE_HOST "justr0st.github.io"
#define RELEASE_BASE "/TempCtl-Duo"
#define RELEASE_MANIFEST RELEASE_BASE "/dl/manifest.json"

// "1.0.52" vs "1.0.9": numbers compared one by one (missing = 0): < 0, 0, > 0
int versionCompare(const char* a, const char* b);

struct ReleaseInfo {
  char version[16];
  char date[24];   // ISO time of the build (UTC)
  char host[64];   // where the image is (RELEASE_HOST unless the url names another https host)
  char path[128];  // e.g. /TempCtl-Duo/dl/tempctl-duo-1.0.52-s3.bin
  uint32_t size;
  char md5[33];    // lower-case hex ("" = not given)
};

// the manifest's entry for this board (env name, BOARD_ID): false + reason when it has none or is broken
bool releaseParse(const char* json, size_t len, const char* board, ReleaseInfo& r, char* err, size_t n);
