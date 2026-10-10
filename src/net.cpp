#include "net.h"
#include "console.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "app_state.h"
#include "config.h"
#include "version.h"

static bool apStarted = false, mdnsStarted = false, haveSta = false;
static uint32_t t0 = 0;
// Station (re)connect: WiFi.begin() right after WiFi.disconnect() is cancelled by the late
// "disconnected (assoc leave)" event, and the core does not retry after that reason, so the
// new network was only joined after a restart. netApplyWifi() therefore disconnects and
// netLoop() calls begin() a moment later; a station that stays disconnected is retried.
static uint32_t beginAt = 0;          // millis() for the pending WiFi.begin(), 0 = none
static uint32_t lastBegin = 0;
static constexpr uint32_t STA_RETRY_MS = 30000;
static bool scanBusyNow();

// Fixed address or DHCP for the station; call right before WiFi.begin()
static void ipConfig(const Settings& s) {
  if (!s.staticIp) {
    WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));  // 0.0.0.0 = DHCP
    return;
  }
  const auto A = [](const uint8_t* v) { return IPAddress(v[0], v[1], v[2], v[3]); };
  const bool d1 = s.dns1[0] | s.dns1[1] | s.dns1[2] | s.dns1[3];
  const bool d2 = s.dns2[0] | s.dns2[1] | s.dns2[2] | s.dns2[3];
  if (!WiFi.config(A(s.ip), A(s.gateway), A(s.subnet), d1 ? A(s.dns1) : A(s.gateway), d2 ? A(s.dns2) : IPAddress((uint32_t)0)))
    LogSerial.println("[wifi] fixed IP address not accepted - check the settings");
}

static void staBegin() {
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  beginAt = 0;
  if (!haveSta || !s.ssid[0]) return;
  WiFi.mode(apStarted ? WIFI_AP_STA : WIFI_STA);
  ipConfig(s);
  WiFi.begin(s.ssid, s.pass);
  lastBegin = millis();
}
static String apName;

static void startAp(const Settings& s) {
  if (apStarted) {  // just update the password
    WiFi.softAP(apName.c_str(), strlen(s.apPass) >= 8 ? s.apPass : nullptr);
    return;
  }
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), AP_NAME_PREFIX "-%02X%02X", mac[4], mac[5]);
  apName = name;
  WiFi.mode(haveSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(name, strlen(s.apPass) >= 8 ? s.apPass : nullptr);
  apStarted = true;
}

// Set by the Wi-Fi event task when the station got an IP address after the
// last netApplyWifi() (so the UI can close its "Connecting..." message).
static volatile bool gotIpSinceApply = false;
// station disconnects (count + last reason) for the serial console's connect result
static volatile uint32_t discCount = 0;
static volatile uint8_t discReason = 0;

void netBegin() {
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  WiFi.onEvent([](arduino_event_id_t) { gotIpSinceApply = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  // connection state in the serial log (helps with link problems)
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t info) {
    switch (e) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LogSerial.printf("[wifi] connected to %s, IP %s, RSSI %d dBm\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        discReason = info.wifi_sta_disconnected.reason;
        ++discCount;
        LogSerial.printf("[wifi] disconnected (reason %u)\n", (unsigned)info.wifi_sta_disconnected.reason);
        break;
      case ARDUINO_EVENT_WIFI_AP_START: {  // mode(AP) + softAP() start it twice: one line is enough
        static uint32_t at = 0;
        if (at && millis() - at < 3000) break;
        at = millis();
        if (!at) at = 1;
        LogSerial.printf("[wifi] access point %s started, %s\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
        break;
      }
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        LogSerial.println("[wifi] a device joined the access point");
        break;
      default:
        break;
    }
  });
  WiFi.persistent(false);
  WiFi.setSleep(false);  // no modem sleep: steadier transfers (the device is mains powered)
  WiFi.setHostname(s.hostname);
  haveSta = s.ssid[0] != 0;
  if (haveSta) {
    WiFi.mode(WIFI_STA);
    ipConfig(s);
    WiFi.begin(s.ssid, s.pass);
    lastBegin = millis();
  } else {
    startAp(s);
  }
  t0 = millis();
}

void netLoop() {
  if (beginAt && (int32_t)(millis() - beginAt) >= 0 && !scanBusyNow()) staBegin();
  if (haveSta && !beginAt && !scanBusyNow() && WiFi.status() != WL_CONNECTED && millis() - lastBegin > STA_RETRY_MS) {
    LogSerial.printf("[wifi] not connected (status %d) - trying again\n", (int)WiFi.status());
    WiFi.disconnect();
    beginAt = millis() + 1000;
    lastBegin = millis();
  }
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
      // found by the local app's device search (tools/launcher): _tempctl._tcp with what it is
      MDNS.addService("tempctl", "tcp", 80);
      MDNS.addServiceTxt("tempctl", "tcp", "fw", FW_VERSION);
      MDNS.addServiceTxt("tempctl", "tcp", "board", BOARD_NAME);
      MDNS.addServiceTxt("tempctl", "tcp", "api", String(API_VERSION));
      mdnsStarted = true;
    }
  }
}

bool netApActive() { return apStarted && WiFi.status() != WL_CONNECTED; }

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
  gotIpSinceApply = false;
  WiFi.disconnect();
  if (haveSta) {
    beginAt = millis() + 1000;  // begin() in netLoop(), after the disconnect went through
    if (!beginAt) beginAt = 1;
    t0 = millis();  // AP fallback again after 20 s without connection
  } else if (!apStarted) {
    startAp(s);
  }
}

// --- network scan
// ESP-IDF refuses to start a scan while the station is busy trying to connect,
// and the Arduino core retries a failing connection forever (wrong password,
// network out of range). So pause those attempts for the scan and resume them
// afterwards. Starting is retried a few times because the first attempt right
// after the disconnect can still fail.
static bool scanActive = false;
// details of the last scan, in the order of netScanResult()'s names
static constexpr int SCAN_DETAIL_N = 32;
static struct {
  int8_t rssi;
  uint8_t auth, ch;
} scanDetail[SCAN_DETAIL_N];
static int scanDetailN = 0;
static bool scanResume = false;  // reconnect the station when the scan ends
static uint8_t scanTries = 0;
static uint32_t scanRetryAt = 0;
static constexpr uint8_t SCAN_MAX_TRIES = 6;
static constexpr uint32_t SCAN_RETRY_MS = 500;

static bool scanKick() {
  const int16_t r = WiFi.scanNetworks(true, false, false, 300);
  LogSerial.printf("[wifi] scan start #%u -> %d (sta status %d)\n", scanTries, r, (int)WiFi.status());
  if (r == WIFI_SCAN_RUNNING) return true;
  scanRetryAt = millis() + SCAN_RETRY_MS;
  return false;
}

static void scanFinish() {
  scanActive = false;
  if (!scanResume) return;
  scanResume = false;
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  WiFi.setAutoReconnect(true);
  if (haveSta && s.ssid[0]) {
    ipConfig(s);
    WiFi.begin(s.ssid, s.pass);
    lastBegin = t0 = millis();
  }
}

void netScanStart() {
  if (scanActive) return;
  WiFi.scanDelete();
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP_STA);  // scanning needs the station
  scanResume = haveSta && WiFi.status() != WL_CONNECTED;
  if (scanResume) {  // stop the endless connection attempts during the scan
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
  }
  scanActive = true;
  scanTries = 1;
  scanKick();
}

bool netScanBusy() { return scanActive; }
static bool scanBusyNow() { return scanActive; }

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
  if (!scanActive) return -2;
  const int16_t r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) return -1;
  if (r < 0) {  // failed to start or timed out
    if (scanTries < SCAN_MAX_TRIES) {
      if ((int32_t)(millis() - scanRetryAt) >= 0) {
        ++scanTries;
        scanKick();
      }
      return -1;
    }
    LogSerial.println("[wifi] scan failed");
    scanFinish();
    return -2;
  }
  if (n) out[0] = 0;
  int count = 0;
  const int total = r > 64 ? 64 : r;
  bool used[64] = {false};
  for (int k = 0; k < total && n; ++k) {  // strongest first
    int best = -1;
    for (int i = 0; i < total; ++i)
      if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    used[best] = true;
    const String ssid = WiFi.SSID(best);
    if (!ssid.length() || hasLine(out, ssid.c_str())) continue;  // hidden / duplicate
    if (strlen(out) + ssid.length() + 2 >= n) break;
    if (count) strcat(out, "\n");
    strcat(out, ssid.c_str());
    if (count < SCAN_DETAIL_N) scanDetail[count] = {(int8_t)WiFi.RSSI(best), (uint8_t)WiFi.encryptionType(best), (uint8_t)WiFi.channel(best)};
    ++count;
  }
  scanDetailN = count < SCAN_DETAIL_N ? count : SCAN_DETAIL_N;
  LogSerial.printf("[wifi] scan done: %d networks (%d unique)\n", r, count);
  WiFi.scanDelete();
  scanFinish();
  return count;
}

bool netScanDetail(int i, int& rssi, uint8_t& auth, int& channel) {
  if (i < 0 || i >= scanDetailN) return false;
  rssi = scanDetail[i].rssi;
  auth = scanDetail[i].auth;
  channel = scanDetail[i].ch;
  return true;
}

uint32_t netDisconnects(uint8_t* lastReason) {
  if (lastReason) *lastReason = discReason;
  return discCount;
}

void netStartAp() {
  Settings s;
  {
    Guard g;
    s = gSettings;
  }
  startAp(s);
}

String netApName() { return apStarted ? apName : String(""); }

bool netConnectedSinceApply() { return gotIpSinceApply && WiFi.status() == WL_CONNECTED; }

String netStaIp() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(""); }
