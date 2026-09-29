// Build configuration: version, board (pins, display, touch) and shared sizes.
#pragma once

#include "version.h"

#if defined(BOARD_S3)
#include "board_s3.h"
#elif defined(BOARD_CYD)
#include "board_cyd.h"
#else
#error "No board selected: build with -D BOARD_S3 or -D BOARD_CYD (platformio.ini envs s3, cyd-1xlocal-1x485, cyd-2xlocal, cyd-2xlocal-2x485)"
#endif

// Chart resolution (points). Idle: one sample per second -> 300 s = 5 min
// rolling window. Running: the program timeline (plus some spare time) is
// divided into CHART_N bins.
#define CHART_N 300
#define LIVE_SAMPLE_SEC 1
