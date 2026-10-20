// Firmware from the release page (spec 9.13; S3 only, HAS_NET_OTA): the device itself reads
// https://RELEASE_HOST/dl/manifest.json (release.h), compares the version and, when asked, downloads its
// board's image straight into the other firmware slot (ota.h, source OTA_NET; MD5 checked at the end).
// Both run in a task of their own, so the touch screen, web page and control loop keep running.
// Touch: Setup › System › Online update; serial: fw net / fw net install yes. (The web page, on every
// board, does the same through the browser and the normal upload.)
#pragma once
#include <stddef.h>
#include <stdint.h>

enum NetOtaPhase : uint8_t { NET_IDLE = 0, NET_CHECKING, NET_CHECKED, NET_FAILED, NET_DOWNLOADING };
struct NetOtaInfo {
  uint8_t phase;
  bool newer;          // NET_CHECKED: the release is newer than this firmware
  char latest[16];     // its version
  char date[24];       // its build time (ISO, UTC)
  uint32_t size;       // its image size (bytes)
  char msg[120];       // NET_FAILED: why
};

bool netOtaAvailable();                        // compiled in (S3)
bool netOtaCheckStart(char* err, size_t n);    // false + why: no Wi-Fi with internet, busy
bool netOtaInstallStart(char* err, size_t n);  // after a check that found a newer release
void netOtaInfo(NetOtaInfo& i);
