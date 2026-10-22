// Demo: no Wi-Fi (shown as connected to a simulated network)
#pragma once
#include <Arduino.h>
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE,
       WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK };
struct IPAddress {
  uint8_t a[4] = {192, 168, 1, 50};
  String toString() const { char b[16]; snprintf(b, sizeof(b), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]); return String(b); }
  uint8_t operator[](int i) const { return a[i]; }
};
class WiFiClient {
 public:
  int fd() { return -1; }
  void stop() {}
  bool connected() { return false; }
  operator bool() { return false; }
};
struct WiFiClass {
  wifi_mode_t getMode() { return WIFI_STA; }
  int status() { return WL_CONNECTED; }
  String SSID() { return String("Demo network"); }
  IPAddress localIP() { return {}; }
  IPAddress softAPIP() { return {}; }
  int RSSI() { return -55; }
  const char* getHostname() { return "tempctl-duo-demo"; }
  String macAddress() { return String("DE:30:A0:D0:00:01"); }
};
extern WiFiClass WiFi;
