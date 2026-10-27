#pragma once
#include <Arduino.h>
void netBegin();
void netLoop();
String netInfo();   // human-readable: mode, SSID, IP
// Access point running and no station connection (status LED: purple)
bool netApActive();
// Re-apply the Wi-Fi settings from gSettings without restarting (joins the
// new network; updates the access point password if the AP is running).
void netApplyWifi();
// true once the station has an IP address after the last netApplyWifi()
bool netConnectedSinceApply();
String netStaIp();  // "" if not connected
// Asynchronous network scan for the touch UI. Pauses a failing station
// connection attempt during the scan (it blocks scanning) and resumes it.
void netScanStart();
bool netScanBusy();
// -1 = still scanning, -2 = no scan / failed, otherwise number of networks.
// Names are written '\n'-separated (unique, strongest first) to out.
// Must be polled until it returns >= -2 != -1, even if the UI moved on.
int netScanResult(char* out, size_t n);
// details of entry i of the last netScanResult() list (auth = wifi_auth_mode_t)
bool netScanDetail(int i, int& rssi, uint8_t& auth, int& channel);
// station disconnect events so far and the reason of the last one (wifi_err_reason_t)
uint32_t netDisconnects(uint8_t* lastReason);
// starts the access point (name TempCtl-Duo-XXXX, password from the settings) next to the station
void netStartAp();
String netApName();  // "" if the access point is not running
// Wi-Fi switched on (setting wifi.enabled). Off: the radio is off - no station, no access point, no scan, no mDNS;
// netApplyWifi() switches it off / on again after the setting changed.
bool netEnabled();
