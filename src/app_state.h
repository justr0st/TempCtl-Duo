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
  uint8_t stepCount;
  int8_t runProg;     // index of the running / last program, -1 = none
  char runName[PROG_NAME_LEN];
  float phaseEl, phaseDur, elapsed, total;
  bool soakWait;
  float hostSp;       // setpoint computed by the program
  float target;       // current step target
  bool spSynced;      // last setpoint write succeeded
  float lastWrittenSp;
  uint32_t spWrites;  // count of SP writes (EEPROM wear indicator)
  // manual setpoint
  bool manualOn;
  float manualSp;
  bool manualSynced;  // manual setpoint (and outputs on/off) written
  char runMsg[80];
};

// Chart history.
struct History {
  // Rolling live window: one sample per LIVE_SAMPLE_SEC, newest at the end.
  float livePv[CHART_N], liveSp[CHART_N];
  // Current / last program run: the timeline 0..runSpan in CHART_N bins.
  bool runValid;
  float runBin, runSpan, runTotal, runStart;
  float runPv[CHART_N], runSp[CHART_N];
  Program runProg;
};

// What the chart shows (touch screen and web use the same data).
struct ChartData {
  bool run;             // true: program timeline, false: rolling live window
  bool planActive;      // program running/paused -> solid, else broken line
  bool spActive;        // setpoint driven (program or manual on) -> solid
  float binSec;         // x axis seconds per point
  float span;           // seconds covered by the x axis
  float planSpan;       // seconds covered by the program curve (preview has its own)
  float planTotal;      // program duration
  float elapsed;        // run: program time
  char progName[PROG_NAME_LEN];
  float pv[CHART_N], sp[CHART_N], plan[CHART_N];
};

enum CmdType : uint8_t {
  CMD_START,        // arg = program index (-1 = selected program)
  CMD_PAUSE,
  CMD_RESUME,
  CMD_STOP,
  CMD_SKIP,
  CMD_SETTINGS_CHANGED,
  CMD_MANUAL_ON,
  CMD_MANUAL_OFF,
  CMD_MANUAL_SP,    // value = new manual setpoint
};

struct Cmd {
  CmdType type;
  int16_t arg;
  float value;
};

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
extern History* gHist;       // heap-allocated in appInit()
extern Program* gPrograms;   // [MAX_PROGRAMS], heap-allocated in appInit()
extern SemaphoreHandle_t gLock;
extern volatile uint32_t gProgGen;  // incremented whenever a stored program changes

struct Guard {
  Guard() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(gLock); }
};

void appInit();                  // create locks/queues, load settings and programs
void controlTaskStart();
bool sendCmd(CmdType c, int arg = 0, float value = 0);
bool busRequest(BusRequest& r, uint32_t timeoutMs = 20000);  // blocks until done
void saveSettingsAndNotify();    // persist gSettings and tell the control task (reopens the link)
void saveSettingsQuiet();        // persist gSettings only (display, selection, manual SP)
bool saveProgram(int idx);       // persist gPrograms[idx] (call without holding gLock)
void selectProgram(int idx);     // select + persist
void setManualSp(float v);       // clamp, persist and send to the control task
float runSpanFor(float total);   // x axis length for a program: total + spare
void chartSnapshot(ChartData& d);
