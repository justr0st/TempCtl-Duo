// Firmware image check (spec 9.11), pure C++ (host-tested). Every TempCtl Duo image carries an id string
//   "TCDUO-IMAGE|<board id>|<version>|end"   (ota.cpp: kept in the image's read-only data)
// The scanner is fed the image as it streams in (web upload, SD card) and finds it across chunk
// boundaries; it also reads the chip id from the ESP image header (0 = ESP32, 9 = ESP32-S3).
#pragma once
#include <stddef.h>
#include <stdint.h>

class ImageScanner {
 public:
  void reset();
  void feed(const uint8_t* p, size_t n);
  bool headerOk() const { return magicOk_; }   // first byte 0xE9 (ESP image)
  int chipId() const { return chip_; }         // -1 until 16 bytes were seen
  bool found() const { return found_; }
  const char* board() const { return board_; }
  const char* version() const { return version_; }
  uint32_t size() const { return total_; }
  void setSize(uint32_t n) { total_ = n; }  // the file's size, when only its start was fed

 private:
  static constexpr size_t ID_MAX = 80;
  uint8_t head_[16];
  uint32_t total_ = 0;
  bool magicOk_ = false;
  int chip_ = -1;
  bool found_ = false;
  char tail_[ID_MAX * 2];  // last bytes of the previous chunk (a match may start there)
  size_t tailN_ = 0;
  char board_[32] = "";
  char version_[16] = "";
  void scan(const char* p, size_t n);
};

// What the image would do on this device: "" = fine, else the reason it is refused
// (board id / chip of this firmware: ownBoard, ownChip)
const char* imageProblem(const ImageScanner& s, const char* ownBoard, int ownChip);
