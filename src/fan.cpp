#include "fan.h"
#include <Arduino.h>
#include "io_pins.h"
#include "settings.h"

static volatile bool isOn = false;
static volatile int rpm = -1;

void fanSet(bool on, uint8_t speedPct) {
  // written every cycle, not only on a change: a pin that just got the fan function starts off (ioConfigure)
  if (speedPct > 100) speedPct = 100;
  const bool pwm = ioPin(IO_FAN_PWM) >= 0;
  if (pwm) ledcWrite(IO_LEDC_FAN, on ? (uint32_t)speedPct * 255u / 100u : 0u);
  const bool sw = ioWrite(IO_FAN, on);
  isOn = on && (pwm || sw);
  // speed from the tach pulses, about once a second
  static uint32_t atMs = 0, atPulses = 0;
  static int tachPin = -1;
  const int tp = ioPin(IO_FAN_TACH);
  const uint32_t now = millis(), pulses = ioTachPulses();
  if (tp != tachPin || !atMs) {  // (new tach pin: count from now)
    tachPin = tp;
    atMs = now ? now : 1;
    atPulses = pulses;
    rpm = tp >= 0 ? 0 : -1;
    return;
  }
  if (tp < 0 || now - atMs < 1000) return;
  // 2 pulses per turn: turns per minute = pulses / 2 * 60000 / ms
  rpm = (int)((uint64_t)(pulses - atPulses) * 30000u / (now - atMs));
  atMs = now ? now : 1;
  atPulses = pulses;
}

bool fanWired() { return ioPin(IO_FAN) >= 0 || ioPin(IO_FAN_PWM) >= 0; }
bool fanOn() { return isOn; }
int fanRpm() { return rpm; }
