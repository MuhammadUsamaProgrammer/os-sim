# OS-Sim: An Integrated Operating System Simulator for CPU Scheduling, Memory Management and Deadlock Detection with a GUI

*(extended with a Process Synchronization module: race conditions, Peterson's solution, mutex locks,
semaphores, producer-consumer and monitors)*

**Course:** Operating System Lab, BS (IT) semester 5, Bahria University
**Instructor:** Amarah Abdullah
**Group NBA:** Aagha Ali Hassan (03-135242-007), Hamza Malik (03-135242-021), Muhammad Usama (03-135242-038)

---

## 1. Introduction

Operating-system concepts such as CPU scheduling, paging and deadlocks are usually studied
separately, with small console programs that print a few numbers. That makes it hard to see how
the parts of an OS behave and how algorithms compare. OS-Sim is a single interactive Qt
application. The user creates processes, runs them through six schedulers, watches page
replacement frame by frame, and avoids, detects and recovers from deadlocks, all with the same
process list. An animated mode replays a schedule with real POSIX threads and synchronisation
primitives, and an integrated mode runs scheduling, paging and resource allocation together.
A fourth tab, **Synchronization**, shows why concurrent processes need synchronization: it executes
race conditions, Peterson's solution, mutex locks, semaphores, the producer-consumer problem and
monitors one instruction at a time, lets the user choose the interleaving, and runs the same
primitives with real POSIX threads.

## 2. Objectives

1. Implement four CPU-scheduling families (FCFS, SJF/SRTF, Round Robin, Priority NP/P) with a
   Gantt chart and average waiting, turnaround and response time.
2. Implement paged memory with FIFO, LRU and Optimal page replacement, reporting page faults and hit ratio.
3. Implement Banker's algorithm (avoidance), a detection algorithm that identifies deadlocked
   processes, and recovery by process termination.
4. Run each simulated process as a POSIX thread synchronised with mutexes, semaphores and a common clock.
5. Provide one tab-based GUI with side-by-side algorithm comparison and exportable results.
6. (Extension) Simulate process synchronization (race condition, Peterson, mutex, binary and
   counting semaphores, producer-consumer, monitors) step by step, compare the mechanisms, and
   demonstrate each primitive with real threads.

7. (Phase 2) Turn OS-Sim into a learning platform with six more modules: disk scheduling, process
   management, IPC, advanced memory management, I/O management (polling / interrupts / DMA) and
   multithreading models, each with deterministic simulations, animated views and Live Linux demos.

All seven objectives are met (see Section 7).

## 3. OS concepts used (concept → file → function)

| Concept | Where in the code | How it is used |
|---|---|---|
| Process / PCB | `core/pcb.h`: `struct PCB`, `enum ProcState`, `stateName()` | pid, arrival, burst, remaining, priority, pages, state, firstRun, finish, resource vectors |
| Process states | `core/scheduler.cpp`: `makeReady()`, `schedulerPick()`; `engine/engine.cpp`: `clockLoop()`, `procLoop()`; `core/integrated.cpp` | New → Ready (arrival), Ready → Running (dispatch), Running → Ready (preemption), Running → Waiting (resource denied), Waiting → Ready (granted), Running → Terminated |
| Process creation | `engine/engine.cpp`: `realProcessDemo()` | `fork()`, child `execlp("sleep","sleep","1")`, parent `waitpid()` and exit codes (console `engine --real`) |
| Threads | `engine/engine.cpp`: `SimulationEngine::start()`, `join()`, `clockMain()`, `procMain()` | one `pthread_create` per process plus one clock thread; `pthread_join` at shutdown |
| Mutex | `engine/engine.cpp`: `mtx_`; `engine/logger.cpp`: `PipeLogger::mtx_` | protects PCBs, ready queue, current tick, paused/stop flags; protects the collected log |
| Semaphores | `engine/engine.cpp`: `runSem_[i]`, `doneSem_` | `sem_init`, `sem_post(&runSem[i])` → process runs one tick → `sem_post(&doneSem)`; `sem_destroy` in the destructor |
| Condition variable | `engine/engine.cpp`: `cond_` in `clockLoop()`, `pause()`, `resume()`, `step()`, `setTickDelay()` | `pthread_cond_wait` while paused, `pthread_cond_timedwait` for the tick delay, `pthread_cond_broadcast` to wake |
| IPC (pipe) | `engine/logger.cpp`: `PipeLogger::logWrite()`, `drainLog()` | `pipe()`; every thread `write()`s one line (< PIPE_BUF, atomic); the dispatcher `read()`s the `O_NONBLOCK` end |
| CPU scheduling | `core/scheduler.cpp`: `runScheduler()`, `schedulerPick()`, `better()` | FCFS, SJF, SRTF, RR (quantum), Priority NP and P, idle segments, context switches |
| Paging | `core/memory.cpp`: `FrameAllocator::allocateEqual()`, `buildPageTable()` | equal frame allocation, page table entries (frame, valid bit) |
| Page replacement | `core/memory.cpp`: `FramePool::access()`, `chooseVictim()`, `runPageReplacement()` | FIFO, LRU, Optimal; faults, hits, hit ratio, evicted page |
| Deadlock avoidance | `core/deadlock.cpp`: `bankersSafety()`, `bankersRequest()` | Need = Max − Allocation, safety sequence + trace, request algorithm with roll-back |
| Deadlock detection | `core/deadlock.cpp`: `detectDeadlock()`, `buildRag()` | multi-instance detection algorithm; RAG with DFS cycle search |
| Deadlock recovery | `core/deadlock.cpp`: `recoverByTermination()` | kill the deadlocked process with the lowest total allocation, release, re-detect |
| Integration | `core/integrated.cpp`: `runIntegrated()` | scheduler + shared frame pool + scripted resource requests + periodic detection |
| Race condition / critical section | `core/sync.cpp`: `SyncSim::step()` (`Op::Read/Inc/Write`), `enterCS()`; `engine/sync_demo.cpp`: `raceBody()` | counter++ split into read, modify, write; lost updates and critical-section violations are counted; the real demo uses a separate atomic load and store (no undefined behaviour) |
| Peterson's solution | `core/sync.cpp`: `Op::SetInterested`, `Op::SetTurn`, `Op::PetersonWait`; `engine/sync_demo.cpp`: `petersonBody()` | lecture variant (`turn = process; while (interested[other] && turn == process);`), busy waiting; real version with `std::atomic` seq_cst |
| Mutex locks | `core/sync.cpp`: `Op::Lock/Unlock`, `release()` (FIFO hand-off); `engine/sync_demo.cpp`: `mutexBody()` | owner, waiting queue, `pthread_mutex_lock/unlock` with real threads |
| Binary / counting semaphores | `core/sync.cpp`: `Op::SemWait/SemSignal`; `engine/sync_demo.cpp`: `semBody()` | value, FIFO waiting queue, wake-up on signal; `sem_init`, `sem_wait`, `sem_post`, `sem_destroy` |
| Producer-consumer | `core/sync.cpp` (programs with `empty`, `full`, `mutex`); `engine/sync_demo.cpp`: `producerBody()`, `consumerBody()` | bounded buffer, blocking on full / empty, overflow and underflow checks |
| Monitors, condition variables | `core/sync.cpp`: `Op::MonEnter/MonExit/CondWhile/CondWait/CondSignal`; `engine/sync_demo.cpp`: `monitorInsert()`, `monitorRemove()` | Mesa semantics (signalled process re-enters through the entry queue and re-checks its while condition); `pthread_cond_wait/signal/broadcast` |
| Safe cancellation | `engine/sync_demo.cpp`: `SyncDemoRunner::cancel()`; `gui/SyncBridge.cpp` | flag + `sem_post` / `pthread_cond_broadcast` wakes every blocked thread, then `pthread_join` |
| Disk scheduling | `core/disk.cpp`: `runDisk()` | FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK; head moves with boundary / wrap kinds |
| Process lifecycle, PCB | `core/procmgmt.cpp`: `LifecycleSim::admit/dispatch/preempt/blockIO/completeIO/exitRunning/kill/tick`, `runAuto()` | validated 5-state transitions; automatic mode reuses `schedulerPick()` |
| fork / exec / wait / zombie / orphan / signals | `apps/ossim_helper.cpp`: `demoFork()`, `demoExec()`, `demoWait()`, `demoZombie()`, `demoOrphan()`, `demoSignal()` | real system calls on the helper's own children, bounded `waitpid`, subreaper |
| IPC | `core/ipc.cpp`: `IpcSim::apply()`; `apps/ossim_helper.cpp`: `demoShm()`, `demoMsg()`, `demoPipe()`, `demoFifo()` | shared memory, message queues (blocking / non-blocking), pipes / FIFOs with EOF rules |
| Contiguous allocation, paging, segmentation | `core/memadv.cpp`: `runAllocation()`, `translatePaging()`, `translateSegment()`, `baseLimitCheck()` | First / Best / Worst Fit, coalescing, compaction, page tables, segment tables, traps |
| Demand paging, thrashing | `core/memadv.cpp`: `runDemandPaging()` (uses `FramePool`), `runThrashing()`, `multiprogrammingCurve()` | page-fault steps, dirty write-back, working set, utilisation model |
| Polling, interrupts, DMA | `core/iomodel.cpp`: `runIo()`, `compareIo()` | logical-tick bus model, pending IRQ queue, DMA setup / completion, cycle stealing |
| Threading models | `core/threads.cpp`: `runThreads()`; `apps/ossim_helper.cpp`: `demoThreads()` | Many-to-One, One-to-One, Many-to-Many; real `pthread_create` / `pthread_join` with arguments and results |

## 4. System design

The design follows one rule: **`core/` calculates, `engine/` replays with threads, `gui/` only displays.**

```
 +---------------------------- gui/ (Qt 6 Widgets, executable "ossim") ----------------------------+
 |  MainWindow: tabs + event-log dock + menus (Load sample, Export report, About) + --selftest       |
 |  SchedulingTab  (GanttWidget, BarChartWidget, Integrated-run panel)                               |
 |  MemoryTab      (frame tables, BarChartWidget, page-table panel)                                  |
 |  DeadlockTab    (matrices, RagView)                                                               |
 |  SyncTab        (SyncView, CodeView, SyncTimeline, comparison, real demo page)                   |
 |  EngineBridge   (engine observer -> queued Qt signal -> GUI thread)                               |
 |  SyncBridge     (sync demo on a std::thread -> queued Qt signals -> GUI thread)                  |
 +-------------------+--------------------------------------------------+-------------------------+
                     | result structs                                   | events (queued signals)
                     v                                                  |
 +------------------ core/ (pure C++17, liboscore) ------------+   +---- engine/ (libosengine) ----+
 |  scheduler: runScheduler, runAll, schedulerPick             |   | SimulationEngine               |
 |  memory:    runPageReplacement, FramePool, FrameAllocator   |-->|   clock thread  (dispatcher)   |
 |  deadlock:  bankersSafety/Request, detectDeadlock,          |   |   N process threads            |
 |             recoverByTermination, buildRag                  |   |   mutex, sem_t, cond var       |
 |  integrated: runIntegrated                                  |   | PipeLogger (pipe IPC)          |
 |  sync:      SyncSim (one pseudocode line per step),         |   | realProcessDemo (fork/exec)    |
 |             syncScenarios, compareSync                      |   | SyncDemoRunner (real pthreads, |
 |  io_formats: CSV / matrix / requests loaders                |   |   mutex, sem_t, cond, atomics) |
 +-------------------------------------------------------------+   +--------------------------------+
            ^                                                               ^
            |                 apps/console_main.cpp (ossim_console)         |
            +---------------- tests/ (7 programs, ctest) -------------------+
```

Why `core/` has no threads: the algorithms must give exactly the textbook numbers on every run,
so they are pure functions of their input and easy to test. The engine then shows the real OS
primitives by replaying a timeline that `core/` computed. Its handshake serialises the threads,
so the log is identical on every run (checked 20 times in `test_engine`).

### Engine handshake (one tick)

```
clock thread                                   process thread i
------------                                   ----------------
lock(mtx); while paused: cond_wait(cond,mtx)
currentTick = t; New->Ready arrivals; pick pid
from the timeline; Running/Ready states; log
unlock(mtx)
sem_post(runSem[i])  ------------------------>  sem_wait(runSem[i])
                                                lock(mtx); remaining--; firstRun/finish;
                                                log "RUNNING"/"TERMINATED"; unlock(mtx)
sem_wait(doneSem)    <------------------------  sem_post(doneSem)
drainLog() from the pipe -> observer (EngineEvent)
cond_timedwait(tickDelay)   (stop/step wake it early)
```

Shutdown: `stop()` only ends the clock loop. The clock thread then sets `exitThreads_` and posts
every `runSem`, so each process thread wakes, sees the flag and returns. `join()` calls
`pthread_join` on all threads, and the destructor calls `sem_destroy`, `pthread_cond_destroy` and
`pthread_mutex_destroy`. A process thread therefore never exits while the clock waits on `doneSem`.

## 5. Implementation of each module

### 5.1 CPU scheduling (`core/scheduler.cpp`)
A single tick-based simulator serves all eight algorithms (FCFS, SJF, SRTF, LJF, LRTF, RR, Priority NP / P). In each tick:
(1) arriving processes are appended to the ready queue (same tick → lower pid first);
(2) `schedulerPick()` applies the preemption rule of the algorithm and chooses the process for this tick;
(3) the process runs one tick and the tick is appended to the Gantt timeline (adjacent ticks of the same pid are merged; idle ticks get `pid = -1`).

- FCFS key = arrival; SJF/SRTF key = remaining time; Priority key = priority number (lower is higher).
  Ties: earlier arrival, then lower pid.
- Non-preemptive algorithms keep the running process. SRTF and Priority (P) re-compete every tick.
  RR preempts when the quantum expires. Arrivals are queued first, so a process arriving at the
  expiry tick goes before the preempted one.
- Metrics: TAT = completion − arrival, WT = TAT − burst, RT = first run − arrival; context switches =
  number of pid changes in the timeline (idle ignored); CPU utilisation = busy ticks / total ticks.
- Validation throws `std::invalid_argument` (empty list, burst ≤ 0, negative arrival/pages, duplicate pid, quantum ≤ 0 for RR).

**LJF and LRTF (added later; same simulator, no second engine).** Both reuse `schedulerPick()`; only
the comparison key and one preemption rule are new. The enum values `Algo::LJF` and `Algo::LRTF`
were appended, so the old values (FCFS = 0 … PRIORITY_P = 5) are unchanged; `allAlgos()` gives the
display order FCFS, SJF, SRTF, LJF, LRTF, RR, Priority (NP), Priority (P). GUI combo boxes store the
enum value as item data (never the index).

| | LJF (non-preemptive) | LRTF (preemptive) |
|---|---|---|
| Key | longest **burst** among the ready processes | longest **remaining** time, every tick |
| Preemption | never: runs until it terminates (or blocks in the integrated run) | only if a ready process has **strictly** more remaining time |
| Tie when the CPU is free | earlier arrival, then lower pid | earlier arrival, then lower pid |
| Tie with the running process | – | the running process **keeps** the CPU (no needless switch) |
| Mirror of | SJF | SRTF |

In `better()` the "longest" keys are negated (`-burst`, `-remaining`) so that the same "smaller key
wins → earlier arrival → lower pid" rule applies. In `schedulerPick()` LRTF only preempts when
`procs[r].remaining > procs[running].remaining` for some ready `r`. Because the running process keeps
the CPU on a tie, LRTF alternates between processes whose remaining times become equal: one tick
later the other one is strictly longer and takes over, so LRTF causes many context switches and every
process finishes near the end. Both algorithms favour long jobs: a short job can starve (see the
`processes_ljf_starvation.csv` sample), which is exactly the opposite problem to SJF / SRTF, where
long jobs can starve. They are rarely used in practice and serve as a contrast in the course.

Hand trace of LRTF on `processes_ljf_lrtf.csv` (P1 0 8, P2 1 4, P3 2 9, P4 3 5):
`P1 0–2, P3 2–6, P1 6–8, P3 8–9, P4 9–11, P1 11–12, P2 12–13, P3 13–15, P1 15–16, P2 16–17,
P4 17–19, P1 19–20, P2 20–21, P3 21–23, P1 23–24, P2 24–25, P4 25–26` – completions 24 / 25 / 23 / 26,
WT 16 / 20 / 12 / 18 (avg 16.5), RT 0 / 11 / 0 / 6 (avg 4.25), 16 context switches.
(t=2: P3 has 9 > 6 and preempts P1; t=5: P3 and P1 both 6 → P3 continues; t=6: P1 6 > 5 → P1.)

### 5.2 Memory (`core/memory.cpp`)
`FramePool` holds the frames and the bookkeeping needed by the replacement policy: load order
(FIFO and ties) and last use (LRU). Optimal asks a callback for the next use of each resident page.
On a reference: hit if resident; otherwise a fault fills the first empty frame (0, 1, 2…) or replaces
a victim. The victim is the oldest loaded (FIFO), the least recently used (LRU), or the page used
furthest in the future, where never-used-again counts as infinitely far (Optimal). Remaining ties
go to the earliest-loaded page. `runPageReplacement()` records the frame contents, fault flag and
evicted page after every reference. `parseRefString()` accepts commas and/or spaces and rejects bad
tokens and negatives. `FrameAllocator::allocateEqual()` gives each process `total / n` consecutive
frames, and `buildPageTable()` marks the first pages of a process as valid in its frames.

### 5.3 Deadlocks (`core/deadlock.cpp`)
- `bankersSafety()`: Need = Max − Allocation; Work = Available. Passes scan P0…Pn−1; an unfinished
  process with Need ≤ Work is taken immediately (Work += its Allocation) and the same pass continues.
  The scan stops when a full pass makes no progress. Safe ⇔ all processes finished. Each step's
  Work before/after is recorded for the trace.
- `bankersRequest()`: request ≤ Need ("exceeds maximum claim" otherwise), request ≤ Available ("must
  wait" otherwise), then pretend-allocate, run the safety algorithm, and roll back if unsafe.
- `detectDeadlock()`: the same pass scan with the Request matrix instead of Need. Processes that
  hold nothing are finished from the start, and processes left unfinished are deadlocked.
- `recoverByTermination()`: while deadlocked, kill the deadlocked process with the lowest total
  allocation (tie → lowest pid), release its resources, detect again.
- `buildRag()`: assignment edges resource → process and request edges process → resource. A DFS
  with three colours finds one cycle. With multi-instance resources a cycle is only *necessary*, so
  the detection algorithm decides who is deadlocked.

### 5.4 Engine and IPC (`engine/`)
See Section 4. `PipeLogger` writes `[t=3] P2 RUNNING remaining=1` style lines into a pipe. The
clock thread, the only dispatcher, drains the pipe after every tick, parses each line into an
`EngineEvent {tick, pid, state, text}` and calls the observer outside the mutex. `realProcessDemo(n)`
forks n children that `exec` `sleep 1` and are reaped with `waitpid`. It is never run by the tests.

### 5.5 Integrated run (`core/integrated.cpp`)
One tick loop combines the modules:
1. arrivals;
2. Waiting processes retry their pending request;
3. `schedulerPick()` (the same function as `runScheduler`) chooses a process. If the process must
   first make a scripted request and the request is denied, it becomes Waiting and the dispatcher
   picks again;
4. the running process references its next page in one shared `FramePool` (key = pid·1000 + page,
   reference strings from `std::mt19937(pid)` with 70% locality) and runs one tick;
5. every K ticks `detectDeadlock()` runs over the blocked requests and, if enabled,
   `recoverByTermination()` kills victims. Terminated or killed processes release their resources and frames.

With avoidance ON, requests go through `bankersRequest()`. With avoidance OFF they are granted
whenever resources are available, which is how a real deadlock can occur. Each process's maximum
claim is the sum of its scripted requests.

### 5.6 GUI (`gui/`)
`MainWindow` (tabs, monospace event-log dock, menus, status bar, About, export, `--selftest`),
`SchedulingTab`, `MemoryTab`, `DeadlockTab`, `GanttWidget` and `BarChartWidget` (custom
`paintEvent`), `RagView` (`QGraphicsScene`), and `EngineBridge`. The bridge turns the engine's
observer callback into a signal delivered with a queued connection to the GUI thread, so worker
threads never touch widgets. Each run has a generation number so stale queued events are ignored.
Every input is validated and errors are shown with `QMessageBox::warning`.

**Built-in samples (Load Sample).** `gui/SampleRegistry.*` is the one catalogue of all built-in
samples: each `SampleDef` has a stable id, name, description, module id (the Module Visibility
Manager id, e.g. `memory.heap`), category, mode, parameters, level, backing file and a loading
handler (plus an optional run handler for *Load && Run*). `gui/SampleCatalog.cpp`
(`MainWindow::registerSamples`) fills it with adapters over the EXISTING providers – core preset
lists (`lifecyclePresets()`, `syncScenarios()`, `heapPresets()`, …), the tabs' own presets
(`loadPreset(i)`) and the files in `samples/` – so no workload is defined twice. `SampleRegistry::load()`
asks a guard (stop / pause a running simulation; confirm before replacing the shared Storage disk),
calls the handler and emits `loaded()` once. `SampleButton` (ModuleKit) is the shared accent button;
`assembleModule()` reserves a "sample slot" at the top of every configuration panel, so all 24 module
pages get the button in the same place. The File menu uses the same registry
(`File > Load Built-in Sample`) and keeps the original file list and a new `Open File...` importer.
The `samples/` folder is embedded as Qt resources (`:/samples`); `ui::samplesDir()` uses a folder on
disk when one exists and otherwise an extracted copy, so the samples work from any directory. The full
inventory (180 samples) is in `docs/SAMPLES.md`.

### 5.8 Phase 2 modules (details and reference tables: `docs/PHASE2_MODULES.md`)
Each module follows the same design: a pure model in `core/` returns a complete trace of snapshots (or events);
the GUI tab shows snapshot *k* of the trace with the shared `PlaybackBar` (Run, Pause/Resume, Prev, Next, Stop,
Reset, speed, scrubber), so Prev simply restores an earlier snapshot. Views (`DiskViews`, `LifecycleViews`,
`IoViews`, `MemAdvViews`, `IpcView`, `ThreadMapView`) only paint the snapshot and animate the change from the
previous one with `QVariantAnimation`. Live Linux demos run in `ossim_helper`, a small single-threaded program the
GUI starts with `QProcess` (`NativeRunner`): it puts itself in its own process group so Stop (SIGTERM, then SIGKILL
after 1.5 s) reaches only its own children; every wait is bounded; System V objects are created with IPC_PRIVATE and
removed; FIFOs live in a private `mkdtemp` directory. Each event is one line `T= ROLE= PID= PPID= EVENT= | detail`,
parsed by the GUI and by `test_native`.

### 5.7 Process synchronization (`core/sync.cpp`, `engine/sync_demo.cpp`, `gui/Sync*`)
**Simulation (`core/`).** Every process runs a small program of pseudocode lines (`Instr`: operation,
synchronization object, text, enter/leave critical-section flags). `SyncSim::step(pid)` executes exactly
one line of one process, so any interleaving can be reproduced: by the user (the Step button on a process
card), by a scenario script, or by a deterministic scheduler (round robin with *k* instructions per turn,
or `std::mt19937` with a fixed seed). Shared memory (counter, `interested[]`, `turn`, buffer, in/out/count)
is plain data inside `SyncSim`; because one line runs at a time there is no real data race - the race is
modelled by the interleaving. Objects (`SyncObject`) are mutexes (owner + FIFO queue), semaphores
(value + FIFO queue), a monitor (owner + entry queue) and condition variables (queue). A blocked process
uses no CPU; a Peterson process that fails its while test stays runnable and is counted as a busy-wait step.
Entering a critical section that is already full is counted as a violation. `stats()` gives expected vs
actual, lost updates (= increments written - counter), violations, maximum occupancy, entries, busy-wait
and blocked steps, overflows/underflows and completion. `compareSync()` runs the same two-process counter
workload unsynchronized, with Peterson, with a mutex and with a binary semaphore.

**Real threads (`engine/`).** `SyncDemoRunner::run()` creates one `pthread` per worker, runs the chosen
primitive, joins all threads and checks the result (exact counter, at most one / S threads inside, every
item consumed exactly once, checksums, no overflow/underflow). The race demo uses an atomic load followed by
a separate atomic store, so updates are lost without undefined behaviour. Peterson uses `std::atomic` with
the default sequentially consistent ordering (plus `sched_yield()` while spinning). Worker log lines go
through `PipeLogger` (pipe IPC); the thread that called `run()` is the dispatcher. `cancel()` sets a flag
and wakes every sleeper (`sem_post` with a hand-on cascade, `pthread_cond_broadcast` under the monitor lock),
so a cancelled run always joins.

**GUI.** `SyncTab` drives the simulation with a `QTimer` (one instruction per tick, so the GUI never blocks)
and shows: process cards (state badge, next line, register/item, queue, per-card Step), `SyncView` (animated
chips moving between entry lane, busy-wait lane, critical section, finished lane and the queue of the object
they sleep on; bounded buffer slots with in/out), `CodeView` (pseudocode with the next line of each process
and the line just executed), a live variable table (changed values highlighted), `SyncTimeline`, the event
log, statistics with *theory vs observed*, the comparison page (table + bar chart) and the real-demo page.
`SyncBridge` runs the real demo on a `std::thread` and forwards lines with queued signals.

## 6. Testing

`ctest` runs seven programs. All checks pass with zero compiler warnings (`-Wall -Wextra`):

```
test_scheduler: 54 checks, 0 failures
test_memory: 26 checks, 0 failures
test_deadlock: 38 checks, 0 failures
test_engine: 34 checks, 0 failures
test_integrated: 44 checks, 0 failures
test_sync: 1068 checks, 0 failures
test_sync_engine: 72 checks, 0 failures
100% tests passed, 0 tests failed out of 7
```

### 6.1 Scheduler acceptance tests

| Case | Input | Expected | Result |
|---|---|---|---|
| FCFS | P1 0 24, P2 0 3, P3 0 3 | WT 0, 24, 27; avg WT 17; avg TAT 27; P1 0–24, P2 24–27, P3 27–30 | pass |
| SJF | same | order P2, P3, P1; WT 6, 0, 3; avg WT 3 | pass |
| RR q=4 | same | WT 6, 4, 7; avg WT 5.6667 | pass |
| SRTF | P1 0 8, P2 1 4, P3 2 9, P4 3 5 | avg WT 6.5 | pass |
| SJF (NP) | same | avg WT 7.75 | pass |
| Idle gap | P1 0 2, P2 5 2 (FCFS) | P1 0–2, idle 2–5, P2 5–7 | pass |
| Priority NP | P1 10/3, P2 1/1, P3 2/4, P4 1/5, P5 5/2 | order P2, P5, P1, P3, P4; avg WT 8.2 | pass |
| Validation | burst 0, duplicate pid | `std::invalid_argument` | pass |

Extra checks: preemptive priority, the RR arrival-at-expiry rule, negative arrival, quantum 0, CSV loading.

### 6.1b LJF and LRTF tests (`test_ljf_lrtf`, 68 checks, all hand-derived)

| Case | Input | Expected | Result |
|---|---|---|---|
| LJF, simultaneous arrivals, equal bursts | P1 0 3, P2 0 6, P3 0 4, P4 0 6 | P2 0–6, P4 6–12, P3 12–16, P1 16–19; WT 16/0/12/6, avg 8.5; avg TAT 13.25; 3 switches | pass |
| LJF, different arrivals | P1 0 8, P2 1 4, P3 2 9, P4 3 5 | P1 0–8, P3 8–17, P4 17–22, P2 22–26; avg WT 10.25, avg TAT 16.75 | pass |
| LJF tie by arrival | P1 0 2, P5 1 4, P3 2 4 | P5 (earlier arrival) before P3 although its pid is higher | pass |
| LJF starvation | P1 0 10, P2 1 1, P3 2 9, P4 3 8 | P2 runs last, waits 26 (SJF: 9) | pass |
| Idle gap (both) | P1 0 2, P2 5 3 | P1 0–2, idle 2–5, P2 5–8; 1 switch; CPU 62.5 % | pass |
| LRTF preemption | P1 0 3, P2 1 6 | P1 0–1, P2 1–6, P1 6–8, P2 8–9; WT 5 / 2; 3 switches | pass |
| LRTF equal remaining | P1 0 2, P2 0 2 | P1 0–1, P2 1–3 (P2 keeps the CPU at t=2), P1 3–4 | pass |
| LRTF ties (sample file) | P1 0 3, P2 0 6, P3 0 4, P4 0 6 | 14 segments (P2 0–1, P4 1–3, …, P3 18–19), 13 switches, avg WT 12.75 | pass |
| LRTF, different arrivals | P1 0 8, P2 1 4, P3 2 9, P4 3 5 | 17 segments (trace in 5.1), avg WT 16.5, TAT 23, RT 4.25, 16 switches | pass |
| Determinism, validation | | same result twice; burst 0, duplicate pid, negative arrival, empty list throw; quantum ignored | pass |
| Compare all | | 8 results; LJF 10.25, LRTF 16.5, SRTF still 6.5 | pass |
| Animated replay | threaded engine | finish times equal core completion times for LJF and LRTF | pass |
| Integrated run | no resource requests | completion times equal `runScheduler`; 26 memory references | pass |

`test_procmgmt` and `test_integrated` loop over `allAlgos()`, so they now also cover LJF and LRTF
(lifecycle simulation = scheduler completion times; every page algorithm × every scheduler).

### 6.2 Memory acceptance tests

| Case | Expected | Result |
|---|---|---|
| 7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1, 3 frames | FIFO 15, LRU 12, Optimal 9 faults | pass |
| Belady 1,2,3,4,1,2,5,1,2,3,4,5 FIFO | 9 faults (3 frames), 10 faults (4 frames) | pass |
| LRU hit ratio, standard string | hits 8, ratio 0.4 | pass |
| `parseRefString("1, 2 3,4")` / `("1,a")` | {1,2,3,4} / throws | pass |

### 6.3 Deadlock acceptance tests

| Case | Expected | Result |
|---|---|---|
| Banker's safety (A=10,B=5,C=7) | safe; Need P0 743, P1 122, P2 600, P3 011, P4 431; sequence P1, P3, P4, P0, P2 | pass |
| Request P1 (1,0,2) | granted | pass |
| then P4 (3,3,0) | denied (exceeds available) | pass |
| then P0 (0,2,0) | denied (unsafe) | pass |
| P1 (5,0,0) | denied, "exceeds maximum claim" | pass |
| Detection (A=7,B=2,C=6) | no deadlock; finish order P0, P2, P3, P4, P1 | pass |
| P2 request 0 0 1 | deadlocked {P1, P2, P3, P4} | pass |
| Recovery | kill list non-empty (P1), deadlock cleared | pass |

### 6.4 Engine tests

| Case | Result |
|---|---|
| FCFS replay: finish times 24, 27, 30 equal `core/` completion times; one `TERMINATED` line per process | pass |
| 20 runs: logs byte-identical | pass |
| SRTF, RR, SJF replays match `core/` | pass |
| start paused → 3 × step → resume → pause → stop under a 5 s `alarm()` watchdog | pass, no hang |

### 6.5 Integrated run tests
With `processes_srtf.csv`, RR q=2, 6 frames LRU and `requests_integrated.csv`: avoidance ON →
0 deadlocks, 0 kills, all 4 processes finish. Avoidance OFF → deadlock `P1,P2` detected and P1 killed;
the others finish. Without auto-recover the run stops in the deadlock. The logs are identical over
10 runs, and all 6 schedulers × 3 page algorithms complete with 26 memory references.

### 6.6 Synchronization tests (`test_sync`, `test_sync_engine`)

| Case | Expected | Result |
|---|---|---|
| Race, order P0 R, P1 R, P0 I, P0 W, P1 I, P1 W | counter 1, expected 2, 1 lost update, 1 violation | pass |
| Race, serial order / 3 instructions per turn | counter correct, nothing lost | pass |
| Race, 3 processes x 3, 1 instruction per turn | updates lost; identical log on every run | pass |
| Peterson, mutex, binary semaphore, 1-5 processes, slices 1-4, 30 random seeds | counter = n x iterations, 0 violations, completed | pass |
| Peterson, **all reachable states** (breadth-first search, 2 x 2 iterations, 1211 states) | never 2 in the critical section, never stuck, final counter 4 | pass |
| Peterson progress (P0 alone) / bounded waiting (200 random runs) | P0 never waits / the other enters at most once while one waits | pass |
| Mutex ownership | owner, FIFO queue, hand-off to the first waiter, free at the end | pass |
| Binary semaphore / counting semaphore S = 2 with 5 processes | value in [0, 1] / [0, 2]; at most 1 / 2 inside; all permits returned | pass |
| Producer-consumer and monitor, fast producer / fast consumer / 40 random runs | producer blocks on full, consumer blocks on empty, no overflow/underflow, every item consumed | pass |
| Monitor | wait releases the monitor, signal moves the waiter to the entry queue, it re-checks `while` | pass |
| Comparison | unsynchronized loses updates; Peterson busy-waits; mutex sleeps; others correct | pass |
| Validation, reset, repeated runs | bad settings throw `std::invalid_argument`; a reset run gives the identical log | pass |
| Real threads: mutex, binary semaphore, Peterson (seq_cst) | exact counter, max 1 thread inside | pass |
| Real threads: counting semaphore, producer-consumer, monitor | at most S inside; 120 of 120 items, equal checksums | pass |
| Cancellation while threads are blocked (6 primitives x 2), then run again | `run()` returns (watchdog), runner reusable | pass |

### 6.7 Phase 2 tests

| Program | Checks | What is asserted (real results) |
|---|---|---|
| `test_disk` | 487 | FCFS 640, SSTF 236, SCAN 331, C-SCAN 382, LOOK 299, C-LOOK 322 with the exact service orders; reverse direction (SCAN 236, LOOK 208, C-SCAN 386, C-LOOK 326); duplicates, head / boundary requests, one side, single / empty queue, ties; 50 random workloads serviced exactly once |
| `test_procmgmt` | 62 | reference event sequence; queue / state invariants after every event; finish times equal `runScheduler()` for all 6 policies; idle CPU; tree; every refused transition; branch after Prev |
| `test_io` | 49 | hand-traced CPU sequences `PUPUPCPC`, `UUUICUIC`, `SSUUUUUD` (latency 8 each); exact transfer; accounting; small vs bulk; slow device; cycle stealing; back-pressure |
| `test_memadv` | 108 | 426 fails with First / Worst Fit, fits with Best Fit; coalescing; compaction; internal waste; 2500 -> 5572; traps; segmentation 4250 / fault; demand paging 10 / 9 / 7 and identical frames to `runPageReplacement`; write-back; swapping; thrashing 20 vs 4 faults |
| `test_ipc` | 52 | HELLO; FIFO order; capacity; blocking vs non-blocking; wake-ups; EOF / EPIPE; removal; replay |
| `test_threads` | 35 | 12 / 4 / 6 ticks; context / core exclusivity every tick; no lost or duplicated work; blocking 15 vs 7; join order; migrations |
| `test_native` | 64 | real fork / exec (same PID) / wait / zombie (`Z`) / orphan (subreaper) / signal / shm (HELLO) / msg (ENOMSG, FIFO) / pipe (EOF) / FIFO / pthreads (arguments and results), twice each |

### 6.7b Phase 3 tests (details: `docs/PHASE3_MODULES.md`)

| Program | Checks | What is asserted (real results) |
|---|---|---|
| `test_vfs` | 214 | create / read / write / rename / copy / rmdir under all 3 strategies; permission precedence (owner `---` denied even with group `rwx`); directory search vs list (711 / 744); chmod / chown / chgrp rules; hard / symbolic / dangling links, ELOOP; descriptor offsets, `dup`, per-process tables, unlink while open; ENOSPC / EFBIG rollback; contiguous preset fails "largest run is 12" while linked / indexed succeed; recovery fixtures intact (verified bytes), partial (86 of 150 bytes), full overwrite, reuse-and-free, carving (unknown vs verified); restore never overwrites, preview on ENOSPC; image round trip and rejection of corrupt images; invariants after every scenario |
| `test_heap` | 107 | aligned payloads (8 / 16 / 32), no overlap, accounting identity; split threshold; coalescing with both neighbours; First / Best / Worst Fit pick @16 / @352 / @720; fragmentation 768 free, largest 320, request fails; calloc zeroing and overflow; realloc in place / move with preserved data / failure keeps old; double / invalid free, bounds, leaks; buddy split chain, buddy = XOR, full merge; modelled corruption detected |
| `test_sysmon` | 59 | /proc fixtures (names with `)`, missing fields, partitions skipped); CPU 50 % / per core 100 % and 0 %; process 50 % and 300 % of one core; zero interval and counter reset invalid; vanished processes; PID reuse; debounce (no alert storm) and hysteresis; bounded history; Linux smoke test sees its own PID |
| `test_jobctl` | 98 | job id vs PID / PGID; background -> stop -> resume -> fg -> complete; Ctrl-Z / Ctrl-C; one foreground group in every snapshot; SIGTTIN; pipeline order `ABCDEF`, writer blocks on a full pipe, EOF; refused PIDs / commands; wait on a stopped job refused |
| `test_rtsched` | 79 | A(2,5) B(4,7) H=35: U = 34/35, RM preemption at 5 and miss at 7 (B done at 8), EDF no miss; deadline equality on time; idle / offsets; RM and EDF ties; horizon semantics; overload with continue / abort (each miss counted once); no duplicate execution; validation |
| `test_mlq` | 26 | MLQ trace and average waiting 3.4; MLFQ trace (P1 demoted twice, done at 19); preemption by higher queues; priority boost; clamping; idle gap; validation |
| `test_sync_classic` | 473 | readers share and the writer waits; 150 random readers-writers runs without violation; naive philosophers deadlock (every chopstick held, one waiter each); asymmetric order completes; 150 random runs each |

### 6.7c Sample loading tests (GUI self-test section "samples", ctest `gui_samples_anywhere`)
- every one of the 24 leaf modules has samples and a visible, labelled **Load Sample** button with
  icon and tooltip, not clipped; its menu offers only that module's samples;
- all 180 samples load (one `loaded()` notification each); 145 samples with a run action execute after loading;
- correct algorithm / mode: LRTF sample → avg WT 16.5 and 16 switches, LJF → 10.25, FIFO standard
  string → 15 faults, Belady 4 frames → 10, unsafe Banker state → UNSAFE, RAG cycle → P0, P1
  deadlocked, disk SSTF → 236 cylinders; loading never runs (stale results are cleared);
- repeated loading does not duplicate processes; loaded parameters can be edited and re-run;
- a running animated replay is stopped and a running playback paused before a load;
- File menu: `Load sample` (files), `Load Built-in Sample` (all 180 entries), `Open File...`, export;
- Module Visibility Manager: a hidden module keeps its samples, loading never changes the workspace,
  the restored module shows its button and the loaded sample;
- light and dark theme: button text contrast ≥ 4.5 : 1 (measured 4.88 and 5.08);
- 1024×768 and 900×650: no button is clipped; `--layout-report 1366x768,1024x768`: 0 problems;
- `gui_samples_anywhere`: `ossim --sample-report --load-all` from `build/gui` with
  `OSSIM_FORCE_EMBEDDED_SAMPLES=1` (only the embedded copy) loads every sample.

### 6.8 Sanitizers and GUI smoke test
- `-DSANITIZE=thread`: all test programs pass (including the real synchronization demos), no ThreadSanitizer report.
- `-DSANITIZE=address` (address + undefined): all tests pass and the whole GUI `--selftest` exits 0 with no report.
- `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` loads every sample, runs every action
  (including the animated engine run, recovery, integrated run and export) and the invalid-input
  cases, and exits with code 0. It also runs every synchronization algorithm and scenario, the manual
  lost-update interleaving, Peterson busy waiting, producer blocking, Run/Pause/Resume/Stop/Reset,
  the comparison, and real POSIX demos including a cancelled one.

## 7. Results and algorithm comparison (real output)

### 7.1 Scheduling (quantum 4)

**processes_fcfs.csv** (24/3/3, all arrive at 0)

| Algorithm | Avg WT | Avg TAT | Avg RT | Context switches |
|---|---|---|---|---|
| FCFS | 17.00 | 27.00 | 17.00 | 2 |
| SJF | 3.00 | 13.00 | 3.00 | 2 |
| SRTF | 3.00 | 13.00 | 3.00 | 2 |
| LJF | 17.00 | 27.00 | 17.00 | 2 |
| LRTF | 19.00 | 29.00 | 15.00 | 6 |
| Round Robin q=4 | 5.67 | 15.67 | 3.67 | 3 |
| Priority (NP) | 3.00 | 13.00 | 3.00 | 2 |
| Priority (P) | 3.00 | 13.00 | 3.00 | 2 |

**processes_srtf.csv** (8/4/9/5 arriving at 0,1,2,3)

| Algorithm | Avg WT | Avg TAT | Avg RT | Context switches |
|---|---|---|---|---|
| FCFS | 8.75 | 15.25 | 8.75 | 3 |
| SJF | 7.75 | 14.25 | 7.75 | 3 |
| SRTF | **6.50** | **13.00** | **4.25** | 4 |
| LJF | 10.25 | 16.75 | 10.25 | 3 |
| LRTF | 16.50 | 23.00 | **4.25** | 16 |
| Round Robin q=4 | 11.75 | 18.25 | 4.50 | 7 |
| Priority (NP) | 7.75 | 14.25 | 7.75 | 3 |
| Priority (P) | 7.00 | 13.50 | 6.00 | 4 |

**processes_priority.csv**

| Algorithm | Avg WT | Avg TAT | Avg RT | Context switches |
|---|---|---|---|---|
| FCFS | 9.60 | 13.40 | 9.60 | 4 |
| SJF | 3.20 | 7.00 | 3.20 | 4 |
| SRTF | 3.20 | 7.00 | 3.20 | 4 |
| LJF | 12.00 | 15.80 | 12.00 | 4 |
| LRTF | 13.20 | 17.00 | 10.40 | 9 |
| Round Robin q=4 | 7.40 | 11.20 | 4.80 | 7 |
| Priority (NP) | 8.20 | 12.00 | 8.20 | 4 |
| Priority (P) | 8.20 | 12.00 | 8.20 | 4 |

Observations:
- SRTF gives the lowest average waiting and turnaround time, because it always runs the shortest remaining job.
- Among the six classic algorithms, Round Robin has the most context switches but a good response time, because every process gets the CPU early (LRTF switches even more).
- FCFS suffers from the convoy effect: one long job (P1 = 24) makes the short ones wait (avg WT 17).
- Priority scheduling optimises for priority, not waiting time (8.2 versus 3.2 for SJF on the same input).
  A low-priority process can starve.
- LRTF has the highest average waiting time on every sample, and LJF is among the worst (it ties
  FCFS on processes_fcfs.csv; Round Robin q=4 is even worse than LJF on processes_srtf.csv, 11.75
  versus 10.25). They do the opposite of SJF / SRTF. LRTF also has by far the most context switches
  (16 on processes_srtf.csv), because processes with equal remaining time take turns tick by tick;
  its response time can still be good (4.25) since a newly arrived long job runs at once.
- `processes_ljf_starvation.csv` (P1 10, P2 1, P3 9, P4 8): under LJF the 1-tick job waits 26 ticks
  (avg WT 12.5); SJF 8.5, SRTF 6.5, LRTF 18.0 with 18 context switches.

### 7.2 Page replacement (3 frames)

| Reference string | FIFO | LRU | Optimal |
|---|---|---|---|
| standard (20 refs) faults / hit ratio | 15 / 0.250 | 12 / 0.400 | 9 / 0.550 |
| Belady (12 refs) faults | 9 | 10 | 7 |

FIFO on the Belady string: 3 frames → 9 faults, 4 frames → 10 faults (Belady's anomaly). With 1…6
frames FIFO gives 12, 12, 9, 10, 5, 5 faults. Optimal is a lower bound, and LRU is the best of the
practical algorithms here.

### 7.3 Deadlocks
- Banker's sample: SAFE `<P1, P3, P4, P0, P2>`. After granting P1 (1 0 2), P4 (3 3 0) is denied
  (exceeds available) and P0 (0 2 0) is denied (unsafe).
- Detection sample: P1, P2, P3, P4 deadlocked. Recovery kills P1 (releases 2 0 0) and the deadlock is cleared.

### 7.4 Integrated run (processes_srtf.csv, RR q=2, 6 frames LRU, requests_integrated.csv)

| Mode | Deadlocks detected | Killed | Requests granted / denied | Memory faults / hits | Avg WT (completed) |
|---|---|---|---|---|---|
| Avoidance ON (Banker's) | 0 | none | 6 / 1 | 12 / 14 | 14.50 |
| Avoidance OFF + detection every 5 ticks + auto-recover | 1 (at t=9: P1, P2) | P1 | 5 / 2 | 10 / 10 | 9.00 |

Avoidance costs waiting time: P2 waits from t=3 to t=19 because granting B would make the state
unsafe. Without avoidance the system deadlocks and recovers only by killing P1, which loses P1's work.

### 7.5 Process synchronization (`ossim_console sync --real`)

Comparison, 2 processes x 3 increments, round robin, 1 instruction per turn:

| Mechanism | Expected | Actual | Lost updates | CS violations | Waiting steps (busy / blocked) | CS entries |
|---|---|---|---|---|---|---|
| Unsynchronized | 6 | 3 | 3 | 3 | 0 (0 / 0) | 6 |
| Peterson | 6 | 6 | 0 | 0 | 8 (8 / 0) | 6 |
| Mutex lock | 6 | 6 | 0 | 0 | 16 (0 / 16) | 6 |
| Binary semaphore | 6 | 6 | 0 | 0 | 16 (0 / 16) | 6 |

Peterson waits by spinning (the CPU is wasted); the mutex and the semaphore put the waiter to sleep
(no CPU used, but more steps counted while asleep). The unsynchronized version is faster but wrong.

Sample scenarios: lost update -> counter 1 of 2; 3 processes x 3 increments with fine-grained switching
-> counter 3 of 9 (6 lost); every Peterson, mutex, semaphore, producer-consumer and monitor scenario
finishes with the expected result and 0 violations (e.g. 5 processes sharing 2 printers: never more than
2 inside; fast producer: the producer sleeps on `empty` while the buffer is full).

Real threads (one run on the 2-core VM; the interleaving differs between runs): split atomic
load/store with 4 threads x 20000 lost 8202 of 80000 updates; Peterson 10000 of 10000; `pthread_mutex`
and `sem_t` 80000 of 80000 with at most one thread inside; counting semaphore at most 2 at once;
producer-consumer and monitor 20 of 20 items with equal checksums.

### 7.6 Phase 2 results (real output)
- Disk, reference queue: FCFS 640, SSTF 236, SCAN 331, C-SCAN 382, LOOK 299, C-LOOK 322 cylinders.
- I/O, common window: small transfer - DMA overhead 5 ticks vs interrupt 2 (DMA loses); bulk 32 units - useful CPU
  ticks polling 23, interrupt 1, DMA 94 of 97; cycle stealing - 32 stalls (one per unit).
- Memory: demand paging 10 / 9 / 7 faults (FIFO / LRU / Optimal); thrashing 20 vs 4 faults (3 vs 4 frames);
  multiprogramming model peak utilisation 1.00 at degree 2, 0.10 at degree 12.
- Threads: 12 / 4 / 6 ticks (Many-to-One / One-to-One / Many-to-Many); with a blocking call 15 vs 7.

### 7.7 Phase 3 results (real output)
- Real-time, A(C=2,T=D=5) and B(C=4,T=D=7), horizon 35 (U = 97.14 %): RM misses B's first deadline at t = 7
  (B completes late at 8); EDF meets every deadline. RM bound for n = 2 is 0.8284, so RM is not guaranteed.
- Block allocation, 36 blocks x 128 B: contiguous allocation of a 1600-byte file fails with 27 free blocks (largest
  run 12); linked (13 blocks, 4-byte pointers) and indexed (13 data + 1 index block) succeed.
- Heap, 1024-byte arena, alignment 16: three holes 304 / 96 / 208 B and a 320 B tail - a 70-byte request is placed
  at payload @16 (First Fit), @352 (Best Fit), @720 (Worst Fit). Fragmentation preset: 768 B free, largest block 320 B,
  a 400-byte request fails.
- MLQ preset: average waiting 3.4 with 2 preemptions; MLFQ preset: P1 (burst 12) demoted twice, completes at 19.
- Dining philosophers (random order, 150 seeds): the naive order deadlocked in 10 runs, the asymmetric order in none.
- GUI self-test: 435 checks pass (301 reported before Phase 3); layout report: 0 problems on 24 pages at 10 window sizes and
  4 scaling factors.

## 8. Limitations

- The animated engine **replays** a precomputed schedule; the threads do not make scheduling
  decisions themselves. This is deliberate, to keep the output deterministic and testable.
- The integrated run is computed in `core/` and shown at once (with its full event log); it is not
  replayed by the thread engine, because blocked and killed processes do not fit the replay model.
- In the integrated run, "Optimal" uses each process's own future references, because the global
  future depends on a schedule that is not known in advance.
- The real-process `fork()/exec()/wait()` demo is available in the console program
  (`ossim_console engine ... --real`), not in the GUI.
- Charts are drawn with our own `QPainter` widget (`BarChartWidget`); QtCharts is not used.
- The Deadlock tab is limited to 10 processes and 6 resource types. The page model is simple
  (equal allocation, no TLB, no dirty bits, no page-fault service time).
- ThreadSanitizer was run on the test programs (which include the engine and the real
  synchronization demos), not on the Qt GUI.
- Synchronization: the step simulation wakes waiters in FIFO order; real POSIX mutexes and
  semaphores do not promise any order. One simulated interleaving can only show a problem, never
  prove correctness (the Peterson test therefore explores every reachable state). The real race
  demo may show 0 lost updates on a single-CPU machine. The monitor uses Mesa (signal-and-continue)
  semantics, like pthreads, not Hoare semantics.

- Phase 2: see the "Limitations" section of `docs/PHASE2_MODULES.md` (static disk batch, single CPU / device /
  DMA channel, modelled ids and capacities in IPC, labelled cost and utilisation models, Linux-only live demos,
  offscreen-only visual checks).

- Phase 3: Windows System Monitor backend not verified on Windows; job-control fg / bg modelled (no PTY); the virtual
  filesystem and the heap are teaching models (see `docs/PHASE3_MODULES.md`, section 11).

- LJF / LRTF: LJF uses the burst (not the remaining time) as its key, as the course defines it; in the
  integrated run a process that was blocked and is dispatched again therefore competes with its full
  burst. Samples are built-in teaching workloads; `File > Open File...` asks which module a file
  belongs to when its name does not reveal it. The confirmation before a sample replaces the shared
  Storage disk is shown every time (it does not track whether the disk was modified).

## 9. Conclusion

OS-Sim brings CPU scheduling, paging and deadlock handling together in one visual, tested tool.
The pure `core/` library reproduces the textbook results exactly (17, 5.67, 6.5, 7.75, 8.2;
15/12/9; Belady 9/10; `<P1, P3, P4, P0, P2>`). The thread engine shows real threads, mutexes,
semaphores, condition variables and pipe IPC working together deterministically, and is clean
under ThreadSanitizer and AddressSanitizer. The GUI lets a student compare algorithms side by
side, watch a deadlock form and be recovered from, and export the results. The
synchronization module makes lost updates, busy waiting, blocking and condition waiting visible line
by line and backs each mechanism with a real POSIX-thread demonstration.

## 10. References

1. A. Silberschatz, P. B. Galvin, G. Gagne, *Operating System Concepts*, 10th ed., Wiley, 2018
   (scheduling, paging, Banker's and detection examples).
2. A. S. Tanenbaum, H. Bos, *Modern Operating Systems*, 4th ed., Pearson, 2014.
3. W. R. Stevens, S. A. Rago, *Advanced Programming in the UNIX Environment*, 3rd ed., Addison-Wesley, 2013.
4. Linux man pages: `pthread_create(3)`, `pthread_mutex_lock(3p)`, `pthread_cond_wait(3p)`,
   `sem_init(3)`, `sem_wait(3)`, `pipe(2)`, `fork(2)`, `execlp(3)`, `waitpid(2)`.
5. Qt 6 documentation: Qt Widgets, `QGraphicsScene`, signals and slots across threads (queued connections).
6. G. L. Peterson, "Myths About the Mutual Exclusion Problem", *Information Processing Letters* 12(3), 1981.
7. C. A. R. Hoare, "Monitors: An Operating System Structuring Concept", *Communications of the ACM* 17(10), 1974.
8. Linux man pages: `sem_post(3)`, `pthread_cond_broadcast(3p)`; C++ reference: `std::atomic`, `std::memory_order`.
