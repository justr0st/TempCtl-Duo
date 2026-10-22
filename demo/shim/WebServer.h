// Demo: no HTTP server (the page's requests come through webApiRequest, as over USB)
#pragma once
#include <Arduino.h>
#include <functional>
#include "WiFi.h"
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_ABORTED;
  String filename, name, type;
  size_t totalSize = 0, currentSize = 0;
  uint8_t buf[16];
};
class WebServer {
 public:
  explicit WebServer(int) {}
  typedef std::function<void()> Fn;
  void on(const char*, HTTPMethod, Fn) {}
  void on(const char*, HTTPMethod, Fn, Fn) {}
  void onNotFound(Fn) {}
  void begin() {}
  void handleClient() {}
  void collectHeaders(const char**, size_t) {}
  bool hasArg(const char*) { return false; }
  String arg(const char*) { return String(); }
  bool hasHeader(const char*) { return false; }
  String header(const char*) { return String(); }
  String hostHeader() { return String(); }
  String uri() { return String(); }
  WiFiClient client() { return WiFiClient(); }
  HTTPUpload& upload() { return up_; }

 private:
  HTTPUpload up_;
};
