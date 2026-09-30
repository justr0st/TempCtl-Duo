// Shared state between the control task (channels + run engine), the LVGL
// UI and the web server. Everything in here is protected by gLock.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "channel.h"
#include "program.h"
#include "program_store.h"
#include "run_engine.h"
#include "settings.h"

// The old touch / web UIs (until steps 6 and 7) show the bottom channel
// (ENGINE_CH); the flat fields below mirror it. Both channels are in `ch`,
// the run in the run fields.
constexpr int ENGINE_CH = CH_BOTTOM;

struct LiveData {
  ChannelLive ch[NUM_CHANNELS];
  // ---- mirror of ch[ENGINE_CH] (+ its setpoint-write errors) ----
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
  // manual setpoint
  bool manualOn;
  float manualSp;
  bool manualSynced;  // manual setpoint (and outputs on/off) written

  // ---- run (both channels) ----
  RunState state;
  uint8_t progType;   // PROG_SEQ / PROG_TL of the running / last program
  int8_t runProg;     // slot of the running / last program, -1 = none
  char runName[PROG_NAME_LEN];
  StageInfo seq;                    // Sequence: phase + stage
  StageInfo lane[NUM_CHANNELS];     // Timeline: step + stage per channel
  uint16_t phases;                  // Sequence: number of phases
  uint16_t steps[NUM_CHANNELS];     // Timeline: steps per channel
  bool waiting;       // waiting for the bottom to reach its target
  bool progPause;     // Pause phase / step: waiting for Continue
  bool pauseMsg;      // ... with "Action required"
  char pauseText[PROG_TEXT_LEN];
  bool runEdited;     // the running copy has unsaved run edits
  float elapsed;      // run time (s)
  float remaining;    // forecast (s), waits and pauses not counted
  ChOut run[NUM_CHANNELS];          // what the run does with each channel
  bool spSyncedCh[NUM_CHANNELS];    // last setpoint write succeeded
  uint32_t spWritesCh[NUM_CHANNELS];

  // ---- legacy run view for the old UIs (bottom channel / Sequence phase) ----
  Stage phase;
  int16_t step;
  uint16_t stepCount;
  float phaseEl, phaseDur, total;
  bool soakWait;
  float hostSp;       // run setpoint of the bottom channel
  float target;
  bool spSynced;
  float lastWrittenSp;
  uint32_t spWrites;  // count of SP writes (EEPROM wear indicator)
  char runMsg[128];
};

// Chart history, both channels.
struct History {
  // Rolling live window: one sample per LIVE_SAMPLE_SEC, newest at the end.
  float livePv[NUM_CHANNELS][CHART_N], liveSp[NUM_CHANNELS][CHART_N];
  // Current / last program run: run time 0..runSpan in CHART_N bins (the
  // span doubles when the run gets longer than planned).
  bool runValid;
  float runBin, runSpan;
  float runPv[NUM_CHANNELS][CHART_N], runSp[NUM_CHANNELS][CHART_N];
  // run's program curve: its setpoints so far + the forecast (kept after the run for the CSV)
  float runPlan[NUM_CHANNELS][CHART_N];
  float runTotal;     // elapsed + remaining
  char runName[PROG_NAME_LEN];
  // preview of the selected program on its own time axis (no run active)
  float plan[NUM_CHANNELS][CHART_N];
  float planBin, planSpan, planTotal;
  char planName[PROG_NAME_LEN];
};

// What the chart shows (touch screen and web use the same data).
struct ChartData {
  bool run;             // true: program timeline, false: rolling live window
  bool planActive;      // program running/paused -> solid, else broken line
  bool spActive;        // setpoint driven (program or manual on) -> solid
  float binSec;         // x axis seconds per point
  float span;           // seconds covered by the x axis
  float planSpan;       // seconds covered by the program curve (preview has its own)
  float planTotal;      // program duration (run: elapsed + remaining)
  float elapsed;        // run: program time
  char progName[PROG_NAME_LEN];
  float pv[CHART_N], sp[CHART_N], plan[CHART_N];     // bottom (ENGINE_CH)
  float pvT[CHART_N], spT[CHART_N], planT[CHART_N];  // top
};

enum CmdType : uint8_t {
  CMD_START,        // arg = program slot (-1 = selected program)
  CMD_PAUSE,
  CMD_RESUME,
  CMD_STOP,
  CMD_NEXT,         // "Next phase" (Sequence); Continue in a program pause
  CMD_CONTINUE,     // Continue after a Pause phase / step
  CMD_SETTINGS_CHANGED,
  CMD_MANUAL_ON,    // arg = channel
  CMD_MANUAL_OFF,   // arg = channel
  CMD_MANUAL_SP,    // arg = channel, value = new manual setpoint
  CMD_ACK_FAULT,    // arg = channel: acknowledge a latched fault
  CMD_TUNE_START,   // arg = channel (local), value = test temperature
  CMD_TUNE_STOP,    // arg = channel
  CMD_RUN_EDIT,     // sub = RunEdit, arg = channel, idx = phase/step (-1 current), value
  CMD_SAVE_RUN,     // store the running copy (with its run edits) into its slot
};

enum RunEdit : uint8_t {
  EDIT_HOLD,        // value = hold seconds
  EDIT_EXTEND,      // value = seconds added to the current hold (may be negative)
  EDIT_RAMP,        // value = ramp seconds (later ramps only)
  EDIT_TARGET,      // value = degC
};

struct Cmd {
  CmdType type;
  uint8_t sub;
  int16_t arg;
  int16_t idx;
  float value;
};

enum BusOp : uint8_t { BUS_READ, BUS_WRITE, BUS_FINDSP };

struct BusRequest {
  BusOp op;
  uint8_t ch;        // channel whose controller (address, register map) is used
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
extern SemaphoreHandle_t gLock;

struct Guard {
  Guard() { xSemaphoreTake(gLock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(gLock); }
};

void appInit();                  // create locks/queues, load settings and programs
void controlTaskStart();
bool sendCmd(CmdType c, int arg = 0, float value = 0, int idx = -1, uint8_t sub = 0);
bool busRequest(BusRequest& r, uint32_t timeoutMs = 20000);  // blocks until done
void saveSettingsAndNotify();    // persist gSettings and tell the control task (reopens the link)
void saveSettingsQuiet();        // persist gSettings only (display, selection, manual SP)
// Loop task (UI + web) scratch program buffer (one task: no lock needed)
DuoProgram& scratchProgram();
// The bottom channel's plan of stored program idx as TempCtl steps (old UIs)
bool loadLegacyView(int idx, Program& out);
// Running copy of the program as JSON (under gLock); false if there was no run
bool runProgramJson(JsonObject o);
void selectProgram(int idx);     // select + persist
void setManualSp(float v, int ch = ENGINE_CH);  // clamp, persist and send to the control task
float runSpanFor(float total);   // x axis length for a program: total + spare
void chartSnapshot(ChartData& d);
