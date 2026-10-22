// Build configuration: version, board (pins, display, touch) and shared sizes.
#pragma once

#include "version.h"

#if defined(BOARD_S3)
#include "board_s3.h"
#elif defined(BOARD_CYD)
#include "board_cyd.h"
#elif defined(BOARD_DEMO)
#include "board_demo.h"  // the browser demo (demo/, its own include path)
#else
#error "No board selected: build with -D BOARD_S3 or -D BOARD_CYD (platformio.ini envs s3, cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485)"
#endif

// SD card (run logs, firmware update, backups): S3 only (the CYD's SD pins carry the thermocouples)
#if defined(PIN_SD_CS) && defined(BOARD_HAS_PSRAM)
#define HAS_SD 1
#else
#define HAS_SD 0
#endif

// firmware download from the release page over HTTPS (spec 9.13): S3 only (a TLS session needs ~70 kB RAM,
// taken from PSRAM; the CYD has none - its web page installs releases through the browser)
#if defined(BOARD_HAS_PSRAM) && !defined(BOARD_DEMO)
#define HAS_NET_OTA 1
#else
#define HAS_NET_OTA 0
#endif

// Backlight PWM (LEDC channel 0, 8 bit). 20 kHz (was 5 kHz until 1.0.46): inaudible, fewer touch gaps on the S3's
// capacitive panel at part brightness. The CYD's RGB status LED (channels 1..3) shares channel 0's timer, so it
// runs at the same frequency.
#define LEDC_PWM_HZ 20000

// Chart resolution (points). Live: the chosen period (display.liveMin) in CHART_N
// bins of the live history. Running: the program timeline (plus some spare time)
// is divided into CHART_N bins.
#define CHART_N 300
// The touch chart's copy also keeps the lowest / highest PV of each point (peaks): with PSRAM only
// (2.4 kB more; the CYD's internal RAM is short)
#ifdef BOARD_HAS_PSRAM
#define CHART_LOHI 1
#endif
#define LIVE_SAMPLE_SEC 1
// Live history in RAM (lost on restart): one slot per LIVE_SAMPLE_SEC with the last,
// lowest and highest reading and the setpoint of both channels, 12 bytes.
// PSRAM: 24 h = 1 MB. CYD: the IRAM heap, as much as fits up to 1 h (see appInit).
#ifdef BOARD_HAS_PSRAM
#define LIVE_HIST_MIN 1440
#else
#define LIVE_HIST_MIN 60
#endif
// Every PV reading for the web chart (raw_hist.h): 8 bytes each. S3: 2 MB of PSRAM = 131 072 per channel
// (36 h at 1 s polling, 9 h at 250 ms); CYD: 600 per channel (10 min at 1 s) in the IRAM heap.
#define RAW_HIST_BYTES (2u * 1024u * 1024u)
#define RAW_HIST_CYD 600
