// Shared state between the control task (channels + run engine), the LVGL
// UI and the web server. Everything in here is protected by gLock.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "channel.h"
#include "duo_program.h"
#include "run_engine.h"
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
  uint8_t chSt[NUM_CHANNELS];       // ChState
  bool canExt[NUM_CHANNELS];        // a hold of this channel is running ("Extend hold")
  int16_t kept[NUM_CHANNELS];       // phase/step whose target the channel has (-1 none)
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

  // ---- "Run interrupted by a reset" offer (spec 9.3.5); the program is runProg / runName ----
  bool intrValid;
  bool intrExact;     // exact position (RTC memory) - else as of the last phase / step change or minute
  RunPos intrPos;
  char intrReason[20];
};

constexpr int RUN_EV_MAX = 400;

// Event log (web, serial, touch): the last LOG_N messages; also in the system log (syslog.h) and saved
// to flash from there
#ifdef BOARD_HAS_PSRAM
constexpr int LOG_N = 48;
#else
constexpr int LOG_N = 32;  // CYD: DRAM is short
#endif
constexpr size_t LOG_LEN = 120;
struct LogEntry {
  uint32_t seq;
  uint32_t upSec;   // uptime
  uint32_t epoch;   // UTC seconds, 0 = the clock was not known yet
  float runT;       // run time, NaN outside a run
  uint8_t kind;     // LogKind (syslog.h): event, run, warning, error
  char msg[LOG_LEN];
};
// any task; the kind comes from the words (errors, warnings), else "event" / "run"
void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logRun(const char* fmt, ...) __attribute__((format(printf, 1, 2)));  // run records (start, phases, end)
uint32_t logSeq();                                        // newest entry
int logSince(uint32_t seq, LogEntry* out, int max);       // entries after seq, oldest first

// Chart value in 0.1 degC, assigned / read as float; NaN (no value) = the type's minimum.
// Tenth (32 bit) is only ever accessed as a whole word, so the chart history can live in the
// ESP32's IRAM heap (32-bit access only) on the CYD, which has no PSRAM and little DRAM left.
// Tenth16 halves the RAM of the touch chart's copy (DRAM).
struct Tenth {
  int32_t v;
  Tenth& operator=(float f) {
    v = (isnan(f) || f > 3.0e5f || f < -3.0e5f) ? INT32_MIN : (int32_t)lroundf(f * 10.0f);
    return *this;
  }
  operator float() const { return v == INT32_MIN ? NAN : v * 0.1f; }
};
struct Tenth16 {
  int16_t v;
  Tenth16& operator=(float f) {
    v = (isnan(f) || f > 3276.0f || f < -3276.0f) ? (int16_t)INT16_MIN : (int16_t)lroundf(f * 10.0f);
    return *this;
  }
  operator float() const { return v == INT16_MIN ? NAN : v * 0.1f; }
};
typedef Tenth TenthRow[CHART_N];

// Chart history, both channels.
struct History {
  // Live history: a ring of liveCap slots (one per LIVE_SAMPLE_SEC, 3 words each, see
  // app_state.cpp); slot s (counted since boot) is at s % liveCap.
  uint32_t* liveRing;
  uint32_t liveCap, liveTotal;
  // The live view: CHART_N bins of the ring (rebuilt on demand, liveView()), newest at the end.
  // (the value arrays are pointers into one block, see appInit(): IRAM on the CYD)
  TenthRow *livePv, *liveSp;  // [NUM_CHANNELS]; PV = the last reading of the bin
  TenthRow *liveLo, *liveHi;  // lowest / highest PV reading within each bin (every poll counts)
  // Current / last program run: run time 0..runSpan in CHART_N bins (the
  // span doubles when the run gets longer than planned).
  bool runValid;
  float runBin, runSpan;
  TenthRow *runPv, *runSp;
  TenthRow *runLo, *runHi;    // lowest / highest PV reading within each run bin
  // run's program curve: its setpoints so far + the forecast (kept after the run for the CSV)
  TenthRow* runPlan;
  float runTotal;     // elapsed + remaining
  char runName[PROG_NAME_LEN];
  char runSt[NUM_CHANNELS][CHART_N];  // channel state per bin (chart lanes): "-orhwjkxpd" = ChState, 'm' manual, ' ' none
  int16_t runStLast;
  PlanEv runEv[RUN_EV_MAX];           // phase starts, hold ends, pauses, end (t = run time)
  uint16_t runEvN;
  uint32_t runGen;                    // changes when a new run starts
  // preview of the selected program on its own time axis (no run active)
  TenthRow* plan;
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
  Tenth16 pv[CHART_N], sp[CHART_N], plan[CHART_N];     // bottom (ENGINE_CH)
  Tenth16 pvT[CHART_N], spT[CHART_N], planT[CHART_N];  // top
#ifdef CHART_LOHI
  Tenth16 lo[CHART_N], hi[CHART_N], loT[CHART_N], hiT[CHART_N];  // lowest / highest PV within each point
#endif
};

enum CmdType : uint8_t {
  CMD_START,        // arg = program slot (-1 = selected program), sub 1 = the draft from startDraft()
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
  CMD_RUN_RECOVER,  // "Resume from the interrupted phase" (run interrupted by a reset)
  CMD_RUN_DISMISS,  // dismiss the interrupted run
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
// Restart on request (touch, web): noted in RTC memory (the next boot logs "restart from ..."),
// local outputs off, then the main loop restarts once the display has finished drawing.
void appRequestRestart(const char* from);
bool appRestartPending();
unsigned controlTaskStackFree();  // bytes of the control task's stack never used so far
bool sendCmd(CmdType c, int arg = 0, float value = 0, int idx = -1, uint8_t sub = 0);
bool busRequest(BusRequest& r, uint32_t timeoutMs = 20000);  // blocks until done
void saveSettingsAndNotify();    // persist gSettings and tell the control task (reopens the link)
void saveSettingsQuiet();        // persist gSettings only (display, selection, manual SP)
// A temporary program buffer (≈ 3.9 kB, heap) for the loop task (UI, web), freed at the end of the scope
struct ScratchProg {
  DuoProgram* p;
  ScratchProg() : p((DuoProgram*)malloc(sizeof(DuoProgram))) {}
  ~ScratchProg() { free(p); }
  ScratchProg(const ScratchProg&) = delete;
  ScratchProg& operator=(const ScratchProg&) = delete;
  explicit operator bool() const { return p != nullptr; }
  DuoProgram& operator*() { return *p; }
};
// "Start without saving" (web): starts program slot idx with this content instead of the stored one
bool startDraft(int idx, const DuoProgram& p);
// Running copy of the program as JSON (under gLock); false if there was no run
bool runProgramJson(JsonObject o);
bool runProgramCopy(DuoProgram& out);  // the running copy (false if there was no run)
void selectProgram(int idx);     // select + persist
void setManualSp(float v, int ch = ENGINE_CH);  // clamp, persist and send to the control task
float runSpanFor(float total);   // x axis length for a program: total + spare
void chartSnapshot(ChartData& d, bool live = false);  // live: the live view even during a run
// Live history as CSV: rows from slot `from` (counted since boot; the oldest kept slot if older) on,
// as many as fit into buf. Returns the length (0 = done) and the next slot in `from`.
// pvMask / spMask: channels (bit 0 bottom, bit 1 top) whose PV (+ lowest, highest) / setpoint are written
size_t liveCsv(uint32_t& from, char* buf, size_t n, uint8_t pvMask = 3, uint8_t spMask = 3);
uint32_t liveHistSec();          // seconds the live history can hold
// Live chart periods (minutes) to choose from, up to what the history holds; the one in effect:
// display.liveMin, at most the longest that fits (both under gLock / from the UI task)
constexpr int LIVE_PERIODS_N = 11;
extern const uint16_t LIVE_PERIODS[LIVE_PERIODS_N];
uint32_t liveMinEff();
uint32_t iramPureFree();         // free IRAM-only heap (32-bit access only, not usable as DRAM)
// /api/chart: v = "run" (current / last run with its forecast) or "live" (rolling window)
void chartJson(String& out, bool run);
