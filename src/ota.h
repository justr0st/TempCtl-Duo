// Firmware update without USB (spec 9.11): a .bin uploaded on the web page (all boards) or a firmware.bin
// in the SD card's root (S3). The image goes into the other OTA slot; it is checked while it streams in
// (ESP image header + chip, the TempCtl Duo id string with the board id: an image for another env /
// board is refused). After the restart the new firmware runs "on probation": it is confirmed once the
// loop and the control task have run for 30 s; a crash or reset before that makes the bootloader start
// the previous firmware again (rollback), which then logs that the update failed.
#pragma once
#include <stddef.h>
#include <stdint.h>

extern const char OTA_IMAGE_ID[];  // "TCDUO-IMAGE|<board id>|<version>|end"

enum OtaSource : uint8_t { OTA_NONE = 0, OTA_WEB = 1, OTA_SD = 2 };
enum OtaPhase : uint8_t { OTA_IDLE = 0, OTA_RUNNING, OTA_DONE, OTA_FAILED };
struct OtaStatus {
  uint8_t phase, source;
  uint32_t done, total;  // bytes (total 0 = unknown)
  char version[16];      // of the image being written (once its id was seen)
  char msg[96];          // failure / result text
};
void otaStatus(OtaStatus& s);

// the other firmware slot: false (+ why) if the partition table has none (CYD before the OTA table)
bool otaSlot(uint32_t& size, char* why, size_t n);
const char* otaRunningSlot();  // "app0" / "app1"
bool otaProbation();           // this firmware is new and not confirmed yet
// May an update start now? (slot, no program run, all outputs off, no autotune, not already busy)
bool otaCanStart(char* err, size_t n);

// the stream: web upload (loop task) or SD card (SD task)
bool otaBegin(OtaSource src, uint32_t total, char* err, size_t n);
bool otaWrite(const uint8_t* p, size_t n);  // false: failed (otaStatus msg); later calls are ignored
bool otaEnd(char* err, size_t n);           // checks the image, makes it the boot firmware; restart follows
void otaAbort(const char* why);

void otaBoot();        // first in setup(): probation state of this firmware
void otaBootReport();  // after the file system is mounted: the result of the last update (event log)
void otaLoop();        // loop task: confirmation after 30 s, the restart after a finished update
