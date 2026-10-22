// Demo: included before every source (-include). The firmware asks LittleFS quietly with stat("/littlefs/...")
#pragma once
#include <sys/stat.h>
#ifdef __cplusplus
int demo_stat(const char* path, struct stat* st);
#define stat(p, s) demo_stat(p, s)
#endif
#if defined(__cplusplus) && defined(__wasi__)
#include <sys/time.h>
#include <time.h>
struct timezone;
extern "C" int settimeofday(const struct timeval* tv, const struct timezone* tz);
extern "C" void tzset(void);
#endif
