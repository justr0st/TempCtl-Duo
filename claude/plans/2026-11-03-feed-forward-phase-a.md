# Feed-forward on the local channels (phase A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Local channels follow ramps closely: the output gets the power the heater model expects for the
setpoint θ seconds ahead, and the PID only corrects the rest.

**Architecture:** Pure, host-tested pieces first:
- the FF formula and the PID's FF input (`pid.*`);
- the setpoint preview (`run_engine.*`);
- the model from autotune (`autotune.*`);
- the settings (`settings.*`).

Then the wiring: the control task computes the preview each cycle and hands it to the local task, which adds
FF in `step()`. Last, parity (web, touch, console), the live / log fields and the docs.

**Tech Stack:** C++17 (Arduino-ESP32 2.0.17, PlatformIO), LVGL 9.6, ArduinoJson 7.4, vanilla JS (`web/index.html`).
Host tests run in WSL (`docs/wiki/Development.md`).

**Spec:** `claude/tempctl-duo-spec.md` section 9.23 (phase A only). Read it before Task 1.

## Global Constraints

- FF off = exactly today's behaviour (PID output, integrator clamp 0…outMax, every protection).
- FF is used only where the PID runs today: outputs on, fresh reading, no fault, not autotuning.
- The total output stays clamped to 0…maxOut. Cut-off, runaway / heating failed, heating while off, reading
  frozen, the E-stop and the SSR timeout see the total output, unchanged.
- Ambient for the model: fixed 25 °C (`FF_AMBIENT_C`).
- New settings in the channel's `local` object:
  - `ff` (bool, default off; not switched on by autotune's Accept);
  - `ffK` (°C/%, 0.1…20, default 3);
  - `ffTau` (s, 5…3600, default 120);
  - `ffDead` (s, 0…120, default 8).
  Missing keys = defaults.
- Full parity: touch (S3 480×320 and CYD 320×240), web Channels page, serial `b.local.ff…` / `t.local.ff…`,
  `/api/config`.
- No new source files (the demo, UI host and console host build fixed file lists).
- API_VERSION unchanged (new keys only). `FW_VERSION` → `1.0.67`.
- Code rules from `CLAUDE.md`: ArduinoJson strings via `JsonString` / `String`; LVGL only in the loop task;
  touch colours only through named styles; log through the log functions with a kind.
- **No commits.** Rost commits when he asks. Each task ends with its checks passing.

## Review Focus

1. **FF switched on or off during a run.** Expected: no jump in the output. The test is in Task 1
   (`shiftIntegral`).
2. **Program paused, or waiting for the bottom, while FF is on.** Expected: the preview stays at the current
   setpoint (rate 0), so there is no ramp power for a ramp that isn't happening. The test is in Task 2.
3. **The control task stalls** (a Modbus timeout of up to 5 s). Expected: a stale preview isn't used. After
   2 s the local task falls back to (SP, 0). The test is in Task 1 (`ffInput` age rule).
4. **A cooling ramp or a setpoint below 25 °C.** Expected: FF goes negative, the total is clamped at 0, and
   nothing breaks. The test is in Task 1.
5. **The CYD Channels page with 4 more rows.** Expected: it still builds within its RAM check. The check is
   in Task 6 (UI host `lowmem` + `MEM=1`).

---

### Task 1: FF formula and PID feed-forward input

**Files:**
- Modify: `src/pid.h`, `src/pid.cpp`
- Test: `test/host_test.cpp` (`testPid`, new `testFeedForward`, add the call in `main`)

**Interfaces:**
- Produces:
  - `constexpr float FF_AMBIENT_C = 25.0f;`
  - `struct FfModel { float k, tau, dead; };` (°C/%, s, s)
  - `float ffOutput(const FfModel& m, float spAhead, float rate);` returns
    `(spAhead - FF_AMBIENT_C) / m.k + m.tau * rate / m.k`. It is not clamped, and NaN if spAhead is NaN.
  - `float ffInput(const FfModel& m, float sp, float spAhead, float rate, uint32_t ageMs);` uses
    (spAhead, rate) when `ageMs <= 2000` and spAhead is not NaN, else (sp, 0). Returns `ffOutput`.
  - `float Pid::update(const PidParams& p, float sp, float pv, float dtSec, float ff = NAN);`
    - ff NaN = today's behaviour (integrator clamp 0…outMax).
    - With a number: `out = clamp(ff + P + I + D, 0, outMax)`, and the integrator is clamped to −outMax…+outMax.
    - The anti-windup test uses the unsaturated total including ff.
  - `void Pid::shiftIntegral(float delta);` adds delta to the integrator, clamped to −100…100. Used for the
    bumpless on / off switch.
  - `float Pid::lastFf() const;` the ff of the last update, 0 when NaN.

- [ ] **Step 1: Write the failing tests** in `testFeedForward()`, on the existing `Heater` (gain 3, τ 120, θ 8) and
  `PidParams cl{3.0f, 0.03f, 20, 100}`, with 0.25 s steps and readings rounded to 0.25 °C like `testPid`:
  - `ffOutput({3,120,8}, 200, 0)` near 58.33; `ffOutput({3,120,8}, 200, 0.5f)` near 78.33;
    `ffOutput({3,120,8}, 100, -1)` near −15.
  - `ffInput`:
    - age 2000 ms uses the preview;
    - age 2001 ms gives `ffOutput(m, sp, 0)`;
    - spAhead NaN gives `ffOutput(m, sp, 0)`.
  - Ramp 25 → 200 °C at 0.5 °C/s (350 s), then a 300 s hold. SP(t) and SP(t+θ) are computed in the test.
    - Run once with `ff = NAN` and once with `ffInput(model, SP(t), SP(t+8), slope(t+8), 0)`, using the true
      model {3, 120, 8}.
    - Measure the lag = max(SP − PV) from t = 30 s to the ramp's end, and the overshoot = peak − 200.
    - `CHECK(lagFf < 3.0f && overFf < 2.0f && lagFf < lagNoFf);`
    - Print both pairs (they go into spec 9.23).
  - Wrong model, K × 0.7 and K × 1.3: at the end of a 10 min hold, `fabsf(T - 200) < 1.0f`, and the peak stays
    below 215.
  - Bumpless switch. Settle at 200 °C with ff NaN, then call `shiftIntegral(-ff)` and switch ff on. The
    output of the next update changes by less than 2 %. The same applies switching off with `+lastFf()`.
  - Cooling: `update(cl, 100, 150, 0.25f, ffOutput(m, 100, -1))` returns 0 (clamped, no NaN).
  - In `testPid`, the existing checks still pass unchanged (ff defaults to NaN).
- [ ] **Step 2: Run** the host test (command in `docs/wiki/Development.md`, with `-fsanitize=address,undefined`).
  Expected: compile errors for `ffOutput` / `FfModel`.
- [ ] **Step 3: Implement** the interfaces above in `pid.h` / `pid.cpp`. Keep derivative on the measurement and
  its filter as they are.
- [ ] **Step 4: Run** the host test. Expected: `all tests passed`. Note the printed lag / overshoot values.

### Task 2: Setpoint preview in the run engine

**Files:**
- Modify: `src/run_engine.h`, `src/run_engine.cpp`
- Test: `test/host_test.cpp` (new `testSpPreview`; the name `testPreview` is taken)

**Interfaces:**
- Produces: `bool RunEngine::preview(int ch, float sec, float& sp, float& rate) const;`
  - It ticks a copy of the engine `sec` seconds forward, with the PVs frozen at the last values the engine saw
    (`lastPv_` / `pvOk_`) and the event sink of the copy set to null.
  - `sp = copy.out(ch).sp`.
  - `rate = moving ? (target − sp) / rampLeft : 0` (signed °C/s).
  - Returns false (sp NaN, rate 0) when the engine isn't active, the channel isn't in the program, or it is
    idle at that moment.
  - It must not change `*this`, the program or the recorded events.

- [ ] **Step 1: Write the failing tests** with the existing program helpers in `host_test.cpp`:
  - **Sequence:** ramp 25 → 200 °C at 1 °C/s, then a 60 s hold, then a ramp to 150 °C.
    - 10 s into the first ramp, `preview(0, 8)` gives sp near 43, rate near 1.
    - 3 s before the ramp's end, the preview gives sp 200, rate 0.
    - 5 s before the hold ends, the preview gives a sp below 200 and a negative rate.
  - **Pause:** after `pause()`, the preview gives the current sp and rate 0.
  - **Wait for bottom** (bottom PV frozen below the band): the preview doesn't run past the wait.
  - **Run edit:** `setTarget` during the ramp → the preview follows the new target.
  - **Timeline:** each lane's preview is independent.
  - **Engine untouched:** `elapsed()`, `out(ch)` and the recorded events are the same before and after
    `preview()`. Count the events with a sink.
- [ ] **Step 2: Run** the host test. Expected: compile error, `preview` is not a member.
- [ ] **Step 3: Implement** `preview()` (const; copy, null the sink, `tick(sec, lastPv_, pvOk_)`, read `out(ch)`).
- [ ] **Step 4: Run** the host test. Expected: `all tests passed`.

### Task 3: Heater model from autotune

**Files:**
- Modify: `src/autotune.h`, `src/autotune.cpp`
- Test: `test/host_test.cpp` (`testAutotune`)

**Interfaces:**
- Produces new fields in `TuneResult`: `bool modelOk; float ffK, ffTau, ffDead;`, filled in `finishCycle()` with
  `c_.sp`.
- The algorithm, with ω = 2π / Pu:
  - `K = (sp − 25) / bias` (bias > 0);
  - `x = K · Ku`; if x ≤ 1, then modelOk = false;
  - `ωτ = sqrt(x² − 1)`;
  - `τ = ωτ / ω`;
  - `θ = (π − atan(ωτ)) / ω`.
  - modelOk = false also when K, τ or θ fall outside the setting ranges (0.1…20, 5…3600, 0…120).
- The message on success appends `, model K %.2f tau %.0f s dead %.0f s`. When there's no model it appends
  `, no heater model`.

- [ ] **Step 1: Write the failing tests** in `testAutotune`:
  - On `Heater h` (true K 3, τ 120, θ 8): `CHECK(r.modelOk)`, and K, τ, θ each within ±20 % of the true value.
    Print the three values.
  - The low-setpoint run (`c.sp = 100`): `modelOk` and K within ±20 %.
  - A result whose `K · Ku ≤ 1` gives `!modelOk`. Construct it via a heater whose relay test gives a tiny Ku,
    or test the helper directly (next bullet).
  - Factor the maths into `bool fopdtFromRelay(float sp, float bias, float ku, float pu, float& k, float& tau, float& dead)`
    in `autotune.cpp`. It is not static: declare it in `autotune.h` so the test can call it directly, with
    x ≤ 1 → false and with values out of range → false.
- [ ] **Step 2: Run** the host test. Expected: compile errors for `modelOk` / `fopdtFromRelay`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the host test. Expected: `all tests passed`.
  - **If the ±20 % check fails for τ or θ** (the relay's describing function is approximate): do not widen the
    tolerance. Stop and report the measured values to Rost. The spec's model source may need the heat-up phase
    as a second estimate.

### Task 4: Settings, JSON, validation, one Accept helper

**Files:**
- Modify: `src/settings.h`, `src/settings.cpp`, `src/web.cpp` (accept, about lines 772-785),
  `src/tui_chan.cpp` (accept, about lines 578-586), `src/console.cpp` (accept, about lines 1283-1293)
- Test: `test/host_test.cpp` (`testSettingsDefaults`, `testSettingsJson`, `testSettingsMerge`)

**Interfaces:**
- Consumes: `TuneResult` (Task 3).
- Produces:
  - In `LocalChannelSettings`: `uint8_t ff; float ffK, ffTau, ffDead;`
  - JSON keys `ff` (bool), `ffK`, `ffTau`, `ffDead` in the `local` object, written and read like `tuneFast` / `kd`.
  - Clamping in `settingsValidate`, next to `kd`.
  - `void settingsTakeTune(LocalChannelSettings& l, const TuneResult& r);` sets kp / ki / kd, and ffK / ffTau /
    ffDead when `r.modelOk`. It never changes `ff`. `settings.h` includes `autotune.h` (pure).

- [ ] **Step 1: Write the failing tests:**
  - Defaults: `ff == 0`, `ffK 3`, `ffTau 120`, `ffDead 8` on both channels.
  - JSON round trip of non-default values, in the file form and the API form.
  - A file without the four keys → defaults (merge test).
  - `ffK = 50` → clamped to 20; `ffDead = -1` → 0.
  - `settingsTakeTune` with modelOk false leaves the model unchanged; with modelOk true it copies the model;
    `ff` is unchanged in both cases.
- [ ] **Step 2: Run** the host test. Expected: compile errors for the new fields.
- [ ] **Step 3: Implement.** Then replace the three copied Accept blocks (web, touch, console) with
  `settingsTakeTune(gSettings.ch[ch].loc, t.res)`. Keep their own messages.
- [ ] **Step 4: Run** the host test, then `test/console_host/build.sh` and `BOARD=s3 test/console_host/build.sh`
  (they compile `console.cpp` and `settings.cpp`). Expected: `all tests passed`, both console hosts build
  without warnings.

### Task 5: Wiring — control task → local task, live and log fields

**Files:**
- Modify:
  - `src/local_ctrl.h`, `src/local_ctrl.cpp` (`Local`, `step()`, `LocalStatus`);
  - `src/app_state.cpp` (control cycle, after `driveSetpoint`; SD row about lines 1240-1252);
  - `src/channel.h`, `src/channel.cpp` (`ChannelLive`, `LocalChannel::poll`);
  - `src/web.cpp` (live channel JSON, about line 450);
  - `src/sdlog.h`, `src/sdlog.cpp` (`SdRow`, header, `sdlogRow`);
  - `tools/wiki/mock/server.py` (same CSV header);
  - `demo/sim.cpp` (model values).
- Test: firmware builds and the browser demo (`demo/build.sh`); this code is not host-testable.

**Interfaces:**
- Consumes: `ffInput`, `Pid::update(..., ff)`, `shiftIntegral`, `lastFf` (Task 1); `RunEngine::preview` (Task 2);
  the settings (Task 4).
- Produces:
  - `void localCtrlSetPreview(int ch, float spAhead, float rate);` stores the values with `millis()` (NaN =
    none), under the existing lock.
  - `LocalStatus` gets `float ff, integ;` (NaN when FF is off or the PID isn't running).
  - `ChannelLive` gets `float ff, integ;` (NaN for Modbus and Off).
  - `/api/live` channel objects get `"ff"` and `"i"` (`setNum`, so NaN → null).
  - SD run log columns: `bottom_ff,bottom_i` after `bottom_out`, and `top_ff,top_i` after `top_out`
    (0 decimals).

- [ ] **Step 1: Control task.** Each cycle, for each local channel with `loc.ff` on:
  - in a run (`inRun(ch)`), call `eng.preview(ch, loc.ffDead, sp, rate)`, then `localCtrlSetPreview(ch, sp, rate)`;
  - otherwise (manual, or the run just ended), call `localCtrlSetPreview(ch, NAN, 0)`. The local task then uses
    its own SP with rate 0, and a stale preview from the run that just ended is never used.
- [ ] **Step 2: Local task.** In `step()`'s PID branch:
  - `ff = cfg.ff ? ffInput(model, l.sp, spA, rate, millis() - previewMs) : NAN`;
  - pass it to `pid.update`.
  - Track whether FF was on in the last step. On off → on, call `shiftIntegral(-ff)` before the update. On
    on → off, call `shiftIntegral(+pid.lastFf())`.
  - A PID reset clears this flag.
  - Fill `LocalStatus.ff` / `integ`.
- [ ] **Step 3: Live and log fields** as in Produces. Update the header in `tools/wiki/mock/server.py` to match.
- [ ] **Step 4: Demo.** In `demo/sim.cpp`, next to the PID values (about line 411), set `ffK` / `ffTau` / `ffDead`
  to values that fit the sim's heaters, with `ff` left off.
  - To get those values: run the demo's autotune once in the browser demo, or compute them from the sim
    constants.
  - Note which method was used in a comment.
- [ ] **Step 5: Build** all four envs (`pio run -e s3`, `cyd-1xlocal-1x485`, `cyd-2xlocal`, `cyd-2xlocal-2x485`)
  and `demo/build.sh` (WSL, wasi-sdk). If the wasi-sdk is missing, say so and skip the demo build.
  - Expected: no warnings in our files.
  - Report the CYD `Flash:` lines (1.0.66: 93.6 / 93.6 / 94.1 %).

### Task 6: Parity — web, touch, console

**Files:**
- Modify: `web/index.html` (Channels › PID control fields, about line 2633; autotune panel, about line 2743),
  `src/tui_chan.cpp` (rows after Kd, about line 436; the accept toast / question, about line 608),
  `src/console.cpp` (key table, about line 1347; help, about line 442; `tune` status / accept lines),
  `src/web.cpp` (autotune JSON, about line 320), `test/console_host/main.cpp` (script)
- Test: UI host shots, `test/console_host`, Playwright `test/web_*.js` (if set up)

**Interfaces:**
- Consumes: the settings and `TuneResult.modelOk / ffK / ffTau / ffDead` (Tasks 3, 4).
- Produces:
  - In the autotune JSON, when `modelOk`: `"model":{"k":…,"tau":…,"dead":…}`.
  - Texts (identical on web, touch and console where they fit):
    - Switch: "Feed-forward". Hint: "Adds the output the heater model expects for the setpoint ahead (holding + ramp
      power); the PID corrects the rest. The autotune measures the model."
    - "°C per % output" (`ffK`, 2 decimals);
    - "Time constant" (`ffTau`, s, 0 decimals);
    - "Dead time" (`ffDead`, s, 0 decimals).
    - The model rows are shown whether the switch is on or off, so an Accepted model can be checked before
      switching it on.

- [ ] **Step 1: Web.**
  - Add the four fields after `local.kd` (the K / τ / θ rows joined like Kp · Ki · Kd).
  - In the autotune panel's Proposed / Current table, add the three model columns when `a.model` exists.
  - The Accept message says the model was taken over, and that feed-forward stays as it is.
- [ ] **Step 2: Touch.**
  - Add the rows after Kd: an `F_SW` row, then three `F_NUM` rows with `L_JOIN | L_COL` like Kp / Ki / Kd,
    with getters / setters named like `gTuneFast` / `sTuneFast`.
  - The accept question includes the model when `modelOk`.
- [ ] **Step 3: Console.**
  - Key table: `local.ff` (K_BOOL), `local.ffK` (0.1…20), `local.ffTau` (5…3600), `local.ffDead` (0…120).
  - One help line.
  - The `tune` done / accept lines print the model.
  - Extend `test/console_host/main.cpp`'s script with `set b.local.ff=on b.local.ffK=2.5` and `get b.local`.
- [ ] **Step 4: Check.** The UI host (both sizes, dark and light) must show the four rows on the Channels page
  with no overlap. Also run:
  - `./uihost chan`, `./uihost_cyd lowmem` and `./uihost monkey 2000`, all exit 0 with no sanitizer findings;
  - `MEM=1 BOARD=cyd` and `./uihost_mem mem`: report the Channels page's heap compared with before;
  - `./conhost`: the new keys are listed and set.
  - Playwright: run `test/web_jumps.js` if Node / Playwright are set up in WSL, else say they were not run.

### Task 7: Docs, version, final checks

**Files:**
- Modify:
  - `src/version.h` (`FW_VERSION "1.0.67"`);
  - `claude/tempctl-duo-spec.md`: a 9.10 entry for 1.0.67; 9.23 status "phase A implemented in 1.0.67", plus
    the measured lag / overshoot and the autotune model accuracy from Tasks 1 and 3;
  - `docs/wiki/Channels.md` (PID section: feed-forward, the model, when to switch it on);
  - `docs/wiki/Web-API.md` (`ff` / `i` in `/api/live`, the four config keys, `model` in the autotune object);
  - `docs/wiki/Internals.md` (the SD log columns, about line 150).
- Regenerate: `docs/wiki/img/touch/*.png` of the Channels page (UI host, `tools/wiki/touch_png.py`).

- [ ] **Step 1:** Write the docs and the version.
- [ ] **Step 2:** Full check:
  - clean build of all four envs (`pio run -e <env> -t clean` first): no warnings;
  - host test, both console hosts, UI host (both sizes, both themes): all pass.
- [ ] **Step 3:** Report to Rost:
  - what was checked on the PC;
  - the flash sizes;
  - what he tests on the station: an autotune → Accept (model shown); the same program with FF off and then
    on; both SD run logs (the `_ff` / `_i` columns); no output jump when switching FF on during a hold.
