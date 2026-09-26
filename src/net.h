#pragma once
#include <Arduino.h>
void netBegin();
void netLoop();
String netInfo();   // human-readable: mode, SSID, IP
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
