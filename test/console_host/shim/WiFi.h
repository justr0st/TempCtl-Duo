// Host stand-in for WiFi.h (serial console test)
#pragma once
#include <Arduino.h>
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA2_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK };
enum { WIFI_REASON_AUTH_EXPIRE = 2, WIFI_REASON_MIC_FAILURE = 14, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 15, WIFI_REASON_NO_AP_FOUND = 201,
       WIFI_REASON_AUTH_FAIL = 202, WIFI_REASON_HANDSHAKE_TIMEOUT = 204 };
struct IPAddress { String toString() const { return String("192.168.1.50"); } };
struct WiFiT {
  int st = WL_CONNECTED;
  wifi_mode_t getMode() { return WIFI_STA; }
  int status() { return st; }
  String SSID() { return String("Workshop"); }
  IPAddress localIP() { return {}; }
  IPAddress softAPIP() { return {}; }
  int RSSI() { return -58; }
};
extern WiFiT WiFi;
