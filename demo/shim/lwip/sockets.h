#pragma once
#include <errno.h>
#include <stddef.h>
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
// (the demo has no sockets: the web page's requests come through webApiRequest)
inline int send(int, const void*, size_t n, int) { return (int)n; }
