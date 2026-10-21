# Programs and runs

One program drives **both channels**. The device stores up to **20 programs** (names up to 31 characters);
on the first start it creates three examples: *Lead-free BGA*, *Leaded BGA* and *Lead-free BGA (Timeline)*.
Times are in seconds (shown as m:ss); a ramp of 0 is a jump.

## Two program types

### Sequence

Phases one after another (up to 100):

| Phase | What it does |
|---|---|
| **Bottom** (`B`) | the bottom ramps to its target, then holds it |
| **Top** (`T`) | the top ramps to its target, then holds it |
| **Both** (`BT`) | the bottom jumps to its target (or goes *Idle* = outputs off), the top ramps and holds |
| **Pause** (`P`) | waits for *Continue*; optionally **"Action required"** with a text up to 40 characters ("Place the board") |

- **Wait for the bottom** (Bottom and Both phases): the hold (Bottom) or the top ramp (Both) starts only when
  the bottom has reached its target within the *"bottom reached" band* (default ±5 °C).
- A channel **keeps its last target** while other phases run.

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/editor-seq-dark.webp"><img src="img/web/editor-seq-light.webp" alt="The Lead-free BGA Sequence" width="900"></picture>

### Timeline

Two independent step lists (up to 100 steps each), started together:

| Step | What it does |
|---|---|
| **Step** | target, ramp time, hold time |
| **Idle** | outputs off for a time |
| **Pause** | the whole run pauses (both channels) until *Continue* |

A channel that finishes earlier keeps its last target until the other one is done.

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/editor-tl-dark.webp"><img src="img/web/editor-tl-light.webp" alt="The Lead-free BGA (Timeline)" width="900"></picture>

A Sequence can be turned into a Timeline (*Convert to Timeline* on the web page): the same temperatures and
times; "wait for bottom" is dropped. A Timeline can't be turned back.

## Ramps

- The **first ramp** of a channel, and the first ramp after Idle, starts at its **measured** temperature; later
  ramps start at the previous target.
- A channel without phases / steps in the program stays under manual control during the run. The program's
  channels start with their outputs off until they get a target.

## Running a program

Choose the program (touch: tap the header; web: the *Program* list), then **Start**.

| Button | |
|---|---|
| **Pause / Continue** | freezes the program time (ramps stop where they are) |
| **Stop** | ends the run; each channel applies its end action |
| **Next phase** (Sequence) | ramp → hold (setpoint at the target), wait → hold / top ramp, hold → next phase; in a pause: continue |

<picture><source media="(prefers-color-scheme: dark)" srcset="img/web/editor-chart-dark.webp"><img src="img/web/editor-chart-light.webp" alt="The program on the chart: squares = targets, diamonds = ramp ends, circles = hold ends" width="900"></picture>

### Changes during a run (run edits)

While a program runs you can change the **running copy** (touch editor, web tables or chart handles, target
buttons on the cards / tiles):

- extend or shorten a hold (not below the time already held; down to "now" ends it), *+ Hold* / *Extend hold*;
- change later ramp times;
- change targets: the current one, later ones, or the target a channel keeps.

Slopes are kept and times adapt. In a Timeline a timing change of one channel shifts the other one through its
holds. After the run the device asks whether to **save the changes** into the program.

### The end

Each channel of the program applies its own **end action**: *keep the last setpoint*, *go to an end setpoint*
or *outputs off*. With "keep" or "end setpoint" the channel goes on in manual mode with that setpoint (its
manual button is on; switch it off when you are done). The alarm plays three short beeps.

## Manual setpoint

Outside a run (and for a channel the program doesn't use) each channel has a **manual setpoint** with an
on / off switch: the touch tile, the web card or sidebar, the serial command `manual`.

- **On:** local channel: the PID heats to the setpoint; Modbus: the controller's outputs on, the setpoint
  written.
- **Off:** local: SSR off; Modbus: outputs off.
- Manual mode is always off after a restart (the value is remembered). An optional **manual mode timeout**
  switches it off after a time without any touch, web or serial action ([Safety](Safety.md)).

## Faults and lost channels

- A **fault** on any channel of the program (no reading, runaway, over temperature …) stops the run, switches
  that channel's outputs off and waits for *Acknowledge* ([Safety](Safety.md)).
- A Modbus channel that doesn't answer for 30 s (*link-loss pause*, Channels page) pauses the program.
- **Start warning:** a channel that is already within 10 °C of (or above) its first target when the program
  starts gets a warning message; the run starts anyway.

## Run interrupted by a reset

After a power loss, brown-out, watchdog or crash during a run the device starts with the program's channels
**off** and shows **"Run interrupted by a reset"** with the reason, program, phase / steps and run time — on the
touch screen, the web page and the serial console. It never resumes by itself:

- **Resume** — Sequence: the interrupted phase from its beginning (ramps from the measured temperatures;
  channels the phase doesn't start get their kept target at once). Timeline: on at the interrupted run time,
  ramps from the measured temperatures in the time they had left, holds with their remaining time.
- **Later** (touch) — closes the message; the red "Interrupted" badge opens it again.
- **Dismiss** — the outputs stay off.

Starting another program also ends the offer. The position is kept exactly across resets (RTC memory) and,
for a power loss, saved at every phase / step change and every minute.

| | |
|---|---|
| <picture><source media="(prefers-color-scheme: dark)" srcset="img/touch/09-interrupted-dark.png"><img src="img/touch/09-interrupted-light.png" alt="Touch: run interrupted" width="360"></picture> | <picture><source media="(prefers-color-scheme: dark)" srcset="img/touch/06-action-dark.png"><img src="img/touch/06-action-light.png" alt="Touch: Action required" width="360"></picture> |

## Run logs

On the S3 every run is logged to the SD card (`runs/` folder, one CSV per run, a row every second plus the
events). The web *Log* page lists them; *Replay* shows a log as a chart ([Web page](Web-page.md#csv-replay)).

## Program files

Programs are JSON files (`/duo/prog0.json` … `prog19.json` on the device). The web *Programs* page exports and
imports single programs; *Tools › Backup* saves all of them in one file.
