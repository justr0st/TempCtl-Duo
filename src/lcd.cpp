// Display glue for LVGL's MIPI panel drivers, see lcd.h.
//
// LVGL's driver (lv_st7796 / lv_ili9341, on lv_lcd_generic_mipi) sends the
// init sequence, the address window and MADCTL for the rotation; this file
// moves the bytes: commands as polling transfers, pixel data as a queued DMA
// transfer whose completion (SPI post-transaction callback) reports
// lv_display_flush_ready(). LVGL renders into the second draw buffer while
// the first one is sent.
#include "lcd.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "config.h"

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
static lv_display_t* disp = nullptr;
static int queued = 0;  // DMA colour transfers not yet collected

// Collects finished queued transfers. Polling transfers must not be started
// while queued ones are pending (spi_master rule). LVGL waits for
// lv_display_flush_ready() before it flushes again, so this does not block
// in practice.
static void drainQueue() {
  while (queued > 0) {
    spi_transaction_t* r = nullptr;
    if (spi_device_get_trans_result(spi, &r, portMAX_DELAY) != ESP_OK) break;
    --queued;
  }
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
  ledcSetup(0, 5000, 8);
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

lv_display_t* lcdInit(bool rotate180) {
  backlightInit();
#ifdef PIN_SD_CS
  pinMode(PIN_SD_CS, OUTPUT);  // keep the SD card off the bus
  digitalWrite(PIN_SD_CS, HIGH);
#endif
  pinMode(LCD_PIN_DC, OUTPUT);
  digitalWrite(LCD_PIN_DC, HIGH);
  if (LCD_PIN_RST >= 0) {  // hardware reset
    pinMode(LCD_PIN_RST, OUTPUT);
    digitalWrite(LCD_PIN_RST, HIGH);
    delay(5);
    digitalWrite(LCD_PIN_RST, LOW);
    delay(20);
    digitalWrite(LCD_PIN_RST, HIGH);
    delay(150);
  }

  const size_t bufBytes = (size_t)SCREEN_W * LCD_BUF_LINES * 2;
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LCD_PIN_MOSI;
  bus.miso_io_num = LCD_PIN_MISO;
  bus.sclk_io_num = LCD_PIN_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = bufBytes;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    Serial.printf("[lcd] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
    return nullptr;
  }
  spi_device_interface_config_t dev = {};
  dev.mode = 0;
  dev.clock_speed_hz = LCD_SPI_HZ;
  dev.spics_io_num = LCD_PIN_CS;
  dev.queue_size = 2;
  dev.flags = SPI_DEVICE_NO_DUMMY;
  dev.post_cb = postCb;
  err = spi_bus_add_device(SPI2_HOST, &dev, &spi);
  if (err != ESP_OK) {
    Serial.printf("[lcd] spi_bus_add_device failed: %s\n", esp_err_to_name(err));
    return nullptr;
  }

  // two draw buffers in internal DMA-capable RAM
  void* buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf1 || !buf2) {
    Serial.printf("[lcd] no RAM for the draw buffers (2 x %u bytes)\n", (unsigned)bufBytes);
    return nullptr;
  }

  lv_lcd_flag_t flags = LV_LCD_FLAG_MIRROR_Y;
  if (LCD_BGR) flags |= LV_LCD_FLAG_BGR;
  disp = LCD_CREATE(LCD_NATIVE_W, LCD_NATIVE_H, flags, sendCmd, sendColor);
  if (!disp) return nullptr;
  LCD_SEND_CMD_LIST(disp, PANEL_FIXUP);
  LCD_SET_INVERT(disp, LCD_INVERT != 0);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lcdSetRotation(rotate180);
  Serial.printf("[lcd] %dx%d, SPI %d MHz, 2 x %u bytes draw buffer\n", (int)lv_display_get_horizontal_resolution(disp),
                (int)lv_display_get_vertical_resolution(disp), LCD_SPI_HZ / 1000000, (unsigned)bufBytes);
  return disp;
}
