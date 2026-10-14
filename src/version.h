// TempCtl Duo identity and version: one place for the firmware, the web page
// and (later) the local app. The patch number goes up with every commit that
// is pushed (1.0.0 -> 1.0.1 -> ...); minor / major only when decided.
#pragma once

#define PRODUCT_NAME "TempCtl Duo"
#define FW_VERSION "1.0.44"

// Host name (mDNS <host>.local) and prefix of the access point name
// (<AP_NAME_PREFIX>-XXXX, last MAC digits)
#define DEFAULT_HOSTNAME "tempctl-duo"
#define AP_NAME_PREFIX "TempCtl-Duo"

// Serial / HTTP API version: a separate integer, raised only when the API
// changes incompatibly.
#define API_VERSION 1
