// Browser demo: the parts of the firmware it does without - Wi-Fi, display, SD card, firmware update,
// status LED, serial console (the page's requests come straight to webApiRequest, sim.cpp).
#include <Arduino.h>
#include "backup.h"
#include "console.h"
#include "net.h"
#include "ota.h"
#include "sdlog.h"
#include "status_led.h"
#include "syslog.h"
#include "ui.h"
#include "app_state.h"

// the log port: system log (web System page) and the browser's console
LogPort LogSerial;
size_t LogPort::write(const uint8_t* b, size_t n) {
  syslogTee(b, n);
  return Serial.write(b, n);
}

static const char NO[] = "Not in the demo";

// Wi-Fi: a simulated network
String netInfo() { return String(gSettings.wifiOn ? "Simulated device, no Wi-Fi" : "Simulated device, Wi-Fi off"); }
bool netEnabled() { return gSettings.wifiOn != 0; }
bool netApActive() { return false; }
void netApplyWifi() {}
static uint32_t scanAt = 0;
void netScanStart() { scanAt = millis() | 1; }
bool netScanBusy() { return scanAt && millis() - scanAt < 1500; }
int netScanResult(char* out, size_t n) {
  if (!scanAt) return -2;
  if (netScanBusy()) return -1;
  scanAt = 0;
  strlcpy(out, "Workshop\nWorkshop-5G\nNeighbour", n);
  return 3;
}

// backups need the SD card (S3)
bool backupCreate(char*, size_t, char* err, size_t n) { strlcpy(err, NO, n); return false; }
bool backupInfo(const char*, BackupInfo&, char* err, size_t n) { strlcpy(err, NO, n); return false; }
bool backupRestore(const char*, uint8_t, char* msg, size_t n) { strlcpy(msg, NO, n); return false; }

// SD card: none
bool sdlogSlot() { return false; }
void sdlogRunStart(const char*, bool, bool) {}
void sdlogRow(const SdRow&) {}
void sdlogRunEnd() {}
void sdlogEvent(uint8_t, const char*) {}
void sdlogState(SdState& s) {
  memset(&s, 0, sizeof(s));
  s.card = -1;
}
int sdlogList(SdFile*, int, uint32_t& t, uint32_t& f, char* err, size_t n, SdDir) {
  t = f = 0;
  strlcpy(err, "No SD card slot", n);
  return -1;
}
char* sdlogLoad(const char*, size_t& len, char* err, size_t n, SdDir) {
  len = 0;
  strlcpy(err, "No SD card slot", n);
  return nullptr;
}
bool sdlogDelete(const char*, char* err, size_t n, SdDir) { strlcpy(err, "No SD card slot", n); return false; }
bool sdlogFirmwareCheckStart(char* err, size_t n) { strlcpy(err, "No SD card slot", n); return false; }
int sdlogFirmwareCheckResult(SdFirmware& f, char* err, size_t n) {
  memset(&f, 0, sizeof(f));
  strlcpy(err, "No SD card slot", n);
  return -1;
}
bool sdlogFirmwareInstall(char* err, size_t n) { strlcpy(err, "No SD card slot", n); return false; }

// firmware update: not in the demo
void otaStatus(OtaStatus& s) { memset(&s, 0, sizeof(s)); }
bool otaSlot(uint32_t& size, char* why, size_t n) {
  size = 0;
  strlcpy(why, "The demo runs in the browser: there is no firmware to update.", n);
  return false;
}
const char* otaRunningSlot() { return "demo"; }
bool otaProbation() { return false; }
bool otaCanStart(char* err, size_t n) { strlcpy(err, "The demo runs in the browser: there is no firmware to update.", n); return false; }
bool otaBegin(OtaSource, uint32_t, char* err, size_t n) { return otaCanStart(err, n); }
bool otaSetMd5(const char*) { return false; }
bool otaWrite(const uint8_t*, size_t) { return false; }
bool otaEnd(char* err, size_t n) { return otaCanStart(err, n); }
void otaAbort(const char*) {}

// display: none (the touch screen is not simulated)
void uiOtaRefresh() {}
void uiScreenRestart(const char*) {}

// status LED: none
void statusLedBegin() {}
LedStatus statusLedPick(bool fault, bool wait, bool ap, bool running) {
  return fault ? LedStatus::Fault : wait ? LedStatus::Wait : ap ? LedStatus::Ap : running ? LedStatus::Running : LedStatus::Idle;
}
void statusLedSet(LedStatus) {}
