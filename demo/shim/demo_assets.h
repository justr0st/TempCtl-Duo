// Demo: the page comes from the web site, not from the firmware
#pragma once
#include <stddef.h>
#include <stdint.h>
struct WebAsset {
  const char* path;
  const char* type;
  const uint8_t* data;
  size_t len;
  bool gzip;
  bool immutable;
};
static const WebAsset WEB_ASSETS[1] = {{"/", "text/html", (const uint8_t*)"", 0, false, false}};
static constexpr size_t WEB_ASSET_COUNT = 0;
