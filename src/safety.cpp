#include "safety.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include "config.h"

static constexpr uint32_t TICK_MS = 50;
static constexpr int BUZZ_CH = 6;  // LEDC channel 6 (timer 3; 0 = backlight, 1..3 = status LED)
static constexpr uint32_t BUZZ_HZ = 2700;

static volatile bool estopOn = false, estopRaw = false, estopEnabled = false;
static volatile bool soundEnabled = true;
static volatile uint8_t soundLevel = SOUND_NONE, mutedLevel = SOUND_NONE;
static volatile bool finishedReq = false, testReq = false;
static volatile uint32_t lastActivity = 0;
static volatile float chipC = NAN;

// patterns: on/off times in ticks (50 ms), 0 = end of the list, then the pattern repeats
static const uint8_t PAT_ALARM[] = {8, 4, 8, 4, 8, 28, 0};          // 3 long, ~3 s
static const uint8_t PAT_ACTION[] = {3, 3, 3, 191, 0};              // 2 short, ~10 s
static const uint8_t PAT_FINISHED[] = {3, 3, 3, 3, 3, 1, 0};        // 3 short, once
static const uint8_t PAT_TEST[] = {4, 1, 0};

#if defined(PIN_BUZZER)
static void buzz(bool on) {
  static bool cur = false;
  if (on == cur) return;
  cur = on;
  ledcWrite(BUZZ_CH, on ? 128 : 0);
}
#else
static void buzz(bool) {}
#endif

static void tick(void*) {
#if defined(PIN_ESTOP)
  static uint8_t hi = 0, lo = 0;
  const bool open = digitalRead(PIN_ESTOP) == HIGH;
  if (open) { lo = 0; if (hi < 255) ++hi; }
  else { hi = 0; if (lo < 255) ++lo; }
  if (hi >= 2) estopRaw = true;
  if (lo >= 10) estopRaw = false;
  estopOn = estopEnabled && estopRaw;
#endif
  // sound
  static const uint8_t* pat = nullptr;
  static uint8_t idx = 0, left = 0;
  static bool once = false;
  const uint8_t lvl = soundLevel;
  if (mutedLevel && lvl < mutedLevel) mutedLevel = lvl;  // condition gone (or lower): mute ends
  const uint8_t eff = soundEnabled && lvl > mutedLevel ? lvl : SOUND_NONE;
  const uint8_t* want = eff == SOUND_ALARM ? PAT_ALARM : eff == SOUND_ACTION ? PAT_ACTION : nullptr;
  if (finishedReq && want) finishedReq = false;  // an alarm is louder news
  if (testReq) { testReq = false; pat = PAT_TEST; once = true; idx = 0; left = 0; }
  else if (finishedReq && !want && (!pat || !once)) {
    finishedReq = false;
    if (soundEnabled) { pat = PAT_FINISHED; once = true; idx = 0; left = 0; }
  }
  if (!once && pat != want) { pat = want; idx = 0; left = 0; }
  if (!pat) { buzz(false); return; }
  if (left == 0) {
    if (pat[idx] == 0) {
      if (once) { pat = nullptr; once = false; buzz(false); return; }
      idx = 0;
    }
    left = pat[idx++];
  }
  buzz((idx & 1) == 1);  // odd index: an "on" time is running
  --left;
}

void safetyBegin() {
  static bool started = false;
  if (started) return;
  started = true;
#if defined(PIN_ESTOP)
  pinMode(PIN_ESTOP, INPUT_PULLUP);
#endif
#if defined(PIN_BUZZER)
  ledcSetup(BUZZ_CH, BUZZ_HZ, 8);
  ledcWrite(BUZZ_CH, 0);
  ledcAttachPin(PIN_BUZZER, BUZZ_CH);
#endif
  lastActivity = millis();
  esp_timer_create_args_t a = {};
  a.callback = tick;
  a.name = "safety";
  esp_timer_handle_t h;
  if (esp_timer_create(&a, &h) == ESP_OK) esp_timer_start_periodic(h, TICK_MS * 1000);
}

void safetyConfigure(bool estop, bool sound) {
  estopEnabled = estop && safetyEstopWired();
  estopOn = estopEnabled && estopRaw;
  soundEnabled = sound;
}

#if defined(PIN_ESTOP)
bool safetyEstopWired() { return true; }
#else
bool safetyEstopWired() { return false; }
#endif
bool safetyEstop() { return estopOn; }
bool safetyEstopRaw() { return estopRaw; }

#if defined(PIN_BUZZER)
bool safetySoundWired() { return true; }
#else
bool safetySoundWired() { return false; }
#endif
void safetySound(SoundLevel level) { soundLevel = level; }
void safetySoundFinished() { finishedReq = true; }
bool safetySoundMuted() { return mutedLevel != SOUND_NONE && soundLevel <= mutedLevel; }
void safetyBeepTest() { testReq = true; }

void safetyActivity() {
  lastActivity = millis();
  if (soundLevel) mutedLevel = soundLevel;
}
uint32_t safetyLastActivityMs() { return lastActivity; }

#if CONFIG_IDF_TARGET_ESP32S3
bool safetyChipTempWired() { return true; }
void safetyChipTempPoll() {
  const float t = temperatureRead();
  chipC = (t > -40 && t < 150) ? t : NAN;
}
#else
bool safetyChipTempWired() { return false; }
void safetyChipTempPoll() {}
#endif
float safetyChipTemp() { return chipC; }
