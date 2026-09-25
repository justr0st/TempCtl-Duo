// Shared state between the control task (Modbus + program engine), the LVGL
// UI and the web server. Everything in here is protected by gLock.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "program.h"
#include "settings.h"

struct LiveData {
  // link
  bool commOk;
  uint32_t okCount, errCount;
  uint32_t lastOkMs;
  char lastErr[80];
  // process values
  bool pvValid; float pv; uint8_t pvFlag;
  bool spEffValid; float spEff;
  bool outValid; float out;
  bool statusValid; uint16_t status;
  bool alarmValid; uint16_t alarm;
  // program
  RunState state;
  Phase phase;
  int8_t step;
  float phaseEl, phaseDur, elapsed, total;
  bool soakWait;
  float hostSp;       // setpoint computed by the program
  float target;       // current step target
  bool spSynced;      // last setpoint write succeeded
  float lastWrittenSp;
  uint32_t spWrites;  // count of SP writes (EEPROM wear indicator)
  char runMsg[80];
};

// Chart history. mode 0 = rolling live window, 1 = current/last program run.
struct History {
  uint8_t mode;
  float binSec;     // seconds per point
  float total;      // run: planned total seconds
  float startTemp;  // run: start temperature
  Program prog;     // run: program copy (to draw the planned curve)
  float pv[HIST_N];
  float sp[HIST_N]; // effective setpoint (or host setpoint)
};

enum CmdType : uint8_t { CMD_START, CMD_PAUSE, CMD_RESUME, CMD_STOP, CMD_SKIP, CMD_SETTINGS_CHANGED };

enum BusOp : uint8_t { BUS_READ, BUS_WRITE, BUS_FINDSP };

struct BusRequest {
  BusOp op;
  uint8_t fn;        // 0 = default from settings
  uint16_t addr;
  uint16_t count;    // read
  int16_t value;     // write (raw)
  uint16_t from, to; // findsp
  // results
  bool ok;
  char err[80];
  uint16_t n;
  int16_t values[64];     // read values or found addresses (cast)
  float refValue;         // findsp: SP.EF used as reference
};

extern Settings gSettings;
extern LiveData gLive;
extern History gHist;
extern SemaphoreHandle_t gLock;

struct Guard {
  Guard() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(gLock); }
};

void appInit();                  // create locks/queues, load settings
void controlTaskStart();
bool sendCmd(CmdType c);
bool busRequest(BusRequest& r, uint32_t timeoutMs = 20000);  // blocks until done
void saveSettingsAndNotify();    // persist gSettings and tell the control task
float histPlanAt(int i);         // planned setpoint for history point i (run mode)
