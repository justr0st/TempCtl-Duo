#include "status_led.h"
#include <Arduino.h>
#include "config.h"

static volatile LedStatus status = LedStatus::Off;
static int32_t shown = -1;  // last written colour (0xRRGGBB) / level

LedStatus statusLedPick(bool fault, bool wait, bool ap, bool running) {
  if (fault) return LedStatus::Fault;
  if (wait) return LedStatus::Wait;
  if (ap) return LedStatus::Ap;
  if (running) return LedStatus::Running;
  return LedStatus::Idle;
}

void statusLedSet(LedStatus s) { status = s; }

static bool blink(uint32_t periodMs) { return (millis() % periodMs) < periodMs / 2; }

#if defined(STATUS_LED_WS2812) || defined(STATUS_LED_RGB_PWM)
#if defined(STATUS_LED_WS2812)
static void writeRaw(uint32_t rgb) { neopixelWrite(PIN_STATUS_LED, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
static void ledInit() {}
#else
// RGB LED, common anode (active low): PWM duty 255 = off
static void writeRaw(uint32_t rgb) {
  ledcWrite(1, 255 - ((rgb >> 16) & 0xFF));
  ledcWrite(2, 255 - ((rgb >> 8) & 0xFF));
  ledcWrite(3, 255 - (rgb & 0xFF));
}
static void ledInit() {
  const uint8_t pins[3] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};
  for (uint8_t i = 0; i < 3; ++i) {
    ledcSetup(1 + i, LEDC_PWM_HZ, 8);  // (channel 1 shares the backlight's timer)
    ledcWrite(1 + i, 255);  // off before the pin is attached (duty 0 would light it)
    ledcAttachPin(pins[i], 1 + i);
  }
}
#endif

static void write(uint32_t rgb) {
  if ((int32_t)rgb == shown) return;
  shown = rgb;
  writeRaw(rgb);
}

// colours at full level, scaled to STATUS_LED_LEVEL
static uint32_t scale(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t L = STATUS_LED_LEVEL;
  return ((r * L / 255) << 16) | ((g * L / 255) << 8) | (b * L / 255);
}

static void ledBegin() {
  ledInit();
  shown = -1;
  write(0);
}

static void ledLoop() {
  switch (status) {
    case LedStatus::Off: write(0); break;
    case LedStatus::Idle: write(scale(0, 255, 0)); break;
    case LedStatus::Running: write(scale(0, 0, 255)); break;
    case LedStatus::Ap: write(scale(160, 0, 255)); break;
    case LedStatus::Wait: write(blink(1000) ? scale(255, 120, 0) : 0); break;
    case LedStatus::Fault: write(blink(400) ? scale(255, 0, 0) : 0); break;
  }
}

#elif defined(STATUS_LED_SINGLE)
static void write(bool on) {
  if ((int32_t)on == shown) return;
  shown = on;
  digitalWrite(PIN_STATUS_LED, (on != (STATUS_LED_ACTIVE_LOW != 0)) ? HIGH : LOW);
}

static void ledBegin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  shown = -1;
  write(false);
}

static void ledLoop() {
  switch (status) {
    case LedStatus::Running: write(true); break;
    case LedStatus::Wait: write(blink(1000)); break;
    case LedStatus::Fault: write(blink(250)); break;
    default: write(false); break;
  }
}

#else
static void ledBegin() {}
static void ledLoop() {}
#endif

// Own task: the control task can block for seconds (Modbus timeouts without a
// controller), which made the blinking irregular.
static void ledTask(void*) {
  for (;;) {
    ledLoop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void statusLedBegin() {
  ledBegin();
  xTaskCreate(ledTask, "statusled", 3072, nullptr, 1, nullptr);
}
