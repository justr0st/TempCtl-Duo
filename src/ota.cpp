#include "ota.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "app_state.h"
#include "console.h"
#include "config.h"
#include "local_ctrl.h"
#include "ota_image.h"
#include "version.h"

// kept in the image (the scanner of the next update looks for it). In the "custom app description" section,
// which the linker puts right after the ESP-IDF app description at the start of the first (flash data)
// segment: file offset 24 + 8 + 256 = 288, so the SD card check finds it in the first kilobytes (1.0.43;
// older images: anywhere in the read-only data, the check then reads the whole file).
__attribute__((used, section(".rodata_custom_desc"))) const char OTA_IMAGE_ID[] =
    "TCDUO-IMAGE|" BOARD_ID "|" FW_VERSION "|end";

static constexpr const char* NOTE = "/duo/ota.json";  // {"from","to","src"} until the new firmware is confirmed
static constexpr uint32_t PROBATION_MS = 30000;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static OtaStatus st{};
static ImageScanner scan;
static bool probation = false, confirmed = false;
static uint32_t doneAt = 0;

// the core (C) calls this at boot: true = this firmware confirms itself later (otaLoop)
extern "C" bool verifyRollbackLater() { return true; }

void otaStatus(OtaStatus& s) {
  portENTER_CRITICAL(&mux);
  s = st;
  portEXIT_CRITICAL(&mux);
}

static void fail(const char* why) {
  portENTER_CRITICAL(&mux);
  st.phase = OTA_FAILED;
  snprintf(st.msg, sizeof(st.msg), "%s", why);
  portEXIT_CRITICAL(&mux);
}

bool otaSlot(uint32_t& size, char* why, size_t n) {
  size = 0;
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || next == esp_ota_get_running_partition()) {
    if (n) snprintf(why, n, "no second firmware slot: flash once over USB with the OTA partition table (README)");
    return false;
  }
  size = next->size;
  return true;
}

const char* otaRunningSlot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->label : "?";
}

bool otaProbation() { return probation && !confirmed; }

bool otaCanStart(char* err, size_t n) {
  uint32_t size;
  if (!otaSlot(size, err, n)) return false;
  if (st.phase == OTA_RUNNING) return snprintf(err, n, "an update is already running"), false;
  if (st.phase == OTA_DONE) return snprintf(err, n, "updated - restarting"), false;
  if (otaProbation()) return snprintf(err, n, "the new firmware is still being checked (30 s after the start)"), false;
  Guard g;
  if (gLive.state == RunState::Running || gLive.state == RunState::Paused)
    return snprintf(err, n, "a program is running - stop it first"), false;
  for (int i = 0; i < NUM_CHANNELS; ++i)
    if (gLive.ch[i].outOn) return snprintf(err, n, "%s: outputs on - switch them off first", gLive.ch[i].name), false;
  return true;
}

bool otaBegin(OtaSource src, uint32_t total, char* err, size_t n) {
  if (!otaCanStart(err, n)) return false;
  uint32_t size;
  otaSlot(size, nullptr, 0);
  if (total && total > size) return snprintf(err, n, "the file (%u kB) is larger than the firmware slot (%u kB)",
                                              (unsigned)(total / 1024), (unsigned)(size / 1024)), false;
  if (!Update.begin(total ? total : UPDATE_SIZE_UNKNOWN, U_FLASH))
    return snprintf(err, n, "update not started: %s", Update.errorString()), false;
  scan.reset();
  portENTER_CRITICAL(&mux);
  st = OtaStatus{};
  st.phase = OTA_RUNNING;
  st.source = src;
  st.total = total;
  portEXIT_CRITICAL(&mux);
  logEvent("Firmware update started (%s%s)", src == OTA_SD ? "SD card" : "web upload", total ? "" : ", size unknown");
  return true;
}

bool otaWrite(const uint8_t* p, size_t n) {
  if (st.phase != OTA_RUNNING) return false;
  const bool hadHead = scan.chipId() >= 0;
  scan.feed(p, n);
  if (!hadHead && scan.chipId() >= 0 && (!scan.headerOk() || scan.chipId() != BOARD_CHIP)) {
    const char* why = imageProblem(scan, BOARD_ID, BOARD_CHIP);  // (header or chip)
    otaAbort(*why ? why : "not a firmware image for this board");
    return false;
  }
  if (Update.write((uint8_t*)p, n) != n) {
    char m[96];
    snprintf(m, sizeof(m), "writing failed: %s", Update.errorString());
    otaAbort(m);
    return false;
  }
  portENTER_CRITICAL(&mux);
  st.done += n;
  if (scan.found() && !st.version[0]) snprintf(st.version, sizeof(st.version), "%s", scan.version());
  portEXIT_CRITICAL(&mux);
  return true;
}

void otaAbort(const char* why) {
  if (st.phase != OTA_RUNNING) return;
  Update.abort();
  fail(why);
  logError("Firmware update failed: %s", why);
}

bool otaEnd(char* err, size_t n) {
  if (st.phase != OTA_RUNNING) {
    snprintf(err, n, "%s", st.phase == OTA_FAILED ? st.msg : "no update running");
    return false;
  }
  const char* why = imageProblem(scan, BOARD_ID, BOARD_CHIP);
  if (*why) {
    otaAbort(why);
    snprintf(err, n, "%s", why);
    return false;
  }
  if (!Update.end(true)) {
    char m[96];
    snprintf(m, sizeof(m), "the image is not valid: %s", Update.errorString());
    fail(m);
    logError("Firmware update failed: %s", m);
    snprintf(err, n, "%s", m);
    return false;
  }
  // a note for the next start: the new firmware reports itself, the old one (after a rollback) the failure
  if (File f = LittleFS.open(NOTE, "w")) {
    f.printf("{\"from\":\"%s\",\"to\":\"%s\",\"src\":\"%s\"}", FW_VERSION, scan.version(), st.source == OTA_SD ? "sd" : "web");
    f.close();
  }
  portENTER_CRITICAL(&mux);
  st.phase = OTA_DONE;
  snprintf(st.msg, sizeof(st.msg), "firmware %s written - restarting", scan.version());
  portEXIT_CRITICAL(&mux);
  doneAt = millis();
  logEvent("Firmware %s written (%u kB) - restarting", scan.version(), (unsigned)(scan.size() / 1024));
  return true;
}

void otaBoot() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t s;
  probation = run && esp_ota_get_state_partition(run, &s) == ESP_OK && s == ESP_OTA_IMG_PENDING_VERIFY;
  LogSerial.printf("[ota] %s, slot %s%s\n", OTA_IMAGE_ID, run ? run->label : "?", probation ? ", new: on probation" : "");
}

void otaBootReport() {
  File f = LittleFS.open(NOTE, "r");
  if (!f) {
    if (probation) logEvent("Firmware %s started on probation (confirmed after 30 s)", FW_VERSION);
    return;
  }
  JsonDocument d;
  const bool ok = deserializeJson(d, f) == DeserializationError::Ok;
  f.close();
  const char* from = d["from"] | "?";
  const char* to = d["to"] | "?";
  if (ok && !strcmp(to, FW_VERSION)) {
    logEvent("Firmware updated from %s to %s - checking it for 30 s", from, to);  // (the note stays until confirmed)
    return;
  }
  LittleFS.remove(NOTE);
  if (ok && !strcmp(from, FW_VERSION))
    logError("Firmware update to %s failed: it did not start correctly, %s runs again (rollback)", to, FW_VERSION);
}

void otaLoop() {
  if (probation && !confirmed && millis() > PROBATION_MS && controlCycles() > 10) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      confirmed = true;
      LittleFS.remove(NOTE);
      logEvent("Firmware %s confirmed: it runs normally", FW_VERSION);
    }
  }
  if (st.phase == OTA_DONE && doneAt && millis() - doneAt > 2000 && !appRestartPending()) {
    doneAt = 0;
    for (int ch = 0; ch < NUM_CHANNELS; ++ch) localCtrlSetOutputs(ch, false);
    appRequestRestart("after the firmware update");
  }
}
