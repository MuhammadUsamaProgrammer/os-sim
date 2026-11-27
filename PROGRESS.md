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
| P2-0 | Phase 2 baseline (build 0 warnings, 7/7 ctest, selftest 117 PASS) | DONE |
| P2-1 | Shared playback kit + M1 Disk Scheduling | DONE |
| P2-2 | M2 Process Management + Live Linux helper | DONE |
| P2-3 | M5 I/O Management | DONE |
| P2-4 | M4 Advanced Memory | DONE |
| P2-5 | M3 IPC | DONE |
| P2-6 | M6 Multithreading | DONE |
| P2-7 | Integration, robustness, docs, sanitizers | DONE |

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

## Phase 2 (specification: OS_SIM_PHASE_2_IMPLEMENTATION.md; details: docs/PHASE2_MODULES.md)
- Baseline before Phase 2: clean build 0 warnings, ctest 7/7, GUI selftest 117 PASS. The nested empty folder `os-sim/`
  in the project root was left untouched.
- Order followed: M1 Disk -> M2 Processes -> M5 I/O -> M4 Memory -> M3 IPC -> M6 Threads (as in the specification).
- Shared infrastructure: `gui/ModuleKit.*` (PlaybackBar, PseudoCodeView, ModuleHeader, semantic colours, layout helper),
  `gui/NativeRunner.*` (QProcess runner with run ids, process-group Stop with SIGKILL escalation, safe destruction),
  `apps/ossim_helper.cpp` (all Live Linux demos; the multithreaded GUI never forks).
- Compatibility changes: the Memory tab became the first sub-tab of the new Memory area (MainWindow::showMemoryTab());
  MainWindow disconnects child signals in its destructor (found by the new shutdown test: a helper finishing during
  shutdown delivered a signal to a half-destroyed window). The report export gained Phase 2 sections only.
- Tests: test_disk 487, test_procmgmt 62, test_io 49, test_memadv 108, test_ipc 52, test_threads 35, test_native 64
  checks; ctest 14/14. GUI selftest 211 PASS including all six modules, live demos, dark theme and 1100x700
  screenshots, and a clean shutdown with work still running (no helper process left).
- Expected values that were mis-derived in two of my own tests were corrected after re-deriving by hand
  (I/O polling useful ticks = 2, allocation candidates for 212 = 3); no model code was bent to fit a test.
- Live demo cleanup checked manually: no System V segments/queues or /tmp/ossim-fifo-* left after the runs.

## Responsive layout fix (global UI)
- Problem: in most modules the left configuration panel was clipped at its right edge (fields, tables, buttons
  unreachable) and could not be dragged wider; on smaller or scaled screens the window was wider than the screen.
- Root causes (measured with the new audit, before the fix: 106 inaccessible controls at 1366x768, window could not
  shrink to 1024x768, minimum about 1106x888):
  1. `assembleModule()` (all Phase 2 tabs) and the Synchronization tab put the configuration panel in a QScrollArea
     with a MAXIMUM width (340 / 380 px) and the horizontal scroll bar switched OFF, while the content needed 600-900 px
     (long combo items, non-wrapping labels, tables, button rows): the overflow was clipped and the splitter stopped at
     the maximum width.
  2. A hard `setMinimumSize(1100, 700)`, large fixed minimum sizes of the views (420-440 px), the one-row playback bar
     (768 px) and wide input rows (Deadlock grid, Memory input bar) made the window minimum larger than small / scaled
     screens. Inspector and bottom areas added their full minimum heights (QSplitter sums minimums, even of collapsible
     children).
  3. QScrollArea::widgetResizable sizes content by its preferred height-for-width (wrapped labels), so pages scrolled
     although they would have fitted.
- Shared solution (`gui/PanelLayout.*`), used by all nine modules:
  - `ConfigPanel` / `FitScrollArea`: no width cap; content gets the viewport size and grows beyond it only when its
    real minimum (height for the current width) does not fit; scroll bars "as needed" in both directions.
  - `ui::makeResponsive`: QFormLayout rows wrap, fields grow, long labels wrap, combo boxes use a minimum content
    length with a full-width pop-up and a tooltip.
  - `PanelSplitter`: panel roles (configuration / view / inspector / detail), content-based default sizes capped by
    a share of the window, view never collapsible, side panels collapsible, visible handles with hover highlight,
    double-click = default layout, show / hide / focus view, sizes saved in QSettings and validated on restore.
  - `PageScroll`: every top-level tab page scrolls when the window is smaller than the page minimum (last-resort
    fallback; no page scrolls at 1024x768 or larger).
  - `PlaybackBar` wraps into two rows when narrow; view minimum sizes reduced (the painters already scale);
    `MainWindow` has no fixed minimum size and starts at 90% of the available screen; the event log starts small.
  - Scheduling, Deadlock, Synchronization and Memory > Page replacement were moved onto the same components
    (Memory > Page replacement: inputs are now a left panel, the page table an inspector).
  - View menu: Reset Panel Layout (Ctrl+Shift+R), Show Configuration Panel (Ctrl+Shift+C), Show Inspector Panel
    (Ctrl+Shift+I), Focus Simulation View (Ctrl+Shift+F), Event log.
- Verification:
  - `--layout-report WxH,...` (gui/LayoutAudit.*) shows every tab and Memory sub-tab and checks the real geometry of
    every visible control against all its clipping ancestors (reachable = inside, or reachable with an enabled
    scroll bar).
  - 0 problems at 1024x768, 1280x720, 1366x768, 1440x900, 1920x1080, 2560x1440 (100%); 1366x768 and 1920x1080 at
    125%; 1366x768 and 1920x1080 at 150%; 1920x1080 and 2560x1440 at 200% (QT_SCALE_FACTOR, logical size minus
    40 px for title bar / panel); font DPI 120 and 144; dark theme.
  - `--selftest` now has 90 layout checks (301 PASS in total): splitter handles dragged with real mouse events in both
    directions on 12 pages (configuration panel widens by 320 px, view shrinks, panel narrows again with every control
    still reachable, inspector resizes both ways, Reset Panel Layout restores the defaults), View menu hide / show /
    focus, 31 preset / algorithm / mode switches, dark theme, saved sizes restored, invalid saved sizes rejected.
  - The new tests found one more real clipping bug (Synchronization process cards capped at 250 px cut off their
    Step button) - fixed.
  - Screenshots: `docs/screenshots/layout/` (before / after, every module at 1366x768 and 1920x1080).
- Limitations: checked with offscreen renders, not on a physical monitor; at the smallest scaled sizes (for example
  1366x768 at 150%) whole pages scroll vertically by up to ~130 px; very narrow views draw smaller diagrams (some
  arrow labels overlap but nothing is clipped).

## Phase 3 (specification: OS_SIM_PHASE_3_INTEGRATED_MODULES.md; details: docs/PHASE3_MODULES.md)

### Stage 0: repository audit and baseline (done)
- Baseline before any Phase 3 change (commit d7a2532): `make -j2` 0 warnings, `ctest` 14/14 passed,
  `--selftest` ALL PASSED (301 checks), `--layout-report` 0 problems. No pre-existing failures.
- Existing coverage audit: CPU scheduling (6 algorithms), page replacement, contiguous allocation, paging,
  segmentation, demand paging, swapping, thrashing, Banker's / detection / recovery / RAG, synchronization
  (7 mechanisms), process lifecycle, IPC, I/O, threads, disk scheduling.
  Confirmed gaps: Multilevel Queue / Multilevel Feedback Queue scheduling, Readers-Writers, Dining Philosophers.
  Deadlock: RAG, Banker's safety/request (avoidance), detection and recovery exist; prevention is only explained
  (documented gap, no competing simulator added).
- Regression contract: the responsive panel layout (PanelLayout.h, layout audit, layout self-tests) is kept for
  every new page; new pages use ConfigPanel / PanelSplitter via assembleModule.

### Stages 1-2: Storage & Files - shared virtual filesystem, Explorer, Access Control, Block Allocation, Recovery Lab (done)
Done together because the Recovery Lab works on the same model (dependency, not a shortcut).
- `core/vfs.*` (pure): one `VirtualFs` = virtual disk blocks with bytes and generations, free-space bitmap, inodes,
  directory entries, users/groups, per-process descriptor tables, open-file table, deleted records; contiguous /
  linked / indexed allocation (all-or-nothing, rollback on ENOSPC/EFBIG); path resolution with search permission and
  symlink hop limit (ELOOP); owner/group/other precedence; chmod/chown/chgrp rules; hard links (not for directories);
  unlink lifetime (link count + open references); metadata recovery with generation tracking; OSF1 signature carving;
  restore into a NEW file avoiding evidence blocks; versioned text image with full validation; invariant checker.
- GUI: new parent tab **Storage & Files** (`gui/StorageTab.*`, `gui/StorageViews.*`) with File Explorer, Access
  Control, Block Allocation, Recovery Lab; all four read one `StorageArea::fs()` and refresh on `changed()`.
- Live Linux: `ossim_helper fsdemo` (open/write/lseek/read/fstat/link/readdir/unlink-while-open/close in a private
  mkdtemp directory), shown in Explorer > "Live Linux syscalls".
- Tests: `test_vfs` (210 checks), `test_native` extended; self-test: 26 storage checks + drag/resize on the 4 pages.

### Stage 3: Memory > Heap Laboratory (done)
- `core/heap.*` (pure): allocator over a bounded `std::vector<uint8_t>` arena; headers really stored in the arena
  (size, requested, magic, free-list link / handle); handles instead of raw pointers; malloc / calloc (overflow
  check, zeroing) / realloc (in place shrink/grow, move with copy, failure keeps the old block) / free (null free,
  double free and invalid handle detected); First / Best / Worst Fit over an address-ordered free list with a minimum
  split size and immediate coalescing; separate buddy allocator (power-of-two split / merge, buddy = offset XOR size);
  bounds-checked payload read/write; modelled overflow + integrity check (no real invalid access); metrics with
  headers and padding as overhead and the fragmentation indicator undefined when nothing is free; deterministic
  scripted sessions (playback, undo, new branch after Previous); 8 presets. Reuses `FitAlgo` names from memadv.
- GUI: `gui/HeapLab.*` added as the last Memory sub-tab "Heap Laboratory" (heap map with header/payload/padding/free,
  free-list arcs, realloc move arrow, change flash, buddy tree, zoom; inspector with payload bytes; history,
  integrity check, outstanding allocations = potential leaks).
- Tests: `test_heap` (107 checks); self-test: 10 heap checks + drag/resize on the page.

### Stage 4: Processes > System Monitor (done; Linux verified, Windows NOT verified)
- `core/sysmon.*` (pure): /proc/stat, /proc/meminfo, /proc/<pid>/stat (names with spaces / ')'), /proc/<pid>/status,
  /proc/diskstats parsers; timestamped deltas (total and per-core CPU %, process CPU % of ONE core, disk rates) with
  zero-interval and counter-reset handling; PID reuse detected by pid + start time; bounded MetricHistory;
  AlertMonitor with threshold, debounce and hysteresis.
- `engine/sysmon_native.*`: Linux /proc backend (no command lines); Windows backend (GetSystemTimes,
  GlobalMemoryStatusEx, Toolhelp32, GetProcessTimes, GetProcessMemoryInfo; psapi linked only on WIN32) - compiled only
  on Windows and NOT verified (no Windows machine available); other platforms report "unsupported".
- GUI: Processes became a sub-tab area (Process Lifecycle = the existing page, System Monitor). Sampling on a worker
  QThread, snapshots via queued signals; summary strip, history chart, per-core bars, sortable/filterable table,
  process tree, inspector with CPU/RSS explanations, alert log; sampling pauses while the page is hidden.
  Owned demo worker (`ossim_helper worker`): start, SIGSTOP/SIGCONT (POSIX), cooperative SIGTERM, confirmed kill;
  signals go only to the QProcess-owned child that is still running. Host processes are read-only.
- Tests: `test_sysmon` (59 checks: fixtures + Linux smoke test); self-test: 13 checks incl. suspend/resume/stop of the
  owned worker. Export contains no live process details.

### Stage 5: Processes > Job Control (done)
- `core/jobctl.*` (pure): one terminal session, jobs (job id separate from PID / PGID), one process group per job,
  exactly one foreground group, limited grammar (compute / sleep / readline / gen N | upper, &, jobs, fg, bg,
  kill -TERM|-KILL|-INT|-STOP|-CONT %n, wait, ctrl-z, ctrl-c, type, tick, until-prompt), SIGTSTP / SIGSTOP / SIGCONT /
  SIGINT / SIGTERM / SIGKILL / SIGTTIN semantics, job completion, refusal of bare PIDs and arbitrary commands. The
  pipeline REUSES the IPC pipe model (IpcSim in pipe mode: capacity, blocking, closed unused ends, EOF).
- GUI: Processes > Job Control: console + buttons, animated process-group view (foreground arrow moves, pipe buffer),
  job list, process tree, transcript, event timeline, playback with new branch after Previous. fg/bg are labelled
  SIMULATED; Live Linux tab runs `ossim_helper jobs` (setpgid, kill(-pgid, SIGSTOP/SIGCONT/SIGTERM) on its own
  children, waitpid WUNTRACED/WCONTINUED, 2-stage pipe pipeline, everything reaped; no tcsetpgrp / PTY).
- Tests: `test_jobctl` (98 checks), `test_native` (jobs demo); self-test: 10 job-control checks.

## Definition of done
- [x] Clean build with zero warnings, all tests green.
- [x] Every number of CLAUDE.md Section 6 is asserted in a test and passes.
- [x] Section 11 (synchronization extension): every algorithm simulated, tested, in the GUI and documented.
- [x] Phase 2: M1-M6 implemented with real models, tested, in the GUI and documented (docs/PHASE2_MODULES.md).
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
