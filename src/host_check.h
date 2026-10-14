// Host header check of the web API (web.cpp hostOk, spec 7 "Request checks"), pure C++ (host-tested).
#pragma once
#include <ctype.h>
#include <string.h>

// true if `host` (the Host header: name or address, optional :port, any case) addresses this device:
// empty, an IPv4 / IPv6 literal, one of `names` (n of them; empty entries ignored) or such a name with a local
// network suffix (.local, .lan, .fritz.box, ...)
inline bool hostAllowed(const char* host, const char* const* names, int n) {
  if (!host || !*host || *host == '[') return true;  // (no Host: HTTP/1.0; '[': IPv6 literal)
  char h[64];
  size_t len = strlen(host);
  if (len >= sizeof(h)) return false;
  for (size_t i = 0; i <= len; ++i) h[i] = (char)tolower((unsigned char)host[i]);
  char* colon = strrchr(h, ':');
  if (colon) *colon = 0;
  len = strlen(h);
  if (len && h[len - 1] == '.') h[--len] = 0;
  if (!len) return false;
  bool ip = true;
  for (size_t i = 0; i < len && ip; ++i) ip = isdigit((unsigned char)h[i]) || h[i] == '.';
  if (ip) return true;
  static const char* const SUFFIX[] = {".local", ".lan", ".home", ".home.arpa", ".localdomain", ".internal",
                                       ".fritz.box", ".box", ".router", ".intranet", ".private"};
  for (int k = 0; k < n; ++k) {
    const char* nm = names[k];
    const size_t nl = nm ? strlen(nm) : 0;
    if (!nl || nl > len || strncasecmp(h, nm, nl)) continue;
    if (len == nl) return true;
    for (const char* sfx : SUFFIX)
      if (!strcmp(h + nl, sfx)) return true;
  }
  return false;
}
