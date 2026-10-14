// Display glue for LVGL's MIPI panel drivers, see lcd.h.
//
// LVGL's driver (lv_st7796 / lv_ili9341, on lv_lcd_generic_mipi) sends the
// init sequence, the address window and MADCTL for the rotation; this file
// moves the bytes: commands as polling transfers, pixel data as a queued DMA
// transfer whose completion (SPI post-transaction callback) reports
// lv_display_flush_ready(). LVGL renders into the second draw buffer while
// the first one is sent.
#include "lcd.h"
#include "console.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include "config.h"
#include "crumbs.h"

#if defined(LCD_CTRL_ST7796)
#define LCD_CREATE lv_st7796_create
#define LCD_SEND_CMD_LIST lv_st7796_send_cmd_list
#define LCD_SET_INVERT lv_st7796_set_invert
#elif defined(LCD_CTRL_ILI9341)
#define LCD_CREATE lv_ili9341_create
#define LCD_SEND_CMD_LIST lv_ili9341_send_cmd_list
#define LCD_SET_INVERT lv_ili9341_set_invert
#else
#error "board header: no LCD controller (LCD_CTRL_ST7796 / LCD_CTRL_ILI9341)"
#endif

static spi_device_handle_t spi = nullptr;
static spi_bus_config_t busCfg;               // (bus and device set up again after a status read)
static spi_device_interface_config_t devCfg;
static lv_display_t* disp = nullptr;
static int queued = 0;  // DMA colour transfers not yet collected

// Collects finished queued transfers. Polling transfers must not be started
// while queued ones are pending (spi_master rule). LVGL waits for
// lv_display_flush_ready() before it flushes again, so this does not block
// in practice.
static void drainQueue() {
  if (queued <= 0) return;
  const CrumbCode was = crumbGet(CR_LOOP);
  crumb(CR_LOOP, L_LCD_WAIT);
  while (queued > 0) {
    spi_transaction_t* r = nullptr;
    if (spi_device_get_trans_result(spi, &r, portMAX_DELAY) != ESP_OK) break;
    --queued;
  }
  crumb(CR_LOOP, was);
}

static void sendPolling(const uint8_t* data, size_t len, bool dc) {
  if (!len) return;
  gpio_set_level((gpio_num_t)LCD_PIN_DC, dc);
  spi_transaction_t t = {};
  t.length = len * 8;
  if (len <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, data, len);
  } else {
    t.tx_buffer = data;
  }
  spi_device_polling_transmit(spi, &t);
}

static void sendCmd(lv_display_t*, const uint8_t* cmd, size_t cmdSize, const uint8_t* param, size_t paramSize) {
  drainQueue();
  sendPolling(cmd, cmdSize, false);
  sendPolling(param, paramSize, true);
}

// Runs in the SPI interrupt (not in IRAM: the SPI interrupt is not
// registered as IRAM-safe, so it is deferred while the flash cache is off).
static void postCb(spi_transaction_t* t) {
  if (t->user) lv_display_flush_ready((lv_display_t*)t->user);
}

static void sendColor(lv_display_t* d, const uint8_t* cmd, size_t cmdSize, uint8_t* param, size_t paramSize) {
  drainQueue();
  sendPolling(cmd, cmdSize, false);
  // LVGL renders RGB565 little endian, the panel expects the high byte first
  lv_draw_rgb565_swap(param, paramSize / 2);
  gpio_set_level((gpio_num_t)LCD_PIN_DC, 1);
  static spi_transaction_t t;  // must stay valid until collected
  t = {};
  t.length = paramSize * 8;
  t.tx_buffer = param;
  t.user = d;
  if (spi_device_queue_trans(spi, &t, portMAX_DELAY) == ESP_OK) ++queued;
  else lv_display_flush_ready(d);  // don't hang LVGL
}

#if defined(LCD_CTRL_ST7796)
// LVGL's ST7796 init sets SS (source scan direction) in Display Function
// Control; TFT_eSPI and the lcdwiki demo leave it off. Use the TFT_eSPI value
// so that MADCTL has the usual meaning (landscape = MV | BGR).
static const uint8_t PANEL_FIXUP[] = {
    0xF0, 1, 0xC3,              // enable extension command 2 part I
    0xF0, 1, 0x96,              // part II
    0xB6, 3, 0x80, 0x02, 0x3B,  // Display Function Control: SS = 0
    0xF0, 1, 0x3C,              // disable extension command 2
    0xF0, 1, 0x69,
    LV_LCD_CMD_DELAY_MS, LV_LCD_CMD_EOF,
};
#elif defined(LCD_CTRL_ILI9341)
// The CYD ran with TFT_eSPI's ILI9341_2_DRIVER (alternative init, TFT_eSPI
// issue #1172): same power, VCOM, gamma and Display Function Control values
// (LVGL's init sets SS = 1, which mirrors the picture).
static const uint8_t PANEL_FIXUP[] = {
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x10,              // power control 1
    0xC1, 1, 0x00,              // power control 2
    0xC5, 2, 0x30, 0x30,        // VCOM control 1
    0xC7, 1, 0xB7,              // VCOM control 2
    0xB1, 2, 0x00, 0x1A,        // frame rate
    0xB6, 3, 0x08, 0x82, 0x27,  // Display Function Control: SS = 0
    0xF2, 1, 0x00,              // 3 gamma off
    0x26, 1, 0x01,              // gamma curve 1
    0xE0, 15, 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0xE1, 15, 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F,
    LV_LCD_CMD_DELAY_MS, LV_LCD_CMD_EOF,
};
#endif

static void backlightInit() {
  ledcSetup(0, LEDC_PWM_HZ, 8);
  ledcAttachPin(PIN_BACKLIGHT, 0);
  ledcWrite(0, 0);  // dark until the first frame is drawn
}

void lcdSetBrightness(uint8_t b) { ledcWrite(0, b); }

void lcdSetRotation(bool rotate180) {
  if (!disp) return;
  // With the flag LV_LCD_FLAG_MIRROR_Y (lcdInit) the driver sends
  // MADCTL = MV (+BGR) for 90 degrees and MV | MX | MY for 270 degrees,
  // TFT_eSPI's rotations 1 and 3.
  lv_display_set_rotation(disp, rotate180 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90);
}

static void resetPulse() {
  if (LCD_PIN_RST < 0) return;  // (CYD: no reset pin; the init sequence starts with a software reset)
  pinMode(LCD_PIN_RST, OUTPUT);
  digitalWrite(LCD_PIN_RST, HIGH);
  delay(5);
  digitalWrite(LCD_PIN_RST, LOW);
  delay(20);
  digitalWrite(LCD_PIN_RST, HIGH);
  delay(150);
}

static lv_lcd_flag_t panelFlags() {
  lv_lcd_flag_t flags = LV_LCD_FLAG_MIRROR_Y;
  if (LCD_BGR) flags |= LV_LCD_FLAG_BGR;
  return flags;
}

lv_display_t* lcdInit(bool rotate180) {
  backlightInit();
#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);  // keep the SD card off the bus
  digitalWrite(PIN_SD_CS, HIGH);
#endif
  pinMode(LCD_PIN_DC, OUTPUT);
  digitalWrite(LCD_PIN_DC, HIGH);
  resetPulse();

  const size_t bufBytes = (size_t)SCREEN_W * LCD_BUF_LINES * 2;
  spi_bus_config_t& bus = busCfg;
  bus = {};
  bus.mosi_io_num = LCD_PIN_MOSI;
  bus.miso_io_num = LCD_PIN_MISO;
  bus.sclk_io_num = LCD_PIN_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = bufBytes;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    LogSerial.printf("[lcd] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
    return nullptr;
  }
  spi_device_interface_config_t& dev = devCfg;
  dev = {};
  dev.mode = 0;
  dev.clock_speed_hz = LCD_SPI_HZ;
  dev.spics_io_num = LCD_PIN_CS;
  dev.queue_size = 2;
  dev.flags = SPI_DEVICE_NO_DUMMY;
  dev.post_cb = postCb;
  err = spi_bus_add_device(SPI2_HOST, &dev, &spi);
  if (err != ESP_OK) {
    LogSerial.printf("[lcd] spi_bus_add_device failed: %s\n", esp_err_to_name(err));
    return nullptr;
  }

#ifdef BOARD_HAS_PSRAM
#define LCD_DOUBLE_BUF 1
#else
#define LCD_DOUBLE_BUF 0
#endif
  // draw buffers in internal DMA-capable RAM: two (LVGL renders while the last part is sent);
  // one on boards without PSRAM (CYD: DRAM is short)
  void* buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#ifdef BOARD_HAS_PSRAM
  void* buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#else
  void* buf2 = nullptr;
#endif
  if (!buf1 || (!buf2 && LCD_DOUBLE_BUF)) {
    LogSerial.printf("[lcd] no RAM for the draw buffers (2 x %u bytes)\n", (unsigned)bufBytes);
    return nullptr;
  }

  disp = LCD_CREATE(LCD_NATIVE_W, LCD_NATIVE_H, panelFlags(), sendCmd, sendColor);
  if (!disp) return nullptr;
  LCD_SEND_CMD_LIST(disp, PANEL_FIXUP);
  LCD_SET_INVERT(disp, LCD_INVERT != 0);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lcdSetRotation(rotate180);
  LogSerial.printf("[lcd] %dx%d, SPI %d MHz, %d x %u bytes draw buffer\n", (int)lv_display_get_horizontal_resolution(disp),
                (int)lv_display_get_vertical_resolution(disp), LCD_SPI_HZ / 1000000, LCD_DOUBLE_BUF ? 2 : 1, (unsigned)bufBytes);
  char info[128];
  lcdCheck(info, sizeof(info));  // the first read decides whether the status can be read back at all
  LogSerial.printf("[lcd] status: %s\n", info);
  return disp;
}

// The panel controller set up again from scratch (hardware reset where wired, then LVGL's whole init
// sequence): for a display that lost its setup (interference, a loose cable). LVGL keeps the display
// object, the screens and the draw buffers; the init sequence is sent by a short-lived second LVGL
// display on the same SPI device (LVGL's driver has no separate "init again"), then the rotation
// (MADCTL) is sent for the real one and everything is drawn again.
bool lcdReinit() {
  if (!disp) return false;
  drainQueue();  // a flush in progress finishes first
  resetPulse();
  lv_display_t* tmp = LCD_CREATE(LCD_NATIVE_W, LCD_NATIVE_H, panelFlags(), sendCmd, sendColor);
  if (!tmp) {
    LogSerial.println("[lcd] restart: no RAM for the init");
    return false;
  }
  LCD_SEND_CMD_LIST(tmp, PANEL_FIXUP);
  LCD_SET_INVERT(tmp, LCD_INVERT != 0);
  lv_display_delete(tmp);
  lv_display_set_rotation(disp, lv_display_get_rotation(disp));  // MADCTL again, all redrawn
  lv_obj_invalidate(lv_display_get_screen_active(disp));
  lv_obj_invalidate(lv_display_get_layer_top(disp));
  LogSerial.println("[lcd] display controller set up again");
  return true;
}

// ------------------------------------------------------------ status read-back (automatic screen check)
// MIPI read commands: 0Ah power mode (bit 4 sleep out, bit 2 display on), 0Bh MADCTL, 0Ch pixel format
// (bits 2..0 = 5: 16 bit). A panel that lost its setup (reset by interference) is asleep / off or back
// at its default MADCTL.
// Read in software, slowly (~100 kHz): the SPI hardware sampled each bit while the line already
// changed to the next one (S3 / ST7796 read value | value << 1: 0x28 -> 0x78). For the read the SPI bus
// is released (device removed, bus freed), the pins driven as GPIOs, then bus and device set up again.
// The sampling point (0: late in the clock's high phase, 1: late in its low phase) is chosen right
// after the init: the one that reads the known setup back. ILI9341: index register D9h = 10h first
// (as TFT_eSPI's readcommand8).
static int8_t readable = -1;  // -1 not tried yet, 0 the status cannot be read back, 1 it can
static uint8_t sampleAt = 0;
// The values right after the init: later reads must match them (0Ah all mode bits incl. the booster,
// 0Ch, 0Dh image mode: inversion, all pixels on / off, gamma), MADCTL the driver's current value.
static constexpr int NREG = 4;
static uint8_t base[NREG];

#if LCD_PIN_MISO >= 0
static inline void half() { delayMicroseconds(4); }
static void bbWrite(uint8_t v) {  // mode 0: data set while SCK is low, the panel takes it on the rising edge
  for (int i = 7; i >= 0; --i) {
    digitalWrite(LCD_PIN_MOSI, (v >> i) & 1);
    half();
    digitalWrite(LCD_PIN_SCK, HIGH);
    half();
    digitalWrite(LCD_PIN_SCK, LOW);
  }
}
static uint8_t bbRead(uint8_t at) {
  uint8_t v = 0;
  for (int i = 0; i < 8; ++i) {
    half();
    if (at == 1) v = (v << 1) | (digitalRead(LCD_PIN_MISO) ? 1 : 0);  // end of the low phase
    digitalWrite(LCD_PIN_SCK, HIGH);
    half();
    if (at == 0) v = (v << 1) | (digitalRead(LCD_PIN_MISO) ? 1 : 0);  // end of the high phase
    digitalWrite(LCD_PIN_SCK, LOW);
  }
  return v;
}
static uint8_t bbReg(uint8_t cmd, uint8_t at) {
#if defined(LCD_CTRL_ILI9341)
  digitalWrite(LCD_PIN_CS, LOW);
  digitalWrite(LCD_PIN_DC, LOW);
  bbWrite(0xD9);
  digitalWrite(LCD_PIN_DC, HIGH);
  bbWrite(0x10);
  digitalWrite(LCD_PIN_CS, HIGH);
  half();
#endif
  digitalWrite(LCD_PIN_CS, LOW);
  digitalWrite(LCD_PIN_DC, LOW);
  bbWrite(cmd);
  digitalWrite(LCD_PIN_DC, HIGH);
  const uint8_t v = bbRead(at);
  digitalWrite(LCD_PIN_CS, HIGH);
  half();
  return v;
}
#endif

static SemaphoreHandle_t busMutex() {
  static StaticSemaphore_t buf;
  static SemaphoreHandle_t m = xSemaphoreCreateMutexStatic(&buf);  // (C++ static: created once, thread-safe)
  return m;
}
bool lcdBusLock(uint32_t waitMs) { return xSemaphoreTake(busMutex(), pdMS_TO_TICKS(waitMs)) == pdTRUE; }
void lcdBusUnlock() { xSemaphoreGive(busMutex()); }

// at: sampling point; several sets of reads (n x NREG values) in one bus release.
// 1: read, 0: SPI error, -1: busy (the SD card uses the bus)
static int readRegs(uint8_t* out, const uint8_t* ats, int sets) {
#if LCD_PIN_MISO < 0
  return 0;
#else
  static const uint8_t CMDS[NREG] = {0x0A, 0x0B, 0x0C, 0x0D};
  if (!lcdBusLock(0)) return -1;
  struct Unlock {
    ~Unlock() { lcdBusUnlock(); }
  } unlock;
  drainQueue();
  if (spi_bus_remove_device(spi) != ESP_OK) return 0;
  spi = nullptr;
  if (spi_bus_free(SPI2_HOST) != ESP_OK) {  // another device is still on the bus: no read, display stays as it is
    if (spi_bus_add_device(SPI2_HOST, &devCfg, &spi) != ESP_OK) {
      spi = nullptr;
      LogSerial.println("[lcd] SPI device could not be added again");  // (should not happen)
      return 0;
    }
    return -1;
  }
  pinMode(LCD_PIN_CS, OUTPUT);
  digitalWrite(LCD_PIN_CS, HIGH);
  pinMode(LCD_PIN_SCK, OUTPUT);
  digitalWrite(LCD_PIN_SCK, LOW);
  pinMode(LCD_PIN_MOSI, OUTPUT);
  pinMode(LCD_PIN_MISO, INPUT);
  for (int k = 0; k < sets; ++k)
    for (int i = 0; i < NREG; ++i) out[k * NREG + i] = bbReg(CMDS[i], ats[k]);
  digitalWrite(LCD_PIN_DC, HIGH);
  bool ok = spi_bus_initialize(SPI2_HOST, &busCfg, SPI_DMA_CH_AUTO) == ESP_OK &&
            spi_bus_add_device(SPI2_HOST, &devCfg, &spi) == ESP_OK;
  if (!ok) {
    LogSerial.println("[lcd] SPI could not be set up again after the status read");  // (should not happen)
    spi = nullptr;
  }
  return ok ? 1 : 0;
#endif
}

// the values as set up? (base: also equal to the values read right after the init)
static bool asSetUp(const uint8_t* v, uint8_t want, const uint8_t* b) {
  const uint8_t M = 0xE8;  // MY MX MV BGR (the others may read 0)
  if ((v[0] & 0x14) != 0x14 || (v[1] & M) != (want & M) || (v[2] & 0x07) != 5) return false;
  return !b || ((v[0] & 0xFC) == (b[0] & 0xFC) && (v[2] & 0x77) == (b[2] & 0x77) && (v[3] & 0x3F) == (b[3] & 0x3F));
}

int lcdCheck(char* info, size_t n) {
  if (!disp || !spi) {
    snprintf(info, n, "no display");
    return -1;
  }
  if (readable == 0) {
    snprintf(info, n, "not readable, check off");
    return -1;
  }
  const lv_lcd_generic_mipi_driver_t* drv = (const lv_lcd_generic_mipi_driver_t*)lv_display_get_driver_data(disp);
  const uint8_t want = drv ? drv->madctl_reg : 0;
  uint8_t v[2 * NREG] = {0};
  if (readable < 0) {  // right after the init: try both sampling points, keep the one that reads back the setup
    static const uint8_t ATS[2] = {0, 1};
    const int r = readRegs(v, ATS, 2);
    if (r < 0) {
      snprintf(info, n, "busy (SD card), tried again");
      return -1;
    }
    if (!r) {
      snprintf(info, n, "not readable (SPI), check off");
      readable = 0;
      return -1;
    }
    for (int k = 0; k < 2 && readable < 0; ++k)
      if (asSetUp(v + k * NREG, want, nullptr)) {
        readable = 1;
        sampleAt = k;
        memcpy(base, v + k * NREG, NREG);
      }
    if (readable < 0) {
      readable = 0;
      snprintf(info, n, "read %02X %02X %02X %02X / %02X %02X %02X %02X (MADCTL %02X): not readable, check off", v[0],
               v[1], v[2], v[3], v[4], v[5], v[6], v[7], want);  // (both sampling points, for the diagnosis)
      return -1;
    }
    snprintf(info, n, "power %02X, MADCTL %02X, format %02X, mode %02X: OK (sample %d), check on", base[0], base[1],
             base[2], base[3], sampleAt);
    return 1;
  }
  const uint8_t at = sampleAt;
  const int r = readRegs(v, &at, 1);
  if (r <= 0) {
    snprintf(info, n, r < 0 ? "busy (SD card)" : "SPI error");
    return -1;
  }
  const bool ok = asSetUp(v, want, base);
  // registers 0A (power), 0B (MADCTL), 0C (format), 0D (image mode); short: it goes into event texts
  if (ok) snprintf(info, n, "regs %02X %02X %02X %02X", v[0], v[1], v[2], v[3]);
  else
    snprintf(info, n, "regs %02X %02X %02X %02X, set %02X %02X %02X %02X", v[0], v[1], v[2], v[3], base[0], want, base[2],
             base[3]);
  return ok ? 1 : 0;
}

// Test of the screen check (hidden serial command "screen check test"): display off + sleep in, as
// after a reset by interference. False without a display.
bool lcdTestBreak() {
  if (!disp || !spi) return false;
  static const uint8_t OFF = 0x28, SLEEP = 0x10;
  sendCmd(disp, &OFF, 1, nullptr, 0);
  sendCmd(disp, &SLEEP, 1, nullptr, 0);
  LogSerial.println("[lcd] test: display switched off and put to sleep");
  return true;
}
