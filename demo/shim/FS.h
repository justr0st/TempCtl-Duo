// Demo: the flash file system in memory (demo/sim.cpp keeps it; the page stores it in the browser)
#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <vector>
namespace fs {
struct DemoNode;
class File : public Stream {
 public:
  File() {}
  File(std::shared_ptr<std::vector<uint8_t>> d, bool w, bool app, const String& path) : d_(d), w_(w), path_(path) { if (app) pos_ = d->size(); }
  operator bool() const { return (bool)d_; }
  int available() override { return d_ && !w_ ? (int)(d_->size() - pos_) : 0; }
  int read() override { return d_ && pos_ < d_->size() ? (*d_)[pos_++] : -1; }
  int peek() override { return d_ && pos_ < d_->size() ? (*d_)[pos_] : -1; }
  size_t read(uint8_t* b, size_t n) { if (!d_) return 0; size_t k = std::min(n, d_->size() - pos_); memcpy(b, d_->data() + pos_, k); pos_ += k; return k; }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) override {
    if (!d_ || !w_) return 0;
    if (pos_ + n > d_->size()) d_->resize(pos_ + n);
    memcpy(d_->data() + pos_, b, n);
    pos_ += n;
    return n;
  }
  using Print::write;
  bool seek(size_t p) { if (!d_ || p > d_->size()) return false; pos_ = p; return true; }
  size_t position() const { return pos_; }
  size_t size() const { return d_ ? d_->size() : 0; }
  void close() { d_.reset(); }
  void flush() override {}
  const char* path() const { return path_.c_str(); }
  const char* name() const { const char* p = strrchr(path_.c_str(), '/'); return p ? p + 1 : path_.c_str(); }
  bool isDirectory() const { return false; }
  File openNextFile() { return File(); }

 private:
  std::shared_ptr<std::vector<uint8_t>> d_;
  bool w_ = false;
  size_t pos_ = 0;
  String path_;
};
class FS {
 public:
  bool begin(bool = false, const char* = "/littlefs", uint8_t = 10, const char* = nullptr) { return true; }
  void end() {}
  bool format() { files.clear(); return true; }
  File open(const char* path, const char* mode = "r", bool create = false);
  File open(const String& path, const char* mode = "r", bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path) { return files.count(path) || dirs.count(path); }
  bool exists(const String& p) { return exists(p.c_str()); }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool remove(const String& p) { return remove(p.c_str()); }
  bool rename(const char* a, const char* b);
  bool rename(const String& a, const String& b) { return rename(a.c_str(), b.c_str()); }
  bool mkdir(const char* p) { dirs[p] = 1; return true; }
  bool rmdir(const char* p) { return dirs.erase(p) > 0; }
  size_t totalBytes() { return 1408 * 1024; }
  size_t usedBytes();
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  std::map<std::string, int> dirs;
};
}  // namespace fs
using fs::File;
using fs::FS;
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
