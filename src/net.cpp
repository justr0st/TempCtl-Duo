#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "app_state.h"

static bool apStarted = false, mdnsStarted = false, haveSta = false;
static uint32_t t0 = 0;
static String apName;

static void startAp(const Settings& s) {
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
