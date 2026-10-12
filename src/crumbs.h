// Breadcrumbs (diagnosis of watchdog resets and crashes): what the loop, the control task and the SD
// card were doing, kept in RTC memory over a reset. After a reset by a watchdog or a crash the boot logs
// an event with them ("task watchdog - loop: LCD SPI wait 15.0s; control: waiting 0.0s; SD: mount 15.2s":
// what each was doing and how long before the last breadcrumb it got there).
// Codes, not strings, so nothing points into memory of the previous run. Cheap: a store each.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum CrumbSlot : uint8_t { CR_LOOP = 0, CR_CONTROL, CR_SD, CR_N };

enum CrumbCode : uint8_t {
  // loop
  L_SYSLOG = 1, L_UI, L_SCREENCHECK, L_LVGL, L_LCD_WAIT, L_CONSOLE, L_WEB, L_WEB_REQ, L_END,
  // control task
  C_WAIT = 20, C_CMD, C_REQUEST, C_POLL, C_CYCLE,
  // SD card
  S_IDLE = 40, S_LOCK, S_MOUNT, S_WORK, S_UNMOUNT,
};

void crumb(CrumbSlot s, CrumbCode c, int16_t arg = 0);  // arg: e.g. the command number
void crumbWeb(const char* path);                         // the web request being answered (copied)
CrumbCode crumbGet(CrumbSlot s);
// after a watchdog / crash: the crumbs of the previous run into b (false: none / not wanted); clears them
bool crumbReport(char* b, size_t n);
