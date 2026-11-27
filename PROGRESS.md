# OS-Sim progress

Read this first after a session restart; continue from the first unfinished stage.

| Stage | Title | Status |
|---|---|---|
| 1 | Skeleton | DONE |
| 2 | Scheduling core | DONE |
| 3 | Memory core | DONE |
| 4 | Deadlock core | DONE |
| 5 | Thread engine | DONE |
| 6 | GUI: Scheduling tab | DONE |
| 7 | GUI: Memory tab | DONE |
| 8 | GUI: Deadlock tab + RAG | DONE |
| 9 | Integration + export | DONE |
| 10 | Polish, docs, final verification | DONE |
| 11 | Extension: process synchronization module | DONE |

## Environment
Ubuntu VM, 2 cores, ~6 GB RAM. g++ 15.2, CMake 4.2.3, Qt 6.10.2 (Widgets + Charts found).

## Stage 1: Skeleton
- Built: directory tree, top-level CMake (C++17, -Wall -Wextra, `-DSANITIZE=thread|address`),
  libraries `oscore`, `osengine`, executable `ossim_console`, optional Qt GUI `ossim`, 4 placeholder ctest tests.
- Run: `mkdir -p build && cd build && cmake .. && make -j2 && ctest`
- Known problems: none.

## Stage 2: Scheduling core
- Built: `core/pcb.h`, `core/scheduler.*` (FCFS, SJF, SRTF, RR, Priority NP/P via one tick simulator;
  `schedulerPick()` is the per-tick dispatcher decision, reusable by the integrated run), `core/io_formats.*`
  (process CSV load/save), `apps/console_main.cpp` (text Gantt + metrics), 3 sample CSVs,
  `tests/test_scheduler.cpp` (every row of 6.1 plus RR-arrival rule, preemptive priority, validation).
- Run: `cd build && ./apps/ossim_console sched ../samples/processes_srtf.csv 4` (or from project root `./build/apps/ossim_console`)
- Test result: `test_scheduler: 54 checks, 0 failures`.
- Note: tie rule "same key -> earlier arrival -> lower pid" is applied literally, also for SRTF preemption.

## Stage 3: Memory core
- Built: `core/memory.*`: `FramePool` (one replacement policy object: FIFO/LRU/Optimal, ties -> earliest loaded,
  fill frames 0,1,2..), `runPageReplacement`, `parseRefString`, `FrameAllocator::allocateEqual`, `buildPageTable`.
  Samples `refstring_standard.txt`, `refstring_belady.txt`. Console command `memory`.
- Run: `./build/apps/ossim_console memory` (from project root) or `memory "7,0,1,2" 3`.
- Test result: `test_memory: 26 checks, 0 failures` (15/12/9, Belady 9/10, LRU hits 8 ratio 0.4).

## Stage 4: Deadlock core
- Built: `core/deadlock.*` (`bankersSafety` with the required pass scan, `bankersRequest`, `detectDeadlock`,
  `recoverByTermination`, `buildRag` with DFS cycle search), `loadMatrixFile`, `loadRequestsCsv`,
  samples `banker_sample.txt`, `deadlock_sample.txt` (P2 requests 0 0 1 -> P1..P4 deadlocked),
  placeholder `requests_integrated.csv` (redesigned in Stage 9). Console command `deadlock`.
- Run: `./build/apps/ossim_console deadlock`
- Test result: `test_deadlock: 38 checks, 0 failures` (safe seq P1,P3,P4,P0,P2; detection order P0,P2,P3,P4,P1;
  recovery kills P1 and clears the deadlock).
- Note: in detection, a process with zero allocation is marked finished at the start (textbook rule)
  and therefore not listed in `finishOrder`.

## Stage 5: Thread engine
- Built: `engine/logger.*` (`PipeLogger`: `pipe()`, one atomic `write()` per line, `O_NONBLOCK` reader `drainLog()`),
  `engine/engine.*` (`SimulationEngine`: clock thread + one pthread per process; handshake
  `sem_post(runSem[i])` -> process -> `sem_post(doneSem)`; mutex on all shared state; `pthread_cond_wait`
  for pause/step and `pthread_cond_timedwait` for the tick delay; clock thread is the single dispatcher
  that drains the pipe and calls the observer; `realProcessDemo()` with fork/execlp/waitpid).
- Shutdown design: `stop()` only ends the clock loop; the clock thread then sets `exitThreads_` and posts every
  `runSem`, so a process thread can never exit while the clock waits on `doneSem`.
- Run: `./build/apps/ossim_console engine samples/processes_srtf.csv rr 4 --real`
- Tests: `test_engine: 34 checks, 0 failures` (finish 24/27/30, one TERMINATED each, 20 identical logs,
  pause/step/resume/stop under a 5 s `alarm()` watchdog).
- Sanitizers (manual): `mkdir build-thread && cd build-thread && cmake .. -DSANITIZE=thread && make -j2 test_engine ... && ctest`
  and the same with `-DSANITIZE=address` (address+undefined): all tests pass, no reports.

## Stage 6: GUI, Scheduling tab
- Built: `gui/MainWindow` (tabs, monospace event-log dock, File/Help menus, About with group NBA, status bar),
  `SchedulingTab` (editable process table, Add/Remove/Clear/Load/Save CSV, 6 algorithms, quantum only for RR,
  Run / Run animated / Compare all, Pause/Step/Stop, speed slider 50-1000 ms), `GanttWidget` (custom paint,
  scrollable, partial drawing while animating), `BarChartWidget` (QPainter grouped bars), `EngineBridge`
  (engine observer -> queued Qt signal; per-run generation number drops stale events), `Style.h` (palette,
  `ui::warn` dialogs that become stderr lines in self-test mode), `--selftest`.
- Engine got `setTickDelay()` so the speed slider works during a run.
- Charts: **BarChartWidget only; QtCharts is not used/linked** (avoids runtime/offscreen issues; allowed by CLAUDE.md).
- Run: `cd build && ./gui/ossim`; self-test: `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` -> exit 0
  (set `OSSIM_SHOTS=<dir>` to also save PNG screenshots of the window).
- Verified: FCFS sample avg WT 17, RR q=4 avg WT 5.6667, Compare all table + chart, animated replay finish times match core.

## Stage 7: GUI, Memory tab
- Built: `gui/MemoryTab` (reference string, frames 1-10, FIFO/LRU/Optimal/Compare all, Run, Random string,
  Standard example, Belady example; frame table per algorithm with Frame rows + Result (HIT green / FAULT red,
  tooltip = replaced page) + Evicted row; summary boxes; faults bar chart; Belady demo shows FIFO faults for
  1..6 frames; page-table panel using `FrameAllocator::allocateEqual` + `buildPageTable` for the pids of
  the Scheduling tab (shared process list)).
- Self-test now also checks 15/12/9, Belady 9 vs 10, and warnings for "1,a" and an empty string.
- Run: `cd build && ./gui/ossim` -> Memory tab -> Standard example.

## Stage 8: GUI, Deadlock tab and RAG
- Built: `gui/DeadlockTab` (process 1-10 / resource 1-6 spin boxes; editable Allocation, Max, Request, Available;
  read-only Need with "!" when Allocation > Max; Load Banker's / deadlock sample, Clear; Check safety with step
  trace, Request resources (granted requests are applied), Detect deadlock (rows red), Recover (one victim every
  700 ms, killed rows grey)), `gui/RagView` (QGraphicsScene: process circles on top, resource squares with
  instance dots below, solid assignment / dashed request edges with arrowheads and counts, deadlocked processes
  and cycle edges red).
- Self-test checks SAFE <P1, P3, P4, P0, P2>, P1 (1 0 2) granted, P4 (3 3 0) and P0 (0 2 0) denied,
  P1..P4 deadlocked, recovery clears it, and warnings for bad input.
- Note: after recovery a cycle P2 -> C -> P2 is still drawn red although there is no deadlock; this is
  intended (multi-instance resources: a cycle is necessary, not sufficient).

## Stage 9: Integration and export
- Built: `core/integrated.*` (`runIntegrated`, pure/deterministic, in core/ per the golden rule): one tick loop
  that uses `schedulerPick()` (all 6 algorithms, Waiting processes skipped), one shared `FramePool`
  (keys pid*1000+page, per-process reference strings from `std::mt19937(pid)` with 70% locality),
  scripted requests (`bankersRequest` when avoidance is ON, "grant if available" when OFF; denied -> Waiting,
  retried every tick), detection every K ticks over the blocked requests, `recoverByTermination` when
  auto-recover is on. Max claim of a process = sum of its scripted requests; totals come from
  `# total=1 1 2` in `requests_integrated.csv` (or the GUI field).
- `samples/requests_integrated.csv` (for `processes_srtf.csv`, RR q=2): avoidance ON -> P2's request for B is
  refused (unsafe), no deadlock; avoidance OFF -> P1 holds A wants B, P2 holds B wants A -> deadlock detected
  at t=9, P1 killed, everything else finishes.
- GUI: "Integrated run" page in the Scheduling tab (requests file, totals, frames, page algorithm, avoidance,
  auto-recover, detection interval); results + Gantt + log; the state at the first deadlock is copied to the
  Deadlock tab with the same pids (P1..P4); Deadlock tab button "Use Scheduling tab pids" (shared process list).
  File > Export report... writes `<name>.txt` and `<name>.csv` (CSV sections start with `## `).
- Tests: `tests/test_integrated.cpp` (44 checks). ctest now has 5 tests.
- Sanitizers: whole GUI built with `-DSANITIZE=address` (address+undefined): ctest 5/5 and `--selftest` exit 0,
  no reports. TSAN build of all tests: 5/5.
- Limitation: in the integrated run, "Optimal" uses each process's own future references (the global future
  depends on the schedule, which is not known in advance).
- The integrated run is computed by core/ (not replayed by the thread engine), because killed/blocked processes
  do not fit the engine's "replay a finished timeline" model.

## Stage 10: Polish, docs, final verification
- Final verification (fresh directories, real output):
  - `rm -rf build && mkdir build && cd build && cmake .. && make -j2` -> 0 warnings; `ctest --output-on-failure`
    -> 100% tests passed, 0 tests failed out of 5 (scheduler 54, memory 26, deadlock 38, engine 34, integrated 44 checks).
  - `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` -> exit 0 (67 PASS, 0 FAIL).
  - `-DSANITIZE=thread` (all tests + console engine): 5/5, no ThreadSanitizer report.
  - `-DSANITIZE=address` (address+undefined, whole app incl. GUI): 5/5, selftest exit 0, no reports.
- Docs: `README.md`, `docs/REPORT.md`, `docs/VIVA_NOTES.md` (concepts + 25 questions),
  `docs/screenshots/README.md` (8 screenshots to take, with steps).

## Stage 11: Extension - Process Synchronization module
- Built:
  - `core/sync.*` (pure, deterministic): `SyncSim` executes one pseudocode line per `step(pid)` for
    race condition, Peterson (lecture variant, 2 processes), mutex, binary / counting semaphore,
    producer-consumer (empty/full/mutex) and monitor (Mesa, notFull/notEmpty). Scheduler: scenario script,
    then round robin (k instructions per turn, producer/consumer speeds) or seeded random. Stats: expected vs
    actual, lost updates, CS violations, max inside, CS entries, busy-wait / blocked steps, overflows,
    completion. `syncScenarios()` (2-4 samples per algorithm), `compareSync()` (same 2-process workload:
    unsynchronized / Peterson / mutex / binary semaphore), `syncExplanation()`, `syncGuarantee()`.
  - `engine/sync_demo.*`: `SyncDemoRunner` - real pthreads for every primitive (split atomic load/store race,
    Peterson with seq_cst `std::atomic`, `pthread_mutex`, `sem_t` binary/counting, producer-consumer with
    `sem_t` + mutex, monitor with `pthread_cond`), PipeLogger IPC, result checks, `cancel()` that wakes
    blocked threads (sem_post cascade / cond broadcast) so a cancelled run always joins.
  - `gui/SyncTab.*`, `gui/SyncViews.*` (SyncView animated scene, CodeView pseudocode, SyncTimeline),
    `gui/SyncBridge.*` (std::thread + queued signals). New main tab "Synchronization": algorithm + scenario,
    parameters, Run/Pause/Resume/Step/Stop/Reset/Run to end, speed slider, process cards with per-process
    Step (manual interleaving), live variables, timeline, event log, statistics (theory vs observed),
    comparison page (table + bar chart), real POSIX demo page, explanation panel.
  - MainWindow: tab, log, export sections `=== Synchronization` / `## Synchronization`,
    `## Synchronization comparison`, `## Synchronization POSIX demo`; self-test extended (about 50 checks).
  - Console: `ossim_console sync [--real]`.
- Tests: `test_sync` (1068 checks, incl. exhaustive Peterson state search: 1211 states) and
  `test_sync_engine` (72 checks, incl. cancellation under a watchdog). ctest now has 7 tests.
- Sanitizers: TSAN build of test_sync + test_sync_engine (3 runs): no report. ASAN+UBSAN: ctest 7/7 and the
  whole GUI `--selftest` exit 0, no report.
- Run: `cd build && ./gui/ossim` -> Synchronization tab; `./build/apps/ossim_console sync --real`.
- Notes: simulated queues are FIFO (POSIX promises no order - stated in the GUI and docs). The animation is
  driven by a QTimer stepping the core model (deterministic); real concurrency is in the POSIX demo page.

## Definition of done
- [x] Clean build with zero warnings, all tests green.
- [x] Every number of CLAUDE.md Section 6 is asserted in a test and passes.
- [x] Section 11 (synchronization extension): every algorithm simulated, tested, in the GUI and documented.
- [x] `./gui/ossim` launches; all three tabs work; bad input shows warnings; `--selftest` exits 0 offscreen.
- [x] Engine uses pthread_create/join, pthread_mutex_*, sem_init/wait/post/destroy, pthread_cond_* and pipe();
      20 runs give byte-identical logs.
- [x] TSAN and ASAN/UBSAN builds report nothing.
- [x] Export writes .txt and .csv with all sections (checked by the self-test).
- [x] README.md, docs/REPORT.md, docs/VIVA_NOTES.md, PROGRESS.md exist.
- [x] One git commit per stage.

## Still to do by the student
- Take the screenshots listed in `docs/screenshots/README.md`.

## Not done
Nothing from the stage list was dropped. Simplifications (also listed in REPORT.md, "Limitations"):
- QtCharts is not used; all charts are `BarChartWidget` (custom QPainter).
- The integrated run is computed in core/ and displayed at once; it is not replayed by the thread engine.
- The fork/exec/wait demo is in the console program only (`ossim_console engine ... --real`).
- Optimal replacement inside the integrated run uses each process's own future references.
