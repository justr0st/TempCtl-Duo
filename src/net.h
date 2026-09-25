#pragma once
#include <Arduino.h>
void netBegin();
void netLoop();
String netInfo();   // human-readable: mode, SSID, IP
// Re-apply the Wi-Fi settings from gSettings without restarting (joins the
// new network; updates the access point password if the AP is running).
void netApplyWifi();
// Asynchronous network scan for the touch UI.
void netScanStart();
// -1 = still scanning, -2 = no scan / failed, otherwise number of networks.
// Names are written '\n'-separated (unique, strongest first) to out.
int netScanResult(char* out, size_t n);
