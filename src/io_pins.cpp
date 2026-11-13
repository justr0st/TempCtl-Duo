#include "io_pins.h"
#include <Arduino.h>
#include "config.h"
#include "settings.h"

// fnPin[f]: the GPIO with function f now (-1 none). Read by any task (relay: local task, E-stop and buzzer:
// the safety timer, fan: control task) without a lock; ioConfigure() and ioWrite() take the mutex, so an
// output is never driven on a pin that is just being given another function.
static volatile int8_t fnPin[IO_FN_COUNT] = {-1, -1, -1, -1, -1, -1, -1};
static volatile uint32_t tachPulses = 0;
static SemaphoreHandle_t mtx = nullptr;

struct LockIo {
  LockIo() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~LockIo() { xSemaphoreGive(mtx); }
};

#if defined(IO_PINS_N)
static_assert(IO_PINS_N <= IO_PINS_MAX, "more optional pins than Settings::ioFn holds");
static const uint8_t PINS[IO_PINS_N] = IO_PINS_GPIO;
static uint8_t cur[IO_PINS_N];  // the function each pin has now (IO_NONE after ioEarlyInit)

static void IRAM_ATTR tachIsr() { tachPulses = tachPulses + 1; }  // (one writer: this interrupt)

// what a pin did before: undone, the pin back to the safe state (input, pull-down, nothing driven)
static void release(int pin, uint8_t f) {
  switch (f) {
    case IO_RELAY:
    case IO_FAN:
      digitalWrite(pin, LOW);
      break;
    case IO_BUZZER:
    case IO_FAN_PWM:
      ledcDetachPin(pin);
      break;
    case IO_FAN_TACH:
      detachInterrupt(digitalPinToInterrupt(pin));
      break;
  }
  pinMode(pin, INPUT_PULLDOWN);
}

static void start(int pin, uint8_t f) {
  switch (f) {
    case IO_RELAY:  // active high, off (the local task switches it, local_ctrl.cpp)
    case IO_FAN:
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
      break;
    case IO_ESTOP:  // NC contact to GND: open (pulled up) = E-stop
      pinMode(pin, INPUT_PULLUP);
      break;
    case IO_BUZZER:  // the buzzer's channel (safety.cpp sets its frequency and volume)
      ledcAttachPin(pin, IO_LEDC_BUZZER);
      break;
    case IO_FAN_PWM:  // off until fanSet()
      ledcSetup(IO_LEDC_FAN, IO_FAN_PWM_HZ, 8);
      ledcWrite(IO_LEDC_FAN, 0);
      ledcAttachPin(pin, IO_LEDC_FAN);
      break;
    case IO_FAN_TACH:  // open collector: pulled up, every falling edge counts
      pinMode(pin, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(pin), tachIsr, FALLING);
      break;
    default:
      pinMode(pin, INPUT_PULLDOWN);
      break;
  }
}
#endif

void ioEarlyInit() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
#if defined(IO_PINS_N)
  for (int f = 0; f < IO_FN_COUNT; ++f) fnPin[f] = -1;
  for (int i = 0; i < IO_PINS_N; ++i) {
    pinMode(PINS[i], INPUT_PULLDOWN);
    cur[i] = IO_NONE;
  }
#endif
}

void ioConfigure(const uint8_t* fn) {
#if defined(IO_PINS_N)
  if (!mtx) ioEarlyInit();
  LockIo g;
  uint8_t want[IO_PINS_N];
  for (int i = 0; i < IO_PINS_N; ++i) want[i] = fn[i] < IO_FN_COUNT ? fn[i] : (uint8_t)IO_NONE;
  // first every changed pin gives up its old function (a function that moves is off on its old pin before
  // it starts on the new one), then the new ones start
  for (int i = 0; i < IO_PINS_N; ++i) {
    if (want[i] == cur[i] || cur[i] == IO_NONE) continue;
    fnPin[cur[i]] = -1;
    release(PINS[i], cur[i]);
    cur[i] = IO_NONE;
  }
  for (int i = 0; i < IO_PINS_N; ++i) {
    if (want[i] == cur[i]) continue;
    start(PINS[i], want[i]);
    cur[i] = want[i];
    if (want[i] != IO_NONE) fnPin[want[i]] = (int8_t)PINS[i];
  }
#else
  (void)fn;
#endif
}

int ioPin(uint8_t fn) {
  if (fn == IO_NONE || fn >= IO_FN_COUNT) return -1;
#if !defined(IO_PINS_N) && defined(PIN_BUZZER)
  if (fn == IO_BUZZER) return PIN_BUZZER;  // (CYD: the speaker, fixed)
#endif
  return fnPin[fn];
}

bool ioWrite(uint8_t fn, bool high) {
  if (!mtx || fn == IO_NONE || fn >= IO_FN_COUNT) return false;
  LockIo g;
  const int pin = fnPin[fn];
  if (pin < 0) return false;
  digitalWrite(pin, high ? HIGH : LOW);
  return true;
}

uint32_t ioTachPulses() { return tachPulses; }
