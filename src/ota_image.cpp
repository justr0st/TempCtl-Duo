#include "ota_image.h"
#include <stdio.h>
#include <string.h>

static const char KEY[] = "TCDUO-IMAGE|";

void ImageScanner::reset() {
  total_ = 0;
  magicOk_ = false;
  chip_ = -1;
  found_ = false;
  tailN_ = 0;
  board_[0] = version_[0] = 0;
}

void ImageScanner::feed(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n && total_ + i < sizeof(head_); ++i) head_[total_ + i] = p[i];
  const uint32_t before = total_;
  total_ += n;
  if (before < 16 && total_ >= 16) {
    magicOk_ = head_[0] == 0xE9;
    chip_ = head_[12] | (head_[13] << 8);
  }
  if (found_ || !n) return;
  // search in (tail of the previous chunk + this chunk)
  char buf[sizeof(tail_) + 512];
  size_t off = 0;
  while (off < n && !found_) {
    const size_t take = n - off < 512 ? n - off : 512;
    memcpy(buf, tail_, tailN_);
    memcpy(buf + tailN_, p + off, take);
    const size_t len = tailN_ + take;
    scan(buf, len);
    // keep the last ID_MAX * 2 - 1 bytes for the next round (a match may cross the border)
    const size_t keep = len < sizeof(tail_) - 1 ? len : sizeof(tail_) - 1;
    memcpy(tail_, buf + len - keep, keep);
    tailN_ = keep;
    off += take;
  }
}

void ImageScanner::scan(const char* p, size_t n) {
  const size_t k = sizeof(KEY) - 1;
  for (size_t i = 0; i + k <= n; ++i) {
    if (p[i] != 'T' || memcmp(p + i, KEY, k) != 0) continue;
    // "TCDUO-IMAGE|board|version|end": complete only if "|end" follows within ID_MAX
    const char* s = p + i + k;
    const size_t rest = n - i - k;
    const char* bar1 = (const char*)memchr(s, '|', rest < ID_MAX ? rest : ID_MAX);
    if (!bar1) continue;
    const size_t r2 = rest - (bar1 + 1 - s);
    const char* bar2 = (const char*)memchr(bar1 + 1, '|', r2 < ID_MAX ? r2 : ID_MAX);
    if (!bar2 || (size_t)(p + n - bar2) < 4 || memcmp(bar2, "|end", 4) != 0) continue;
    const size_t bl = bar1 - s, vl = bar2 - bar1 - 1;
    if (!bl || bl >= sizeof(board_) || !vl || vl >= sizeof(version_)) continue;
    memcpy(board_, s, bl);
    board_[bl] = 0;
    memcpy(version_, bar1 + 1, vl);
    version_[vl] = 0;
    found_ = true;
    return;
  }
}

const char* imageProblem(const ImageScanner& s, const char* ownBoard, int ownChip) {
  static char why[96];
  if (s.size() < 64 * 1024) return "the file is too small for a firmware image";
  if (!s.headerOk()) return "not an ESP32 firmware image (.bin from .pio/build/<env>/firmware.bin)";
  if (s.chipId() != ownChip)
    return s.chipId() == 9 ? "this firmware is for the ESP32-S3, not for this board"
                           : "this firmware is for another ESP32 chip than this board";
  if (!s.found()) return "not a TempCtl Duo firmware";
  if (strcmp(s.board(), ownBoard) != 0) {
    snprintf(why, sizeof(why), "this firmware is for \"%s\", this device is \"%s\"", s.board(), ownBoard);
    return why;
  }
  return "";
}
