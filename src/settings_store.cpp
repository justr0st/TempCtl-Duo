#include "settings_store.h"
#include "console.h"
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "program_store.h"

static const char* const PATH = "/duo/settings.json";
static const char* const TMP = "/duo/settings.tmp";
static const char* const BAD = "/duo/settings.bad";

BoardLimits boardLimits() { return BoardLimits{HAS_RS485 != 0, (uint8_t)LOCAL_CHANNELS_MAX}; }

bool settingsLoad(Settings& s) {
  const BoardLimits lim = boardLimits();
  settingsDefaults(s, lim);
  if (storageBegin()) fsRecoverTmp(PATH, TMP);
  if (!storageBegin() || !fsExists(PATH)) {
    LogSerial.println("[settings] no /duo/settings.json - defaults");
    return false;
  }
  File f = LittleFS.open(PATH, "r");
  JsonDocument d;
  const DeserializationError e = f ? deserializeJson(d, f) : DeserializationError::InvalidInput;
  if (f) f.close();
  char msg[160];
  if (e || !settingsFromJson(d.as<JsonObjectConst>(), s, msg, sizeof(msg))) {
    LogSerial.printf("[settings] %s unreadable (%s) - kept as %s, defaults loaded\n", PATH,
                  e ? e.c_str() : msg, BAD);
    LittleFS.remove(BAD);
    LittleFS.rename(PATH, BAD);
    settingsDefaults(s, lim);
    return false;
  }
  if (!settingsValidate(s, lim, msg, sizeof(msg))) LogSerial.printf("[settings] %s\n", msg);
  return true;
}

static volatile uint32_t gen = 1;
uint32_t settingsGen() { return gen; }

bool settingsSave(const Settings& s) {
  ++gen;
  if (!storageBegin()) return false;
  JsonDocument d;
  settingsToJson(s, d.to<JsonObject>(), false);
  File f = LittleFS.open(TMP, "w");
  if (!f) return false;
  const size_t n = serializeJsonPretty(d, f);
  f.close();
  if (n == 0) {
    LittleFS.remove(TMP);
    return false;
  }
  if (LittleFS.rename(TMP, PATH)) return true;  // replaces the old file
  LittleFS.remove(PATH);
  return LittleFS.rename(TMP, PATH);
}

uint32_t serialConfigFor(const Settings& s) {
  if (s.parity == 1) return s.stopBits == 2 ? SERIAL_8E2 : SERIAL_8E1;
  if (s.parity == 2) return s.stopBits == 2 ? SERIAL_8O2 : SERIAL_8O1;
  return s.stopBits == 2 ? SERIAL_8N2 : SERIAL_8N1;
}
