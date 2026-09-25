#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "app_state.h"

static bool apStarted = false, mdnsStarted = false, haveSta = false;
static uint32_t t0 = 0;
static String apName;

static void startAp(const Settings& s) {
  if (apStarted) {  // just update the password
    WiFi.softAP(apName.c_str(), strlen(s.apPass) >= 8 ? s.apPass : nullptr);
    return;
  }
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "TempCtl-%02X%02X", mac[4], mac[5]);
  apName = name;
  WiFi.mode(haveSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(name, strlen(s.apPass) >= 8 ? s.apPass : nullptr);
  apStarted = true;
}

void netBegin() {
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  WiFi.persistent(false);
  WiFi.setHostname(s.hostname);
  haveSta = s.ssid[0] != 0;
  if (haveSta) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.ssid, s.pass);
  } else {
    startAp(s);
  }
  t0 = millis();
}

void netLoop() {
  if (haveSta && !apStarted && WiFi.status() != WL_CONNECTED && millis() - t0 > 20000) {
    Settings s;
    {
      Guard g;
      s = gSettings;
    }
    startAp(s);  // keep trying STA in the background, but offer the AP
  }
  if (!mdnsStarted && (WiFi.status() == WL_CONNECTED || apStarted)) {
    char host[24];
    {
      Guard g;
      strlcpy(host, gSettings.hostname, sizeof(host));
    }
    if (MDNS.begin(host)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
    }
  }
}

String netInfo() {
  String out;
  if (WiFi.status() == WL_CONNECTED)
    out = "Wi-Fi " + WiFi.SSID() + "  " + WiFi.localIP().toString();
  else if (haveSta)
    out = "Wi-Fi connecting...";
  if (apStarted) {
    if (out.length()) out += "\n";
    out += "AP " + apName + "  " + WiFi.softAPIP().toString();
  }
  return out;
}

void netApplyWifi() {
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  if (apStarted) startAp(s);  // new AP password, if any
  haveSta = s.ssid[0] != 0;
  WiFi.disconnect();
  if (haveSta) {
    WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(s.ssid, s.pass);
    t0 = millis();  // AP fallback again after 20 s without connection
  } else if (!apStarted) {
    startAp(s);
  }
}

void netScanStart() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);  // scanning needs the station interface
  WiFi.scanNetworks(true);
}

static bool hasLine(const char* list, const char* name) {
  const size_t len = strlen(name);
  for (const char* p = list; *p;) {
    const char* e = strchr(p, '\n');
    const size_t l = e ? (size_t)(e - p) : strlen(p);
    if (l == len && strncmp(p, name, len) == 0) return true;
    if (!e) break;
    p = e + 1;
  }
  return false;
}

int netScanResult(char* out, size_t n) {
  const int16_t r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) return -1;
  if (r < 0 || n == 0) return -2;
  out[0] = 0;
  int count = 0;
  const int total = r > 64 ? 64 : r;
  bool used[64] = {false};
  for (int k = 0; k < total; ++k) {  // strongest first
    int best = -1;
    for (int i = 0; i < total; ++i)
      if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    used[best] = true;
    const String ssid = WiFi.SSID(best);
    if (!ssid.length() || hasLine(out, ssid.c_str())) continue;  // hidden / duplicate
    if (strlen(out) + ssid.length() + 2 >= n) break;
    if (count) strcat(out, "\n");
    strcat(out, ssid.c_str());
    ++count;
  }
  return count;
}
