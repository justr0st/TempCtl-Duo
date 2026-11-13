#include "safety.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <math.h>
#include "config.h"
#include "io_pins.h"
#include "settings.h"

static constexpr uint32_t TICK_MS = 10;       // the sound engine's tick
static constexpr uint8_t ESTOP_EVERY = 5;     // E-stop sampled every 5th tick (50 ms, as before 1.0.68)
static constexpr uint8_t BUZZ_CH = IO_LEDC_BUZZER;  // LEDC channel 6 (timer 3, its own: 7 stays unused)
static constexpr uint32_t BUZZ_HZ = 2700;
// volume 1 ... 5 = PWM duty of the 8-bit channel (128 = a square wave, the loudest); about 3x per step
static const uint8_t DUTY[6] = {0, 2, 6, 16, 48, 128};

static volatile bool estopOn = false, estopRaw = false, estopEnabled = false;
static volatile bool soundEnabled = true, keyBeepOn = true;
static volatile uint8_t volume = 3;
static volatile uint8_t soundLevel = SOUND_NONE, mutedLevel = SOUND_NONE;
static volatile uint32_t lastActivity = 0;
static volatile float chipC = NAN;

// ------------------------------------------------------------------ sounds
// A sound is a list of notes (Hz, ms; 0 Hz = silence), played once or repeated. Lengths are rounded up to
// the 10 ms tick.
struct Note {
  uint16_t hz, ms;
};
struct Sound {
  const Note* n;
  uint8_t len;
  bool repeat;
  bool loud;     // always at volume 5 (the alarm)
  uint8_t prio;  // one-shots: a higher or equal one replaces the one that plays
};
#define SOUND(notes, rep, loud, prio) {notes, (uint8_t)(sizeof(notes) / sizeof(notes[0])), rep, loud, prio}

// fault / E-stop: 3 long beeps, again after 3 s
static const Note N_ALARM[] = {{2700, 400}, {0, 200}, {2700, 400}, {0, 200}, {2700, 400}, {0, 1400}};
// action required (pause with a message, manual mode about to end): 2 short beeps, again after 10 s
static const Note N_ACTION[] = {{2000, 150}, {0, 150}, {2000, 150}, {0, 9550}};
// run finished: C, E, G rising (7th octave: where small piezos are loud)
static const Note N_FINISHED[] = {{2093, 120}, {0, 40}, {2637, 120}, {0, 40}, {3136, 240}};
static const Note N_KEY[] = {{3000, 20}};
static const Note N_OK[] = {{2349, 60}, {0, 30}, {3136, 90}};  // D, G: a fourth up
static const Note N_REFUSED[] = {{440, 160}};
static const Note N_TEST[] = {{2700, 200}};  // the alarm's tone, at the set volume

static const Sound S_ALARM = SOUND(N_ALARM, true, true, 0);
static const Sound S_ACTION = SOUND(N_ACTION, true, false, 0);
// one-shots, by SoundFx; FX_FINISHED (internal) after them
static constexpr int FX_FINISHED = FX_TEST + 1, FX_N = FX_FINISHED + 1;
static const Sound S_FX[FX_N] = {
    SOUND(N_KEY, false, false, 1), SOUND(N_OK, false, false, 2), SOUND(N_REFUSED, false, false, 2),
    SOUND(N_TEST, false, false, 4), SOUND(N_FINISHED, false, false, 3),
};
// requests from any task; the tick takes them (a request that comes in while the tick clears it is lost: a
// click, not a safety matter)
static volatile bool fxReq[FX_N];

// the buzzer's tone: frequency and duty only written when they change
static void buzzOut(uint16_t hz, uint8_t duty) {
  static uint16_t curHz = BUZZ_HZ;
  static uint8_t curDuty = 0;
  if (!hz) duty = 0;
  if (hz && hz != curHz) {
    ledcChangeFrequency(BUZZ_CH, hz, 8);
    curHz = hz;
  }
  if (duty != curDuty) {
    ledcWrite(BUZZ_CH, duty);
    curDuty = duty;
  }
}

static const Sound* play = nullptr;  // what plays now (timer only)
static uint8_t noteIdx = 0;
static uint16_t noteLeft = 0;        // ticks of the current note still to go

static void startSound(const Sound* s) {
  play = s;
  noteIdx = 0;
  noteLeft = 0;
  if (!s) buzzOut(0, 0);
}

static void soundTick() {
  const uint8_t lvl = soundLevel;
  if (mutedLevel && lvl < mutedLevel) mutedLevel = lvl;  // condition gone (or lower): mute ends
  const uint8_t eff = soundEnabled && lvl > mutedLevel ? lvl : SOUND_NONE;
  const Sound* want = eff == SOUND_ALARM ? &S_ALARM : eff == SOUND_ACTION ? &S_ACTION : nullptr;
  // the most important one-shot asked for since the last tick
  const Sound* fx = nullptr;
  for (int i = 0; i < FX_N; ++i)
    if (fxReq[i]) {
      fxReq[i] = false;
      if (!fx || S_FX[i].prio > fx->prio) fx = &S_FX[i];
    }
  if (want) {  // an alarm / action level replaces anything lower; one-shots are dropped meanwhile
    if (play != want) startSound(want);
  } else {
    if (play && play->repeat) startSound(nullptr);  // the level ended (or was muted)
    if (fx && (!play || fx->prio >= play->prio)) startSound(fx);
  }
  if (!play) return;
  if (noteLeft == 0) {
    if (noteIdx >= play->len) {
      if (!play->repeat) {
        startSound(nullptr);
        return;
      }
      noteIdx = 0;
    }
    const Note& n = play->n[noteIdx++];
    noteLeft = (uint16_t)((n.ms + TICK_MS - 1) / TICK_MS);
    if (!noteLeft) noteLeft = 1;
    const uint8_t v = volume >= 1 && volume <= 5 ? volume : 3;
    buzzOut(n.hz, DUTY[play->loud ? 5 : v]);
  }
  --noteLeft;
}

// ------------------------------------------------------------------ E-stop

static void estopSample() {
  static uint8_t hi = 0, lo = 0;
  static int lastPin = -1;
  const int pin = ioPin(IO_ESTOP);
  if (pin != lastPin) {  // function moved / added / removed: count again on the new pin (the state stays)
    lastPin = pin;
    hi = lo = 0;
    if (pin < 0) estopRaw = false;  // (no contact any more)
  }
  if (pin >= 0) {
    const bool open = digitalRead(pin) == HIGH;
    if (open) { lo = 0; if (hi < 255) ++hi; }
    else { hi = 0; if (lo < 255) ++lo; }
    if (hi >= 2) estopRaw = true;
    if (lo >= 10) estopRaw = false;
  }
  estopOn = estopEnabled && estopRaw && pin >= 0;
}

static void tick(void*) {
  static uint8_t n = 0;
  if (++n >= ESTOP_EVERY) {
    n = 0;
    estopSample();
  }
  soundTick();
}

void safetyBegin() {
  static bool started = false;
  if (started) return;
  started = true;
  // the buzzer's channel; its pin is attached by ioConfigure (S3: an optional pin) or here (fixed pin)
  ledcSetup(BUZZ_CH, BUZZ_HZ, 8);
  ledcWrite(BUZZ_CH, 0);
#if !defined(IO_PINS_N) && defined(PIN_BUZZER)
  ledcAttachPin(PIN_BUZZER, BUZZ_CH);
#endif
  lastActivity = millis();
  esp_timer_create_args_t a = {};
  a.callback = tick;
  a.name = "safety";
  esp_timer_handle_t h;
  if (esp_timer_create(&a, &h) == ESP_OK) esp_timer_start_periodic(h, TICK_MS * 1000);
}

void safetyConfigure(bool estop, bool sound, uint8_t vol, bool keyBeep) {
  estopEnabled = estop && safetyEstopWired();
  estopOn = estopEnabled && estopRaw;
  soundEnabled = sound;
  volume = vol >= 1 && vol <= 5 ? vol : 3;
  keyBeepOn = keyBeep;
}

bool safetyEstopWired() { return ioPin(IO_ESTOP) >= 0; }
bool safetyEstop() { return estopOn; }
bool safetyEstopRaw() { return estopRaw; }

bool safetySoundWired() { return ioPin(IO_BUZZER) >= 0; }
void safetySound(SoundLevel level) { soundLevel = level; }
void safetySoundFinished() {
  if (soundEnabled) fxReq[FX_FINISHED] = true;
}
bool safetySoundMuted() { return mutedLevel != SOUND_NONE && soundLevel <= mutedLevel; }
void safetyFx(SoundFx fx) {
  if (fx > FX_TEST) return;
  if (fx != FX_TEST && !soundEnabled) return;
  if (fx == FX_KEY && !keyBeepOn) return;
  fxReq[fx] = true;
}
void safetyBeepTest() { safetyFx(FX_TEST); }

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
