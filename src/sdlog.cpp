// Run logs on the SD card (sdlog.h)
#include "ota.h"
#include "ota_image.h"
#include "sdlog.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "config.h"

#if defined(PIN_SD_CS) && defined(BOARD_HAS_PSRAM)
#include <ctype.h>
#include <dirent.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <freertos/semphr.h>
#include <sdmmc_cmd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "app_state.h"
#include "console.h"
#include "crumbs.h"
#include "ff.h"
#include "lcd.h"
#include "syslog.h"

static const char* const MNT = "/sd";
static const char* const DIR_RUNS = "/sd/runs";
static const char* const DIR_BACKUP = "/sd/backup";
static constexpr size_t BUF = 64 * 1024;           // per buffer (two: one fills while the other is emptied)
static constexpr size_t PEND = 192 * 1024;         // taken from the buffers, kept until it is on the card
static constexpr uint32_t FLUSH_MS = 30000;        // write at least every 30 s
static constexpr uint32_t RETRY_MS = 5000;         // after a failed write
static constexpr size_t LOAD_MAX = 4 * 1024 * 1024; // web download: larger files only with a PC

// All card work happens in one task (the SD task: own large stack, core 1). Rows are appended to bufA
// (any task, under mux); the SD task swaps the buffers, moves the full one into pend and writes pend;
// pend is only emptied once the card took it, so a failed write is tried again and loses nothing
// (unless pend overflows: then the newest rows are counted as lost and noted in the file).
static char *bufA = nullptr, *bufB = nullptr;
static size_t used = 0;
static char* pend = nullptr;
static size_t pendLen = 0;
static uint32_t dropped = 0;    // rows that did not fit into bufA (between two swaps)
static uint32_t lostBytes = 0;  // bytes that did not fit into pend
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t wmx = nullptr;  // run start (control task) vs. the SD task's writing
static TaskHandle_t sdTask = nullptr;
static bool failing = false;             // the last write failed (warning event once, "written again" after)

// the run's file: base name until the first write picks the final one ("runs/<base>.csv", "_2" ...)
static volatile bool open_ = false, startPending = false, endPending = false;
static char base[64], file[80];
static float lastRunS = 0;
static uint32_t lastFlush = 0;
static int8_t cardSeen = -1;
static char lastErr[64] = "";
static uint32_t runNo = 0;
static sdmmc_card_t card;
static sdspi_dev_handle_t devH = -1;  // the SD device on the display's SPI bus while mounted
static BYTE pdrv = 0xFF;              // FATFS drive number while mounted
static char drv[3] = "0:";

bool sdlogSlot() { return true; }

// ------------------------------------------------------------ card
static void csHigh() {  // the card stays deselected (after an unmount the CS pin is reset)
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
}

// The SD device on the bus, FATFS and the VFS path, step by step (what esp_vfs_fat_sdspi_mount does), so
// that every failure removes the SD device from the bus again: a device left on the bus would keep the
// screen check from freeing it. The card is never formatted.
static void removeDevice() {
  if (devH >= 0) sdspi_host_remove_device(devH);
  devH = -1;
  csHigh();
}

static bool mountCard(char* err, size_t n) {
  // no card: the data line would float and the driver read noise as answers (slow retries); with the
  // pull-up it reads 0xFF ("no answer") and gives up at once
  gpio_pullup_en((gpio_num_t)LCD_PIN_MISO);
  sdspi_host_init();  // (no-op when already done)
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = SPI2_HOST;
  dev.gpio_cs = (gpio_num_t)PIN_SD_CS;
  esp_err_t e = sdspi_host_init_device(&dev, &devH);
  if (e != ESP_OK) {
    devH = -1;
    csHigh();
    snprintf(err, n, "SD device not added (%s)", esp_err_to_name(e));
    return false;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = devH;
  host.max_freq_khz = 10000;  // (shared lines with the display: a slower clock is safer; rows are small)
  e = sdmmc_card_init(&host, &card);
  if (e != ESP_OK) {
    removeDevice();
    snprintf(err, n, "no card (%s)", esp_err_to_name(e));
    cardSeen = 0;
    return false;
  }
  if (ff_diskio_get_drive(&pdrv) != ESP_OK) {
    removeDevice();
    snprintf(err, n, "no free FAT drive");
    return false;
  }
  ff_diskio_register_sdmmc(pdrv, &card);
  drv[0] = (char)('0' + pdrv);
  FATFS* fs = nullptr;
  e = esp_vfs_fat_register(MNT, drv, 2, &fs);
  if (e != ESP_OK || f_mount(fs, drv, 1) != FR_OK) {
    if (e == ESP_OK) esp_vfs_fat_unregister_path(MNT);
    ff_diskio_unregister(pdrv);
    pdrv = 0xFF;
    removeDevice();
    snprintf(err, n, e == ESP_OK ? "no FAT file system on the card" : "VFS error (%s)", esp_err_to_name(e));
    cardSeen = 1;
    return false;
  }
  cardSeen = 1;
  return true;
}

static void unmountCard() {
  if (pdrv != 0xFF) {
    f_mount(nullptr, drv, 0);
    esp_vfs_fat_unregister_path(MNT);
    ff_diskio_unregister(pdrv);
    pdrv = 0xFF;
  }
  removeDevice();
}

// the card mounted for fn() (bus lock held meanwhile; the screen check holds it only for a few ms, a web
// request for one file operation)
template <typename F>
static bool withCard(char* err, size_t n, F fn, uint32_t waitMs = 5000) {
  crumb(CR_SD, S_LOCK);
  if (!lcdBusLock(waitMs)) {
    crumb(CR_SD, S_IDLE);
    snprintf(err, n, "SPI bus busy");
    return false;
  }
  crumb(CR_SD, S_MOUNT);
  bool ok = mountCard(err, n);
  if (!ok && cardSeen != 0) {  // (a card that answered but failed: once more)
    vTaskDelay(pdMS_TO_TICKS(100));
    ok = mountCard(err, n);
  }
  if (ok) {
    crumb(CR_SD, S_WORK);
    ok = fn();
    crumb(CR_SD, S_UNMOUNT);
    unmountCard();
  }
  lcdBusUnlock();
  crumb(CR_SD, S_IDLE);
  return ok;
}

// ------------------------------------------------------------ buffer
static void append(const char* s, size_t n) {
  if (!bufA) return;
  portENTER_CRITICAL(&mux);
  if (used + n <= BUF) {
    memcpy(bufA + used, s, n);
    used += n;
  } else {
    ++dropped;
  }
  portEXIT_CRITICAL(&mux);
}

static int timeText(char* b, size_t n) {
  const uint32_t ep = syslogEpoch();
  if (!ep) {
    if (n) b[0] = 0;
    return 0;
  }
  const time_t t = ep;
  struct tm tm;
  localtime_r(&t, &tm);  // local time (time zone setting, applied by syslogLoop; not set: UTC)
  return (int)strftime(b, n, "%Y-%m-%d %H:%M:%S", &tm);
}

static size_t csvQuoted(char* b, size_t n, const char* s) {  // "text" with "" for "
  size_t k = 0;
  if (k + 1 < n) b[k++] = '"';
  for (; *s && k + 3 < n; ++s) {
    if (*s == '"') b[k++] = '"';
    b[k++] = *s == '\n' || *s == '\r' ? ' ' : *s;
  }
  if (k + 1 < n) b[k++] = '"';
  b[k] = 0;
  return k;
}

static const char* const HEADER =
    "time,run_s,where,bottom_pv,bottom_sp,bottom_prog,bottom_out,bottom_state,top_pv,top_sp,top_prog,top_out,"
    "top_state,kind,event\n";

// ------------------------------------------------------------ writing
static bool nameUsed(const char* rel) {
  char p[112];
  snprintf(p, sizeof(p), "%s/%s", MNT, rel);
  struct stat st;
  return stat(p, &st) == 0;
}

// SD task: moves the filled buffer into pend and writes pend; at a run's start it picks the file name first
static void flush(bool closing) {
  xSemaphoreTake(wmx, portMAX_DELAY);
  size_t n;
  uint32_t lost;
  portENTER_CRITICAL(&mux);
  char* t = bufA;
  bufA = bufB;
  bufB = t;
  n = used;
  used = 0;
  lost = dropped;
  dropped = 0;
  portEXIT_CRITICAL(&mux);
  if (lost) {  // a note row first
    char l[96];
    const int k = snprintf(l, sizeof(l), ",,,,,,,,,,,,,warning,\"%lu rows lost (no room in the buffer)\"\n", (unsigned long)lost);
    if (pendLen + k <= PEND) {
      memcpy(pend + pendLen, l, k);
      pendLen += k;
    }
  }
  if (pendLen + n <= PEND) {
    memcpy(pend + pendLen, bufB, n);
    pendLen += n;
  } else {
    lostBytes += n;
  }
  char err[64] = "";
  const bool starting = startPending;
  const uint32_t lostNow = lostBytes;
  bool ok = withCard(err, sizeof(err), [&]() -> bool {
    if (starting) {
      mkdir(DIR_RUNS, 0775);
      snprintf(file, sizeof(file), "runs/%s.csv", base);
      for (int k = 2; k < 100 && nameUsed(file); ++k) snprintf(file, sizeof(file), "runs/%s_%d.csv", base, k);
    }
    char p[112];
    snprintf(p, sizeof(p), "%s/%s", MNT, file);
    FILE* f = fopen(p, starting ? "w" : "a");
    if (!f) {
      snprintf(err, sizeof(err), "cannot open %.48s", file);
      return false;
    }
    bool good = !starting || fwrite(HEADER, 1, strlen(HEADER), f) == strlen(HEADER);
    good &= fwrite(pend, 1, pendLen, f) == pendLen;
    if (lostNow) {
      char l[112];
      const int k = snprintf(l, sizeof(l), ",,,,,,,,,,,,,warning,\"about %lu kB of rows lost (the card could not be written for a while)\"\n",
                             (unsigned long)(lostNow / 1024 + 1));
      good &= fwrite(l, 1, k, f) == (size_t)k;
    }
    good &= fclose(f) == 0;
    if (!good) snprintf(err, sizeof(err), "write error on %.44s", file);
    return good;
  }, 60000);
  lastFlush = millis();
  if (ok) {
    pendLen = 0;
    lostBytes = 0;
  }
  if (starting) {
    startPending = false;
    if (ok) {
      LogSerial.printf("[sd] run log: %s\n", file);
    } else {  // no card (or not writable): this run is not logged
      LogSerial.printf("[sd] %s - the run is not logged\n", err);
      open_ = false;
      pendLen = 0;
      portENTER_CRITICAL(&mux);
      used = 0;
      portEXIT_CRITICAL(&mux);
    }
  } else if (!ok && !failing) {
    failing = true;
    logEvent("SD card: writing the run log failed (%s) - trying again", err);  // (a row in the buffer too)
  } else if (ok && failing) {
    failing = false;
    LogSerial.println("[sd] run log written again");
  }
  strlcpy(lastErr, ok ? "" : err, sizeof(lastErr));
  if (closing && (ok || !open_)) {
    open_ = false;
    endPending = false;
  }
  xSemaphoreGive(wmx);
}

// ------------------------------------------------------------ web requests (done by the SD task)
enum ReqType : uint8_t { RQ_NONE = 0, RQ_LIST, RQ_LOAD, RQ_DELETE, RQ_FWCHECK, RQ_BACKUP };
static struct {
  volatile uint8_t type;
  volatile bool abandoned;  // the web gave up waiting: the SD task cleans up and frees the slot
  uint8_t dir;              // SdDir
  char name[64];
  bool ok;
  char err[96];
  SdFile* files;  // PSRAM, LIST_MAX entries (owned by this module)
  int count;
  uint32_t totalKb, freeKb;
  char* data;  // LOAD: PSRAM, handed to the web (or freed when abandoned)
  size_t len;
  char* wdata;  // BACKUP: the file's content (owned by the request: the SD task frees it)
  size_t wlen;
  SdFirmware fw;  // FWCHECK
} rq;
static volatile bool fwInstall = false;  // install /firmware.bin (started by sdlogFirmwareInstall)
static void installFirmware();
static constexpr int LIST_MAX = 200;
static SemaphoreHandle_t rqFree = nullptr, rqDone = nullptr;  // binary: one request at a time / answered
static void doRequest();

static void writerTask(void*) {
  uint32_t closeTries = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));  // (a web request wakes it at once)
    if (rq.type) doRequest();
    if (fwInstall) installFirmware();
    if (!open_) continue;
    size_t n;
    portENTER_CRITICAL(&mux);
    n = used;
    portEXIT_CRITICAL(&mux);
    const bool end = endPending;
    const uint32_t wait = failing ? RETRY_MS : FLUSH_MS;
    if (startPending || end || n > BUF / 2 || ((n || pendLen) && millis() - lastFlush >= wait)) {
      if (end && failing && ++closeTries > 3) {  // the run ended and the card still fails: give up
        logEvent("SD card: the end of the run log could not be written (%s)", lastErr);
        xSemaphoreTake(wmx, portMAX_DELAY);
        pendLen = 0;
        open_ = endPending = failing = false;
        xSemaphoreGive(wmx);
        continue;
      }
      if (end && failing) vTaskDelay(pdMS_TO_TICKS(1000));
      flush(end);
      if (!open_) closeTries = 0;
    }
  }
}

void sdlogBegin() {
  bufA = (char*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bufB = (char*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  pend = (char*)heap_caps_malloc(PEND, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  rq.files = (SdFile*)heap_caps_malloc(sizeof(SdFile) * LIST_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  wmx = xSemaphoreCreateMutex();
  rqFree = xSemaphoreCreateBinary();
  rqDone = xSemaphoreCreateBinary();
  if (!bufA || !bufB || !pend || !rq.files || !wmx || !rqFree || !rqDone) {
    LogSerial.println("[sd] no memory for the run log buffers");
    bufA = nullptr;  // (everything stays off)
    return;
  }
  xSemaphoreGive(rqFree);
  csHigh();
  // all card work in this task: a large stack (FATFS with long names, the SD driver), core 1 (the SD
  // driver waits for the card in polling loops; on core 0 a long wait would starve the idle task there,
  // which the task watchdog watches)
  xTaskCreatePinnedToCore(writerTask, "sdlog", 12288, nullptr, 1, &sdTask, 1);
}

// ------------------------------------------------------------ the run
void sdlogRunStart(const char* prog, bool timeline, bool resumed) {
  if (!bufA) return;
  // the previous run's last rows (a start right after an end): the writer closes it within 0.2 s; the
  // control task does no card work itself
  for (int i = 0; open_ && i < 40; ++i) vTaskDelay(pdMS_TO_TICKS(50));
  if (open_) {
    LogSerial.println("[sd] the previous run's log is still being written - its last rows are dropped");
    xSemaphoreTake(wmx, portMAX_DELAY);
    open_ = endPending = false;
    xSemaphoreGive(wmx);
  }
  xSemaphoreTake(wmx, portMAX_DELAY);  // (no flush while the new run is set up)
  // file name: start time (local) + program, else start number + run number
  char nm[32];
  size_t k = 0;
  for (const char* s = prog; *s && k + 1 < sizeof(nm); ++s) {
    const char c = *s;
    nm[k++] = isalnum((unsigned char)c) || c == '-' || c == '(' || c == ')' ? c : '_';
  }
  nm[k] = 0;
  ++runNo;
  const uint32_t ep = syslogEpoch();
  if (ep) {
    const time_t t = ep;
    struct tm tm;
    localtime_r(&t, &tm);
    char d[24];
    strftime(d, sizeof(d), "%Y-%m-%d_%H%M%S", &tm);
    snprintf(base, sizeof(base), "%s_%s", d, nm);
  } else {
    snprintf(base, sizeof(base), "start%lu_run%lu_%s", (unsigned long)syslogBoot(), (unsigned long)runNo, nm);
  }
  portENTER_CRITICAL(&mux);
  used = 0;
  dropped = 0;
  portEXIT_CRITICAL(&mux);
  lastRunS = 0;
  file[0] = 0;
  startPending = true;
  endPending = false;
  open_ = true;
  char l[160];
  char tx[24];
  timeText(tx, sizeof(tx));
  const int n = snprintf(l, sizeof(l), "%s,,,,,,,,,,,,,info,\"%s %s (%s)\"\n", tx, resumed ? "Resumed:" : "Program:", prog,
                         timeline ? "Timeline" : "Sequence");
  append(l, n);
  xSemaphoreGive(wmx);
}

static void num(char*& p, char* end, float v, int dec) {
  if (!isnan(v) && p < end) p += snprintf(p, end - p, "%.*f", dec, v);
  if (p < end) *p++ = ',';
}

void sdlogRow(const SdRow& r) {
  if (!open_) return;
  lastRunS = r.runS;
  char l[256];
  char* p = l;
  char* const end = l + sizeof(l) - 4;
  p += timeText(p, end - p);
  p += snprintf(p, end - p, ",%.0f,%s,", r.runS, r.where);
  for (int c = 0; c < 2; ++c) {
    num(p, end, r.pv[c], 1);
    num(p, end, r.sp[c], 1);
    num(p, end, r.prog[c], 1);
    num(p, end, r.out[c], 0);
    p += snprintf(p, end - p, "%s,", r.state[c] ? r.state[c] : "");
  }
  *p++ = ',';  // kind, event: empty
  *p++ = '\n';
  append(l, p - l);
}

void sdlogEvent(uint8_t kind, const char* text) {
  if (!open_) return;
  char l[256];
  int k = timeText(l, sizeof(l));
  k += snprintf(l + k, sizeof(l) - k, ",%.0f,,,,,,,,,,,,%s,", lastRunS, logKindName((LogKind)kind));
  k += csvQuoted(l + k, sizeof(l) - k - 1, text);
  l[k++] = '\n';
  append(l, k);
}

void sdlogRunEnd() {
  if (open_) endPending = true;  // the writer writes the rest (events of the end actions included)
}

void sdlogState(SdState& s) {
  s.slot = true;
  s.card = cardSeen;
  s.logging = open_;
  strlcpy(s.file, file, sizeof(s.file));
  strlcpy(s.err, lastErr, sizeof(s.err));
  portENTER_CRITICAL(&mux);
  s.buffered = used;
  portEXIT_CRITICAL(&mux);
}

// ------------------------------------------------------------ web
static const char* dirPath(uint8_t dir) { return dir == SD_BACKUPS ? DIR_BACKUP : DIR_RUNS; }

static bool nameOk(const char* name, uint8_t dir) {
  const size_t n = strlen(name);
  const char* ext = dir == SD_BACKUPS ? ".json" : ".csv";
  const size_t e = strlen(ext);
  if (n <= e || n > 60 || strcmp(name + n - e, ext)) return false;
  return !strchr(name, '/') && !strchr(name, '\\') && !strstr(name, "..");
}

// --- run by the SD task
static int listFiles(uint8_t dir, SdFile* out, int max, uint32_t& totalKb, uint32_t& freeKb, char* err, size_t n) {
  int count = 0;
  totalKb = freeKb = 0;
  const bool ok = withCard(err, n, [&]() -> bool {
    FATFS* fs = nullptr;
    DWORD fre = 0;
    if (f_getfree(drv, &fre, &fs) == FR_OK && fs) {
      const uint64_t clus = (uint64_t)fs->csize * 512 / 1024;  // kB per cluster (SD: 512-byte sectors)
      totalKb = (uint32_t)((fs->n_fatent - 2) * clus);
      freeKb = (uint32_t)(fre * clus);
    }
    DIR* d = opendir(dirPath(dir));
    if (!d) return true;  // nothing written yet
    char p[96];
    for (struct dirent* e; (e = readdir(d)) != nullptr && count < max;) {
      if (!nameOk(e->d_name, dir)) continue;
      SdFile& f = out[count];
      strlcpy(f.name, e->d_name, sizeof(f.name));
      snprintf(p, sizeof(p), "%s/%.60s", dirPath(dir), e->d_name);
      struct stat st;
      if (stat(p, &st) != 0) continue;
      f.size = (uint32_t)st.st_size;
      f.mtime = st.st_mtime > 1700000000 ? (uint32_t)st.st_mtime : 0;
      ++count;
    }
    closedir(d);
    return true;
  });
  if (!ok) return -1;
  // newest first (names begin with the date; files without one by their time)
  for (int i = 1; i < count; ++i)
    for (int j = i; j > 0; --j) {
      const SdFile &a = out[j - 1], &b = out[j];
      const bool later = b.mtime != a.mtime ? b.mtime > a.mtime : strcmp(b.name, a.name) > 0;
      if (!later) break;
      SdFile t = out[j - 1];
      out[j - 1] = out[j];
      out[j] = t;
    }
  return count;
}

static char* loadFile(uint8_t dir, const char* name, size_t& len, char* err, size_t n) {
  len = 0;
  char* data = nullptr;
  withCard(err, n, [&]() -> bool {
    char p[96];
    snprintf(p, sizeof(p), "%s/%s", dirPath(dir), name);
    struct stat st;
    if (stat(p, &st) != 0) {
      snprintf(err, n, "no such file");
      return false;
    }
    const size_t room = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if ((size_t)st.st_size > LOAD_MAX || (size_t)st.st_size + 256 * 1024 > room) {
      snprintf(err, n, "too large for the web page (%lu kB): read the card in a PC", (unsigned long)(st.st_size / 1024));
      return false;
    }
    data = (char*)heap_caps_malloc(st.st_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    FILE* f = data ? fopen(p, "r") : nullptr;
    if (!f) {
      free(data);
      data = nullptr;
      snprintf(err, n, "cannot read the file");
      return false;
    }
    len = fread(data, 1, st.st_size, f);
    fclose(f);
    return true;
  });
  return data;
}

static bool deleteFile(uint8_t dir, const char* name, char* err, size_t n) {
  return withCard(err, n, [&]() -> bool {
    char p[96];
    snprintf(p, sizeof(p), "%s/%s", dirPath(dir), name);
    if (unlink(p) != 0) {
      snprintf(err, n, "could not delete the file");
      return false;
    }
    return true;
  });
}

// a backup file: name = base on entry, the file name written on return
static bool writeBackup(const char* data, size_t len, char* name, size_t nn, char* err, size_t n) {
  char base[64];
  strlcpy(base, name, sizeof(base));
  return withCard(err, n, [&]() -> bool {
    mkdir(DIR_BACKUP, 0775);
    char rel[96];
    snprintf(rel, sizeof(rel), "backup/%s.json", base);
    for (int k = 2; k < 100 && nameUsed(rel); ++k) snprintf(rel, sizeof(rel), "backup/%s_%d.json", base, k);
    char p[112];
    snprintf(p, sizeof(p), "%s/%s", MNT, rel);
    FILE* f = fopen(p, "w");
    if (!f) return snprintf(err, n, "cannot create %.60s", rel), false;
    bool good = fwrite(data, 1, len, f) == len;
    good &= fclose(f) == 0;
    if (!good) {
      unlink(p);
      snprintf(err, n, "write error (card full?)");
      return false;
    }
    strlcpy(name, rel + 7, nn);  // ("backup/")
    return true;
  });
}

// ------------------------------------------------------------ firmware update from the card
static const char* const FW_FILE = "/sd/firmware.bin";
static const char* const FW_DONE = "/sd/firmware.done";
static constexpr size_t FW_CHUNK = 4096;

static bool checkFirmware(SdFirmware& f, char* err, size_t n) {
  memset(&f, 0, sizeof(f));
  return withCard(err, n, [&]() -> bool {
    FILE* fp = fopen(FW_FILE, "rb");
    if (!fp) return true;  // none on the card
    uint8_t* buf = (uint8_t*)malloc(FW_CHUNK);
    if (!buf) {
      fclose(fp);
      snprintf(err, n, "out of memory");
      return false;
    }
    ImageScanner sc;
    sc.reset();
    for (size_t k; (k = fread(buf, 1, FW_CHUNK, fp)) > 0;) sc.feed(buf, k);
    fclose(fp);
    free(buf);
    f.found = true;
    f.size = sc.size();
    strlcpy(f.board, sc.board(), sizeof(f.board));
    strlcpy(f.version, sc.version(), sizeof(f.version));
    strlcpy(f.problem, imageProblem(sc, BOARD_ID, BOARD_CHIP), sizeof(f.problem));
    return true;
  });
}

// in the SD task: the card stays mounted (bus lock held) for the whole install, about 20 s; the display
// keeps drawing (only its screen check waits)
static void installFirmware() {
  fwInstall = false;
  char err[96] = "";
  bool written = false;
  const bool ok = withCard(err, sizeof(err), [&]() -> bool {
    FILE* fp = fopen(FW_FILE, "rb");
    if (!fp) return snprintf(err, sizeof(err), "no firmware.bin on the card"), false;
    struct stat stt;
    const uint32_t size = stat(FW_FILE, &stt) == 0 ? (uint32_t)stt.st_size : 0;
    uint8_t* buf = (uint8_t*)malloc(FW_CHUNK);
    if (!buf || !otaBegin(OTA_SD, size, err, sizeof(err))) {
      free(buf);
      fclose(fp);
      if (!err[0]) snprintf(err, sizeof(err), "out of memory");
      return false;
    }
    bool good = true;
    for (size_t k; good && (k = fread(buf, 1, FW_CHUNK, fp)) > 0;) good = otaWrite(buf, k);
    if (good && ferror(fp)) {
      otaAbort("reading the card failed");
      good = false;
    }
    fclose(fp);
    free(buf);
    if (!good) {
      OtaStatus o;
      otaStatus(o);
      snprintf(err, sizeof(err), "%s", o.msg);
      return false;
    }
    if (!otaEnd(err, sizeof(err))) return false;
    written = true;
    unlink(FW_DONE);  // installed: renamed, so it is not offered again
    if (rename(FW_FILE, FW_DONE) != 0) logEvent("SD card: firmware.bin could not be renamed (delete it in a PC)");
    return true;
  }, 10000);
  if (!ok && !written) {
    OtaStatus o;
    otaStatus(o);
    if (o.phase == OTA_RUNNING) otaAbort(err);  // (card lost after the start)
    else if (o.phase != OTA_FAILED) logEvent("Firmware update from the SD card not started: %s", err);
  }
}

static void doRequest() {
  rq.err[0] = 0;
  rq.data = nullptr;
  rq.len = 0;
  switch (rq.type) {
    case RQ_LIST:
      rq.count = listFiles(rq.dir, rq.files, LIST_MAX, rq.totalKb, rq.freeKb, rq.err, sizeof(rq.err));
      rq.ok = rq.count >= 0;
      break;
    case RQ_LOAD:
      rq.data = loadFile(rq.dir, rq.name, rq.len, rq.err, sizeof(rq.err));
      rq.ok = rq.data != nullptr;
      break;
    case RQ_DELETE: rq.ok = deleteFile(rq.dir, rq.name, rq.err, sizeof(rq.err)); break;
    case RQ_FWCHECK: rq.ok = checkFirmware(rq.fw, rq.err, sizeof(rq.err)); break;
    case RQ_BACKUP: rq.ok = writeBackup(rq.wdata, rq.wlen, rq.name, sizeof(rq.name), rq.err, sizeof(rq.err)); break;
    default: rq.ok = false;
  }
  free(rq.wdata);
  rq.wdata = nullptr;
  portENTER_CRITICAL(&mux);  // (decided together with the web side's give-up, so exactly one frees the slot)
  rq.type = RQ_NONE;
  const bool gone = rq.abandoned;
  portEXIT_CRITICAL(&mux);
  if (gone) {  // nobody waits any more
    free(rq.data);
    rq.data = nullptr;
    xSemaphoreGive(rqFree);
  } else {
    xSemaphoreGive(rqDone);
  }
}

// --- web side (loop task): hands the request to the SD task and waits for the answer
static constexpr uint32_t WEB_WAIT_MS = 8000;  // (the loop watchdog allows 15 s)

// wdata (RQ_BACKUP): taken over in any case
static bool ask(uint8_t type, const char* name, char* err, size_t n, uint8_t dir = SD_RUNS, char* wdata = nullptr,
                size_t wlen = 0) {
  if (!sdTask || !rqFree) {
    free(wdata);
    snprintf(err, n, "SD card not available");
    return false;
  }
  if (xSemaphoreTake(rqFree, pdMS_TO_TICKS(WEB_WAIT_MS)) != pdTRUE) {
    free(wdata);
    snprintf(err, n, "SD card busy - try again");
    return false;
  }
  xSemaphoreTake(rqDone, 0);  // (clear)
  strlcpy(rq.name, name ? name : "", sizeof(rq.name));
  rq.dir = dir;
  rq.wdata = wdata;
  rq.wlen = wlen;
  rq.abandoned = false;
  rq.type = type;
  xTaskNotifyGive(sdTask);
  if (xSemaphoreTake(rqDone, pdMS_TO_TICKS(WEB_WAIT_MS)) != pdTRUE) {
    portENTER_CRITICAL(&mux);
    const bool running = rq.type != RQ_NONE;
    if (running) rq.abandoned = true;  // the SD task frees the slot when it is done
    portEXIT_CRITICAL(&mux);
    if (running) {
      snprintf(err, n, "SD card busy - try again");
      return false;
    }
    xSemaphoreTake(rqDone, portMAX_DELAY);  // it finished just now: its "done" is on the way
  }
  if (!rq.ok) strlcpy(err, rq.err, n);
  return true;  // the caller reads rq, then calls done()
}
static void done() { xSemaphoreGive(rqFree); }

int sdlogList(SdFile* out, int max, uint32_t& totalKb, uint32_t& freeKb, char* err, size_t n, SdDir dir) {
  totalKb = freeKb = 0;
  if (!ask(RQ_LIST, nullptr, err, n, dir)) return -1;
  int count = rq.ok ? rq.count : -1;
  if (count > max) count = max;
  if (count > 0) memcpy(out, rq.files, sizeof(SdFile) * count);
  totalKb = rq.totalKb;
  freeKb = rq.freeKb;
  done();
  return count;
}

static const char* badName(uint8_t dir) { return dir == SD_BACKUPS ? "not a backup file name" : "not a run log name"; }

char* sdlogLoad(const char* name, size_t& len, char* err, size_t n, SdDir dir) {
  len = 0;
  if (!nameOk(name, dir)) {
    snprintf(err, n, "%s", badName(dir));
    return nullptr;
  }
  if (!ask(RQ_LOAD, name, err, n, dir)) return nullptr;
  char* data = rq.data;
  len = rq.len;
  rq.data = nullptr;
  done();
  return data;
}

bool sdlogDelete(const char* name, char* err, size_t n, SdDir dir) {
  if (!nameOk(name, dir)) {
    snprintf(err, n, "%s", badName(dir));
    return false;
  }
  if (dir == SD_RUNS && open_ && !strcmp(file + 5, name)) {  // ("runs/" + name)
    snprintf(err, n, "this run is still being logged");
    return false;
  }
  if (!ask(RQ_DELETE, name, err, n, dir)) return false;
  const bool ok = rq.ok;
  done();
  return ok;
}

bool sdlogBackupWrite(char* data, size_t len, const char* base, char* name, size_t nn, char* err, size_t n) {
  if (nn) name[0] = 0;
  if (!data) return snprintf(err, n, "out of memory"), false;
  if (!ask(RQ_BACKUP, base, err, n, SD_BACKUPS, data, len)) return false;
  const bool ok = rq.ok;
  if (ok) strlcpy(name, rq.name, nn);
  done();
  return ok;
}

bool sdlogFirmwareCheck(SdFirmware& f, char* err, size_t n) {
  memset(&f, 0, sizeof(f));
  if (!ask(RQ_FWCHECK, nullptr, err, n)) return false;
  const bool ok = rq.ok;
  f = rq.fw;
  done();
  return ok;
}

bool sdlogFirmwareInstall(char* err, size_t n) {
  if (!sdTask) return snprintf(err, n, "SD card not available"), false;
  if (!otaCanStart(err, n)) return false;
  fwInstall = true;
  xTaskNotifyGive(sdTask);
  return true;
}

#else  // ------------------------------------------------------------ no SD slot

bool sdlogSlot() { return false; }
void sdlogBegin() {}
void sdlogRunStart(const char*, bool, bool) {}
void sdlogRow(const SdRow&) {}
void sdlogRunEnd() {}
void sdlogEvent(uint8_t, const char*) {}
void sdlogState(SdState& s) {
  memset(&s, 0, sizeof(s));
  s.card = -1;
}
int sdlogList(SdFile*, int, uint32_t& totalKb, uint32_t& freeKb, char* err, size_t n, SdDir) {
  totalKb = freeKb = 0;
  snprintf(err, n, "no SD slot on this board");
  return -1;
}
char* sdlogLoad(const char*, size_t& len, char* err, size_t n, SdDir) {
  len = 0;
  snprintf(err, n, "no SD slot on this board");
  return nullptr;
}
bool sdlogDelete(const char*, char* err, size_t n, SdDir) {
  snprintf(err, n, "no SD slot on this board");
  return false;
}
bool sdlogBackupWrite(char* data, size_t, const char*, char* name, size_t nn, char* err, size_t n) {
  free(data);
  if (nn) name[0] = 0;
  snprintf(err, n, "no SD slot on this board");
  return false;
}
bool sdlogFirmwareCheck(SdFirmware& f, char* err, size_t n) {
  memset(&f, 0, sizeof(f));
  snprintf(err, n, "no SD slot on this board");
  return false;
}
bool sdlogFirmwareInstall(char* err, size_t n) {
  snprintf(err, n, "no SD slot on this board");
  return false;
}
#endif
