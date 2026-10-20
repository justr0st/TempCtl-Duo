#include "ota_net.h"
#include "config.h"

#include <Arduino.h>
#include <string.h>
#include "release.h"

#if HAS_NET_OTA
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include "ca_roots.h"
#include "console.h"
#include "ota.h"
#include "version.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static NetOtaInfo info{};
static ReleaseInfo rel{};
static bool relOk = false;
static bool running = false;  // the task exists (one job at a time)
static constexpr uint32_t STALL_MS = 20000;  // no data for this long: the download is given up

static void setFail(const char* why) {
  portENTER_CRITICAL(&mux);
  info.phase = NET_FAILED;
  snprintf(info.msg, sizeof(info.msg), "%s", why);
  portEXIT_CRITICAL(&mux);
}

// GET https://host/path: false + why (the client stays connected for the body). The reason names the stage:
// name lookup, TCP / TLS (mbedTLS text and code), HTTP; the serial log adds the free internal RAM
static bool get(HTTPClient& h, WiFiClientSecure& c, const char* host, const char* path, char* err, size_t n) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    snprintf(err, n, "%s: name not found (DNS server %s)", host, WiFi.dnsIP().toString().c_str());
    LogSerial.printf("[ota] %s\n", err);
    return false;
  }
  c.setCACert(CA_ROOTS);
  c.setHandshakeTimeout(15);
  h.setConnectTimeout(10000);
  h.setTimeout(15000);
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setUserAgent("TempCtl-Duo/" FW_VERSION " (" BOARD_ID ")");
  const unsigned freeKb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
  const unsigned bigKb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
  if (!h.begin(c, host, 443, path, true)) return snprintf(err, n, "%s: no connection", host), false;
  const int code = h.GET();
  if (code == 200) return true;
  if (code < 0) {
    char t[72] = "";
    const int e = c.lastError(t, sizeof(t));  // (TLS / socket stage of the connect)
    if (e) snprintf(err, n, "%s: %s (-0x%04X)", host, t, (unsigned)-e);
    else snprintf(err, n, "%s: %s", host, HTTPClient::errorToString(code).c_str());
  } else {
    snprintf(err, n, "%s answered HTTP %d", host, code);
  }
  LogSerial.printf("[ota] GET https://%s%s (%s): %s - RAM before: %u kB free, largest block %u kB\n", host, path,
                   ip.toString().c_str(), err, freeKb, bigKb);
  h.end();
  return false;
}

static void check() {
  char err[120];
  WiFiClientSecure c;
  HTTPClient h;
  if (!get(h, c, RELEASE_HOST, RELEASE_MANIFEST, err, sizeof(err))) return setFail(err);
  const int len = h.getSize();
  if (len > 16384) {
    h.end();
    return setFail("the release list is too large");
  }
  String body = h.getString();
  h.end();
  ReleaseInfo r;
  if (!releaseParse(body.c_str(), body.length(), BOARD_ID, r, err, sizeof(err))) return setFail(err);
  const bool newer = versionCompare(r.version, FW_VERSION) > 0;
  portENTER_CRITICAL(&mux);
  rel = r;
  relOk = true;
  info.phase = NET_CHECKED;
  info.newer = newer;
  snprintf(info.latest, sizeof(info.latest), "%s", r.version);
  snprintf(info.date, sizeof(info.date), "%s", r.date);
  info.size = r.size;
  info.msg[0] = 0;
  portEXIT_CRITICAL(&mux);
  LogSerial.printf("[ota] release page: %s (%u kB)%s\n", r.version, (unsigned)(r.size / 1024), newer ? " - newer" : "");
}

static void install() {
  char err[120];
  ReleaseInfo r;
  portENTER_CRITICAL(&mux);
  r = rel;
  portEXIT_CRITICAL(&mux);
  if (!otaBegin(OTA_NET, r.size, err, sizeof(err))) return setFail(err);
  if (r.md5[0]) otaSetMd5(r.md5);
  WiFiClientSecure c;
  HTTPClient h;
  if (!get(h, c, r.host, r.path, err, sizeof(err))) {
    otaAbort(err);
    return setFail(err);
  }
  const int len = h.getSize();
  if (len > 0 && (uint32_t)len != r.size) {
    snprintf(err, sizeof(err), "the file has %d bytes, the release list says %u", len, (unsigned)r.size);
    h.end();
    otaAbort(err);
    return setFail(err);
  }
  static constexpr size_t BUF = 4096;
  uint8_t* buf = (uint8_t*)malloc(BUF);
  if (!buf) {
    h.end();
    otaAbort("no memory for the download");
    return setFail("no memory for the download");
  }
  WiFiClient* s = h.getStreamPtr();
  uint32_t got = 0, last = millis();
  bool ok = true;
  while (got < r.size) {
    const size_t av = s->available();
    if (av) {
      const size_t want = min(min(av, BUF), (size_t)(r.size - got));
      const int k = s->read(buf, want);
      if (k > 0) {
        if (!otaWrite(buf, k)) {  // (otaWrite aborted the update; its reason is in otaStatus)
          OtaStatus st;
          otaStatus(st);
          setFail(st.msg);
          ok = false;
          break;
        }
        got += k;
        last = millis();
        continue;
      }
    }
    if (!s->connected() && !s->available()) {
      snprintf(err, sizeof(err), "the connection closed after %u of %u kB", (unsigned)(got / 1024), (unsigned)(r.size / 1024));
      ok = false;
    } else if (millis() - last > STALL_MS) {
      snprintf(err, sizeof(err), "the download stalled after %u of %u kB", (unsigned)(got / 1024), (unsigned)(r.size / 1024));
      ok = false;
    }
    if (!ok) {
      otaAbort(err);
      setFail(err);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  free(buf);
  h.end();
  if (!ok) return;
  if (!otaEnd(err, sizeof(err))) return setFail(err);  // (written: otaLoop restarts the device)
  portENTER_CRITICAL(&mux);
  info.phase = NET_IDLE;
  portEXIT_CRITICAL(&mux);
}

// mbedTLS takes ~70 kB for a session (16 kB in + 16 kB out buffers, handshake, the parsed root list); the
// S3's internal RAM has less than that free while the UI runs ("SSL - Memory allocation failed", 76 kB free,
// largest block 67 kB). While a job runs, mbedTLS allocates in PSRAM first (internal RAM if that fails);
// afterwards the core's allocator again. Freeing works for blocks from either heap, so blocks that another
// mbedTLS user (Wi-Fi) got meanwhile are freed correctly later too.
// (the core's pair; esp_mem.h declares it without extern "C", so it is named here by its C symbol)
extern "C" void* coreTlsCalloc(size_t n, size_t size) asm("esp_mbedtls_mem_calloc");
extern "C" void coreTlsFree(void* p) asm("esp_mbedtls_mem_free");
static void* psramCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void psramFree(void* p) { heap_caps_free(p); }

static void worker(void* arg) {
  mbedtls_platform_set_calloc_free(psramCalloc, psramFree);
  if ((uintptr_t)arg == 2) install();
  else check();
  mbedtls_platform_set_calloc_free(coreTlsCalloc, coreTlsFree);
  portENTER_CRITICAL(&mux);
  running = false;
  portEXIT_CRITICAL(&mux);
  vTaskDelete(nullptr);
}

static bool start(uintptr_t job, char* err, size_t n) {
  if (WiFi.status() != WL_CONNECTED)
    return snprintf(err, n, "not connected to a Wi-Fi network (needs one with internet access)"), false;
  portENTER_CRITICAL(&mux);
  const bool busy = running;
  if (!busy) {
    running = true;
    info.phase = job == 2 ? NET_DOWNLOADING : NET_CHECKING;
    info.msg[0] = 0;
  }
  portEXIT_CRITICAL(&mux);
  if (busy) return snprintf(err, n, "still busy with the release page"), false;
  // (TLS handshake: mbedTLS needs a large stack; core 1 like the loop, below its priority)
  if (xTaskCreatePinnedToCore(worker, "netota", 12288, (void*)job, 1, nullptr, 1) != pdPASS) {
    portENTER_CRITICAL(&mux);
    running = false;
    portEXIT_CRITICAL(&mux);
    setFail("could not start (no memory)");
    return snprintf(err, n, "could not start (no memory)"), false;
  }
  return true;
}

bool netOtaAvailable() { return true; }

bool netOtaCheckStart(char* err, size_t n) { return start(1, err, n); }

bool netOtaInstallStart(char* err, size_t n) {
  NetOtaInfo i;
  netOtaInfo(i);
  if (!relOk || i.phase != NET_CHECKED) return snprintf(err, n, "check for a release first"), false;
  if (!i.newer) return snprintf(err, n, "%s is not newer than this firmware (%s)", i.latest, FW_VERSION), false;
  if (!otaCanStart(err, n)) return false;
  return start(2, err, n);
}

void netOtaInfo(NetOtaInfo& i) {
  portENTER_CRITICAL(&mux);
  i = info;
  portEXIT_CRITICAL(&mux);
}

#else  // CYD: not compiled in (the web page installs releases through the browser)

bool netOtaAvailable() { return false; }
bool netOtaCheckStart(char* err, size_t n) {
  return snprintf(err, n, "on this board: web page, Settings, Firmware update, Check for updates"), false;
}
bool netOtaInstallStart(char* err, size_t n) { return netOtaCheckStart(err, n); }
void netOtaInfo(NetOtaInfo& i) { i = NetOtaInfo{}; }

#endif
