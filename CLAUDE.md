# CLAUDE.md: OS-Sim master build instructions (for Claude Code)

You are Claude Code, working inside `~/os-sim` on an Ubuntu 22.04 VM (VMware, 2–4 cores, limited RAM).
Your job is to **build the complete OS-Sim project from scratch, by yourself, stage by stage**, until every item in the "Definition of done" section is true.
The student who owns the project has very little time and is working alone. Work autonomously. Ask a question only if you are truly blocked.

---

## 0. How to work (read first)

1. Work through the stages in Section 7 **in order**. Do not skip ahead.
2. After each stage: build from clean, run `ctest`, fix every failure, then `git add -A && git commit -m "Stage N: <title>"`.
3. Keep a file `PROGRESS.md` in the project root. After each stage, update it with: stage number, status, what was built, how to run it, and any known problem. If a session restarts, read `PROGRESS.md` first and continue from the first unfinished stage.
4. Never leave the build broken. If something takes too long, use the simpler fallback given in the stage.
5. Never claim something works unless you have built and run it. Show the real test output.
6. Keep the code **simple, readable and commented**. The student must explain it in a viva. Prefer clear loops over clever templates. Write a short comment on every function, saying what it does and which OS concept it shows.
7. Use only: C++17 standard library, POSIX (pthread, semaphore.h, unistd.h, sys/wait.h), Qt 6 (Widgets, Charts). No other libraries and no network access. If Qt Charts causes trouble, replace charts with a custom `QPainter` bar chart (see Stage 6).
8. Build with `make -j2` (more jobs can exhaust VM memory).
9. At the end of each stage, tell the student in 3–5 lines what was done and the exact command to run it.

Start now by reading `PROPOSAL.md` if it exists (it is only background; this file is the authority), then create `PROGRESS.md`, then begin Stage 1.

---

## 1. Project summary

**Title:** OS-Sim: An Integrated Operating System Simulator for CPU Scheduling, Memory Management and Deadlock Detection with a GUI
**Course:** Operating System Lab, BS (IT) semester 5, Bahria University. Instructor: Amarah Abdullah.
**Group NBA:** Aagha Ali Hassan (03-135242-007), Hamza Malik (03-135242-021), Muhammad Usama (03-135242-038). Show these in the About dialog and the README.

**Problem:** OS concepts (CPU scheduling, paging, deadlocks) are usually studied separately with console tools. OS-Sim is one interactive GUI where a user creates processes, runs them through different schedulers, watches memory allocation and page replacement, and detects or avoids deadlocks, so the internal working of an OS is visual, comparable and easy to understand.

**Objectives:**
- 4 CPU scheduling algorithm families (FCFS, SJF/SRTF, Round Robin, Priority) with a Gantt chart and average waiting, turnaround and response time.
- Paged memory with FIFO, LRU and Optimal page replacement, reporting page faults and hit ratio.
- Banker's Algorithm (avoidance) plus a detection algorithm that identifies deadlocked processes, with recovery by process termination.
- Each simulated process runs as a POSIX thread, synchronized with mutexes and semaphores and a common simulation clock.
- One tab-based GUI with side-by-side algorithm comparison and exportable results.

**Extension (Stage 11):** a fourth tab, **Synchronization** (race condition, Peterson's solution, mutex locks,
binary and counting semaphores, producer-consumer, monitors with condition variables). See Section 11.

**OS concepts that must visibly appear in the code** (the examiner will look for these):

| Concept | Where / how |
|---|---|
| Process management | `PCB` struct (pid, arrival, burst, remaining, priority, pages, state, times); states New → Ready → Running → Waiting → Terminated; `fork()/exec()/wait()` in an optional real-process demo |
| Threads | One `pthread_create` per simulated process plus one clock thread; `pthread_join` at shutdown |
| Synchronization | `pthread_mutex_lock/unlock` on ready queue and shared state; POSIX semaphores `sem_init/sem_wait/sem_post` to step processes; `pthread_cond_*` for the clock tick and pause/resume |
| CPU scheduling | FCFS, SJF, SRTF, Round Robin (quantum), Priority (preemptive and non-preemptive) |
| Memory management | Paging, page tables, frame allocation, FIFO/LRU/Optimal |
| Deadlocks | Banker's safety and request algorithms, detection, recovery by termination |
| IPC | `pipe()` used by worker threads to send log lines to the logger/GUI |
| Process synchronization | `core/sync.*` step simulation (race, Peterson, mutex, semaphores, producer-consumer, monitor); `engine/sync_demo.*` real pthread / `sem_t` / `pthread_cond` / `std::atomic` demos |

---

## 2. Architecture (mandatory)

```
os-sim/
├── CMakeLists.txt            top level; C++17; enable_testing(); add_subdirectory(...)
├── CLAUDE.md                 this file
├── PROGRESS.md               you maintain this
├── README.md                 written in Stage 10
├── core/                     PURE C++17. No Qt. No threads. Deterministic.
│   ├── pcb.h
│   ├── scheduler.h / .cpp
│   ├── memory.h / .cpp
│   ├── deadlock.h / .cpp
│   ├── io_formats.h / .cpp   CSV and sample-file loaders/savers
│   └── CMakeLists.txt        static library `oscore`
├── engine/                   pthreads + semaphores + mutex + condvar + pipe
│   ├── engine.h / .cpp
│   ├── logger.h / .cpp
│   └── CMakeLists.txt        static library `osengine` (links oscore, Threads::Threads)
├── apps/
│   └── console_main.cpp      console demo of everything (executable `ossim_console`)
├── gui/                      Qt 6 Widgets application (executable `ossim`)
│   ├── main.cpp
│   ├── MainWindow.h / .cpp
│   ├── SchedulingTab.h / .cpp
│   ├── MemoryTab.h / .cpp
│   ├── DeadlockTab.h / .cpp
│   ├── GanttWidget.h / .cpp
│   ├── RagView.h / .cpp
│   ├── BarChartWidget.h / .cpp   (custom painter bar chart; use this instead of QtCharts if charts misbehave)
│   ├── EngineBridge.h / .cpp     QObject that runs the engine on a worker thread and emits Qt signals
│   └── CMakeLists.txt
├── tests/                    plain C++ asserts, one executable per module, registered with add_test
│   ├── test_scheduler.cpp
│   ├── test_memory.cpp
│   ├── test_deadlock.cpp
│   ├── test_engine.cpp
│   └── CMakeLists.txt
├── samples/
│   ├── processes_fcfs.csv        the 24/3/3 example
│   ├── processes_srtf.csv        the 8/4/9/5 example
│   ├── processes_priority.csv
│   ├── refstring_standard.txt    the 20-page string
│   ├── refstring_belady.txt
│   ├── banker_sample.txt
│   ├── deadlock_sample.txt
│   └── requests_integrated.csv   scripted resource requests for the integrated run
└── docs/                     REPORT.md, screenshots/
```

**Golden rule:** `core/` calculates everything and returns result structs. `engine/` only **replays** a result from `core/` using threads, so the output is identical on every run. `gui/` only displays. Worker threads must never touch Qt widgets; use queued signals.

Top-level CMake must set: `CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `-Wall -Wextra`, `CMAKE_AUTOMOC ON` (for gui), `find_package(Threads REQUIRED)`, `find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)` (inside gui, so core/engine/tests still build if Qt is missing; make the gui target optional with `if(Qt6_FOUND)`).
Provide a CMake option `-DSANITIZE=thread|address` that adds `-fsanitize=...`.

---

## 3. Core data structures (exact definitions to implement)

### 3.1 `core/pcb.h`

```cpp
enum class ProcState { New, Ready, Running, Waiting, Terminated };
const char* stateName(ProcState);

struct PCB {
    int pid = 0;
    int arrival = 0;
    int burst = 0;
    int remaining = 0;      // set to burst before a run
    int priority = 0;       // lower number = higher priority
    int pages = 0;          // number of pages the process needs
    ProcState state = ProcState::New;
    int firstRun = -1;      // first time on CPU (for response time)
    int finish = -1;        // completion time
    std::vector<int> maxRes;    // resource Max vector (for Banker's)
    std::vector<int> allocRes;  // resource Allocation vector
};
```

### 3.2 `core/scheduler.h`

```cpp
enum class Algo { FCFS, SJF, SRTF, RR, PRIORITY_NP, PRIORITY_P };
const char* algoName(Algo);

struct Segment { int pid; int start; int end; };   // pid == -1 means CPU idle

struct ProcMetrics { int pid; int arrival; int burst; int completion;
                     int waiting; int turnaround; int response; };

struct SchedResult {
    Algo algo;
    int quantum;                       // used by RR, else 0
    std::vector<Segment> timeline;     // adjacent segments of the same pid MUST be merged
    std::vector<ProcMetrics> metrics;  // sorted by pid
    double avgWaiting, avgTurnaround, avgResponse;
    int contextSwitches;               // number of times the running pid changes (ignore idle)
};

SchedResult runScheduler(Algo, std::vector<PCB> procs, int quantum = 4);
std::vector<SchedResult> runAll(const std::vector<PCB>&, int quantum); // all six algorithms, same input
```

Definitions: turnaround = completion − arrival; waiting = turnaround − burst; response = firstRun − arrival.
Tie-breaking (must be deterministic): same key → earlier arrival → lower pid.
RR: a process arriving at the same tick as a quantum expiry is queued **before** the preempted process.
Insert idle segments (`pid = -1`) when the CPU has nothing to run.
Validate input: burst > 0, arrival ≥ 0, quantum > 0, unique pids; throw `std::invalid_argument` with a clear message.

### 3.3 `core/memory.h`

```cpp
enum class PageAlgo { FIFO, LRU, OPTIMAL };

struct MemStep {
    int page;                    // page referenced
    std::vector<int> frames;     // frame contents after this reference, -1 = empty
    bool fault;
    int replaced;                // page evicted, or -1
};
struct MemResult {
    PageAlgo algo; int numFrames;
    std::vector<MemStep> steps;
    int faults, hits; double hitRatio;   // hitRatio = hits / steps.size()
};
MemResult runPageReplacement(PageAlgo, const std::vector<int>& refs, int numFrames);

// Simple paging model for the integrated run
struct PageTableEntry { int frame = -1; bool valid = false; };
struct PageTable { int pid; std::vector<PageTableEntry> entries; };
struct FrameAllocator {          // equal-share allocation of frames among processes
    int totalFrames; std::vector<int> frameOwner;   // pid or -1
    // allocateEqual(pids): returns frames per process = totalFrames / pids.size()
};
```

Rules: frame filling order is frame 0, 1, 2…; for ties in LRU/Optimal, evict the page that was loaded earliest. Optimal: evict the page whose next use is furthest in the future; pages never used again go first (earliest loaded first among those).
Also provide `std::vector<int> parseRefString(const std::string&)` accepting commas and/or spaces, throwing `std::invalid_argument` on bad tokens or negatives.

### 3.4 `core/deadlock.h`

```cpp
using Matrix = std::vector<std::vector<int>>;
using Vec = std::vector<int>;

struct SafetyStep { int pid; Vec workBefore; Vec workAfter; };
struct SafetyResult { bool safe; std::vector<int> sequence; std::vector<SafetyStep> trace; Matrix need; };
SafetyResult bankersSafety(const Matrix& alloc, const Matrix& max, const Vec& avail);

struct RequestResult { bool granted; std::string reason; SafetyResult safetyAfter; };
// Resource-request algorithm. Check: request <= need, request <= available, pretend-allocate, run safety, roll back if unsafe.
RequestResult bankersRequest(int pid, const Vec& request, Matrix alloc, const Matrix& max, Vec avail);

struct DetectionResult { bool deadlocked; std::vector<int> deadlocked_pids; std::vector<int> finishOrder; };
DetectionResult detectDeadlock(const Matrix& alloc, const Matrix& request, const Vec& avail);

struct RecoveryStep { int killedPid; Vec released; DetectionResult after; };
// Terminate deadlocked processes one at a time (lowest total allocation first, tie -> lowest pid) and re-detect.
std::vector<RecoveryStep> recoverByTermination(Matrix alloc, Matrix request, Vec avail);

struct RagEdge { int from; int to; bool isRequest; };  // process nodes 0..n-1; resource nodes n..n+m-1
struct RagGraph { int nProcs, nRes; std::vector<RagEdge> edges; std::vector<int> cycleNodes; };
RagGraph buildRag(const Matrix& alloc, const Matrix& request);   // finds a cycle (DFS) and fills cycleNodes
```

Notes: matrix dimension mismatches throw `std::invalid_argument`. For multi-instance resources, `cycleNodes` is informative only (a cycle is necessary, not sufficient); the **detection algorithm decides** who is deadlocked. In the GUI, colour the nodes of the deadlocked processes red and also draw the cycle edges in red if a cycle exists.

### 3.5 `core/io_formats.h`

- `loadProcessesCsv(path)` / `saveProcessesCsv`: header `pid,arrival,burst,priority,pages`; ignore blank lines and lines starting with `#`.
- `loadMatrixFile(path)` for Banker/detection samples, format:
  ```
  # processes=5 resources=3
  available: 3 3 2
  alloc:
  0 1 0
  2 0 0
  ...
  max:
  7 5 3
  ...
  request:        (optional; used for detection)
  ...
  ```
- `loadRequestsCsv(path)`: `pid,run_tick,r0,r1,...` (resource request made by a process when it has executed `run_tick` ticks).
- Every loader throws `std::runtime_error` with a clear message on a bad file.

---

## 4. Engine specification (`engine/`)

Purpose: show real OS primitives. The engine **replays** a timeline that `core/` produced.

### 4.1 Objects
- `SimulationEngine(std::vector<PCB> procs, std::vector<Segment> timeline, EngineOptions)`
- `EngineOptions`: `tickDelayMs` (0 for tests, e.g. 200 for animation), `useRealDemo` (bool).
- Public: `start()`, `pause()`, `resume()`, `step()`, `stop()`, `join()`, and a thread-safe way to read the log and the current state snapshot (tick, and each pid's state and remaining).
- Observer hook: `std::function<void(const EngineEvent&)>` called from a **single** dispatcher (not from every worker). `EngineEvent {int tick; int pid; ProcState state; std::string text;}`.

### 4.2 Threads and primitives (all must be used for real)
- **One clock thread** (`pthread_create`). Loop over ticks `t = 0 .. endTime-1`:
  1. Lock the mutex. Wait on a **condition variable** while paused (use `pthread_cond_wait` in a loop; `resume()/step()` signal it).
  2. Set `currentTick = t`. Move processes with `arrival == t` from New to Ready (log it). Look up which pid the timeline says runs at tick t. Update states: that pid becomes Running, previous Running pid becomes Ready (or Terminated if remaining == 0). Unlock.
  3. If a pid runs: `sem_post(&runSem[pid])`, then `sem_wait(&doneSem)`, which waits until the process thread has executed that tick.
  4. Optional sleep `tickDelayMs` (use `usleep`/`nanosleep`).
- **One thread per process** (`pthread_create`). Loop: `sem_wait(&runSem[pid])`; if the stop flag is set → exit; lock mutex; `remaining--`; if `firstRun < 0` set it to t; if `remaining == 0` set Terminated and `finish = t+1`; log; unlock; `sem_post(&doneSem)`.
- **Mutex** (`pthread_mutex_t`) protects: the ready queue, each PCB, `currentTick`, the paused flag.
- **Semaphores**: `sem_t runSem[N]` (one per process) and `sem_t doneSem` (init 0), via `sem_init`. Destroy with `sem_destroy`.
- **Shutdown**: set the stop flag, `sem_post` every `runSem`, `pthread_join` every thread, destroy primitives. No leaks (Valgrind/ASan clean).
- All of this must be deterministic: the handshake `runSem → process → doneSem` serialises ticks, so there is **no free-running concurrency in the order of log lines**.

### 4.3 IPC logger (`logger.*`)
- Create a `pipe(fds)`. Worker threads (and the clock thread) call `logWrite(const std::string&)`, which `write()`s one line (`\n` terminated, lines < PIPE_BUF so writes are atomic) to the write end.
- A reader function `drainLog()` reads the read end (non-blocking: set `O_NONBLOCK`) and appends complete lines to an in-memory `std::vector<std::string>`; the dispatcher calls it after every tick and forwards the new lines as events.
- Line format: `[t=<tick>] P<pid> <STATE> <detail>`, e.g. `[t=3] P2 RUNNING remaining=1`.
- Also log non-process events with `P-` as the id: `[t=0] CLOCK tick`, `[t=5] MEM fault page=3`, `[t=7] DEADLOCK detected P1,P2`.

### 4.4 Optional real-process demo
`realProcessDemo(int n)`: for i in 1..n `fork()`; the child calls `execlp("sleep","sleep","1",nullptr)`; the parent `waitpid()`s each child and logs PIDs and exit codes. Must not run in tests.

### 4.5 Integrated run (Stage 9)
`IntegratedRun` combines all three modules, tick by tick, deterministically:

- Inputs: processes (with `pages`), a scheduling algorithm and quantum, a number of frames, a page algorithm, resource data (`max`, `available` total) and the scripted requests file, and the mode `avoidance` (Banker's) on/off, plus detection interval K (default 5 ticks).
- Memory: each process gets a deterministic reference sequence, generated with a fixed-seed `std::mt19937(pid)` as pages in `[0, pages-1]`, with locality (70% chance to repeat or move ±1 from the previous page). On every tick the running process references its next page; use one shared frame pool with the chosen replacement algorithm (key frames by `(pid,page)`). Log `MEM hit` or `MEM fault`.
- Resources: when the running process reaches `run_tick` of a scripted request, apply it. With avoidance ON, call `bankersRequest`: granted → allocate; denied → the process goes to **Waiting** and retries at every tick. With avoidance OFF: grant only if `request <= available`, otherwise Waiting (this is how a real deadlock can occur). When a process terminates, release its resources and wake waiting ones.
- Detection: every K ticks run `detectDeadlock` over the blocked processes' pending requests. If deadlocked, log it, and (if "auto-recover" is on) call `recoverByTermination` and apply it, logging each killed process.
- Waiting processes must be skipped by the scheduler: the integrated run re-evaluates the pick each tick from the Ready set using the same algorithm rules (write one tick-based simulator step function in `core/` shared with `runScheduler` so the logic is not duplicated: `schedulerPick(...)`). If this is too complex, implement the integrated run as a simpler tick loop with FCFS/RR/Priority only and say so in `PROGRESS.md`.
- Output: the event log, final metrics, memory stats, deadlock info. Export it as in Stage 9.

---

## 5. GUI specification (`gui/`)

Window title `OS-Sim: Operating System Simulator`, minimum size 1100×700, a `QTabWidget` with **Scheduling**, **Memory**, **Deadlock**, a shared **event-log dock** at the bottom (`QDockWidget` containing a read-only `QPlainTextEdit`, monospace), a menu bar and a status bar.

**Menus:** File → Load sample (submenu of all sample files), Export report…, Exit. Help → About (shows project title, group name NBA and the three members).
Use a clean, consistent look: group boxes, spacing 8, a fixed colour per pid (a 12-colour palette repeated), grey for idle, red for faults/deadlock, green for hits/safe.

### 5.1 Scheduling tab
- Left: editable `QTableWidget` (PID, Arrival, Burst, Priority, Pages) with Add, Remove, Clear, Load CSV, Save CSV. Algorithm `QComboBox` (six algorithms), quantum `QSpinBox` (enabled only for RR), buttons **Run**, **Run animated**, **Compare all**, plus Pause/Step/Stop for animated mode and a speed slider (50–1000 ms per tick).
- Right: `GanttWidget` (custom `paintEvent`): time axis with tick labels, one coloured box per segment with `P<pid>` text, idle in grey, ready to be partially drawn up to the current tick for the animated mode. Make it scrollable if the timeline is wide.
- Results `QTableWidget`: PID, Arrival, Burst, Completion, Waiting, Turnaround, Response, and a final Averages row; also show context switches and CPU utilisation.
- **Compare all**: a table (rows = algorithms, columns = avg WT, TAT, RT, context switches), the best value in each column in bold green, and a bar chart (QtCharts `QChart` with `QBarSeries`, or `BarChartWidget`).
- **Run animated** uses `EngineBridge`: the engine runs in a `QThread`/`std::thread`; each event is emitted as a Qt signal (queued connection) that updates the Gantt, a process-state table (PID, state, remaining) and the log dock.

### 5.2 Memory tab
- Inputs: reference string `QLineEdit`, frames `QSpinBox` (1–10), algorithm combo (FIFO, LRU, Optimal, Compare all), buttons **Run**, **Random string** (length 20, pages 0–9), **Standard example**, **Belady example**.
- Output table: one column per reference; rows Frame 1..N; a last row HIT/FAULT; faults in red, hits in green; the replaced page shown in a small tooltip or in a row "Evicted".
- Summary: faults, hits, hit ratio per algorithm; in compare mode, three summaries side by side plus a bar chart of faults.
- Page-table panel: choose a pid from the Scheduling table and show a simple page table (page → frame, valid), built from `FrameAllocator`.

### 5.3 Deadlock tab
- Inputs: spin boxes for processes (1–10) and resources (1–6); editable tables for Allocation, Max, Request, plus a one-row Available table; Need is computed and read-only. Buttons: Load Banker's sample, Load deadlock sample, Clear.
- Actions: **Check safety** (shows `SAFE: <P1, P3, P4, P0, P2>` in green or `UNSAFE` in red, plus a step trace list), **Request resources** (pid + request vector → granted/denied and the reason), **Detect deadlock** (deadlocked processes highlighted red in the tables), **Recover** (terminates step by step with a short delay and redraws).
- `RagView` using `QGraphicsScene`/`QGraphicsView`: processes as circles, resources as squares with dots for instances, assignment edges (resource → process) and request edges (process → resource) with arrowheads, deadlocked processes and cycle edges in red. Layout: processes in a row on top, resources in a row at the bottom (or a circle layout). Redraw after every action.

### 5.4 Export
`File → Export report…` asks for a base name and writes `<name>.txt` and `<name>.csv`:
- the process table, the scheduling results and averages for the last run (and the comparison table if it was run),
- memory results (faults/hits/ratio per algorithm and the frame table),
- deadlock results (matrices, safe sequence or deadlocked list),
- the event log.
The CSV has sections separated by a line `## <section>`. Show a message box with the saved path.

### 5.5 Quality
- Validate every input and show a friendly `QMessageBox::warning` (empty table, negative values, burst 0, bad reference string, mismatched matrices). The app must **never crash** on bad input.
- Make all widgets resize sensibly (layouts, no fixed pixel positions except the custom painters).
- Smoke test without a display: `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` must create the main window, load each sample, run each action once and exit with code 0. Implement the `--selftest` flag in `main.cpp`.

---

## 6. Acceptance tests (these exact numbers must pass in `tests/`)

### 6.1 Scheduler
| Case | Input (pid arrival burst [priority]) | Expected |
|---|---|---|
| FCFS | P1 0 24, P2 0 3, P3 0 3 | WT = 0, 24, 27; avg WT = **17**; avg TAT = 27.0; timeline P1 0–24, P2 24–27, P3 27–30 |
| SJF (non-preemptive) | same | order P2, P3, P1; WT = 6, 0, 3; avg WT = **3** |
| RR q=4 | same | WT: P1=6, P2=4, P3=7; avg WT = **5.6667** |
| SRTF | P1 0 8, P2 1 4, P3 2 9, P4 3 5 | avg WT = **6.5** |
| SJF non-preemptive | same as SRTF row | avg WT = **7.75** |
| Idle gap | P1 0 2, P2 5 2 (FCFS) | timeline P1 0–2, idle 2–5, P2 5–7 |
| Priority NP | P1 0 10 pr3, P2 0 1 pr1, P3 0 2 pr4, P4 0 1 pr5, P5 0 5 pr2 | order P2, P5, P1, P3, P4; avg WT = **8.2** |
| Validation | burst 0 or duplicate pid | throws `std::invalid_argument` |

### 6.2 Memory
- Reference string `7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1`, 3 frames: **FIFO 15 faults, LRU 12, Optimal 9**.
- Belady: `1,2,3,4,1,2,5,1,2,3,4,5` with FIFO: **9 faults at 3 frames, 10 faults at 4 frames**.
- Hit ratio check: for the standard string with LRU, hits = 8, hit ratio = 0.4.
- `parseRefString("1, 2 3,4")` → `{1,2,3,4}`; `parseRefString("1,a")` throws.

### 6.3 Deadlock
Banker's (A=10, B=5, C=7):
```
Alloc: P0 0 1 0 | P1 2 0 0 | P2 3 0 2 | P3 2 1 1 | P4 0 0 2
Max:   P0 7 5 3 | P1 3 2 2 | P2 9 0 2 | P3 2 2 2 | P4 4 3 3
Available: 3 3 2
```
- `bankersSafety`: safe = true; need matrix = `P0 7 4 3, P1 1 2 2, P2 6 0 0, P3 0 1 1, P4 4 3 1`; a valid safe sequence is `P1, P3, P4, P0, P2`. Scan method (use exactly this so the sequence is reproducible): repeat passes over pids 0..n-1; inside a pass, whenever an unfinished process has `need <= work`, take it immediately (work += its allocation) and **continue the same pass with the next pid** (do not restart from P0); stop when a full pass makes no progress. This yields exactly `P1, P3, P4, P0, P2`; assert it.
- `bankersRequest(P1, {1,0,2})`: granted.
- After granting that request: `bankersRequest(P4, {3,3,0})` denied (exceeds available); `bankersRequest(P0, {0,2,0})` denied (unsafe state).
- A request greater than the process's Need (for example P1 asking `{5,0,0}`) is denied with a reason mentioning "exceeds maximum claim".

Detection (A=7, B=2, C=6):
```
Alloc:   P0 0 1 0 | P1 2 0 0 | P2 3 0 3 | P3 2 1 1 | P4 0 0 2
Request: P0 0 0 0 | P1 2 0 2 | P2 0 0 0 | P3 1 0 0 | P4 0 0 2
Available: 0 0 0
```
- No deadlock. With the same pass-scan method as above the finish order is `P0, P2, P3, P4, P1` (assert it; the textbook's `P0, P2, P3, P1, P4` is also valid but is not what this scan produces).
- Change P2's request to `0 0 1`: deadlocked = `{P1, P2, P3, P4}`.
- `recoverByTermination` on that state clears the deadlock and returns the kill order (assert that the final `after.deadlocked` is false and that the kill list is non-empty).

### 6.4 Engine
- Run the FCFS example through `SimulationEngine` with `tickDelayMs = 0`: final PCB finish times equal the `core/` completion times (27 for P2, 30 for P3, 24 for P1), and the log contains exactly one `TERMINATED` line per process.
- Run the engine **20 times**: the logs must be byte-identical every time.
- Run it with `-fsanitize=thread` once and with `-fsanitize=address,undefined` once, and fix every report. (Do this as a manual command in the stage, not inside ctest.)
- Pause → resume → stop must not deadlock or hang (test with a 5-second timeout using `alarm()` or a watchdog thread).

---

## 7. Build stages (execute in order; commit after each)

### Stage 1: Skeleton (≈ 30 min)
Create the directory tree from Section 2, the top-level and sub-`CMakeLists.txt` files, empty placeholder sources that compile, `PROGRESS.md`, a `.gitignore` containing `build/`, and a one-test ctest target that passes. Check `g++ --version`, `cmake --version`, `qmake6 --version`, and that `find_package(Qt6 COMPONENTS Widgets Charts)` works. If Qt6 or Charts is missing, tell the student the exact `sudo apt install` command and continue with core/engine meanwhile.
Done when: `mkdir -p build && cd build && cmake .. && make -j2 && ctest` succeeds.

### Stage 2: Scheduling core
Implement `core/pcb.h`, `core/scheduler.*`, `core/io_formats.*` (process CSV), `apps/console_main.cpp` (prints a text Gantt like `|P1....|P2..|` plus the metrics table for all algorithms), sample CSVs, and `tests/test_scheduler.cpp` with every row of Section 6.1.
Done when: all scheduler tests pass and the console demo prints all six algorithms for each sample.

### Stage 3: Memory core
Implement `core/memory.*` and `tests/test_memory.cpp` (Section 6.2). Extend the console demo with a `memory` command. Add `refstring_*.txt` samples.
Done when: 15/12/9 and the Belady 9/10 results pass.

### Stage 4: Deadlock core
Implement `core/deadlock.*`, the matrix/requests loaders, sample files, and `tests/test_deadlock.cpp` (Section 6.3). Extend the console demo with a `deadlock` command.
Done when: every assertion in 6.3 passes.

### Stage 5: Thread engine
Implement `engine/` exactly as in Section 4.1–4.4 and `tests/test_engine.cpp` (Section 6.4). Add a console command `engine` that runs the engine with `tickDelayMs=100` and prints the live log. Run the TSAN and ASAN builds manually (`cmake -DSANITIZE=thread ..` in a separate build dir) and fix all reports.
Done when: tests pass, 20 identical logs, TSAN and ASAN clean.

### Stage 6: GUI, Scheduling tab
Implement `MainWindow`, `SchedulingTab`, `GanttWidget`, `BarChartWidget` (and QtCharts if available; if QtCharts fails to build or load, use `BarChartWidget` only and note it in `PROGRESS.md`), `EngineBridge`, the log dock and `--selftest`. Wire up Run, Run animated (through the engine), Compare all and the CSV load/save.
Done when: the app launches, the FCFS sample shows the right Gantt chart and avg WT 17, RR q=4 shows ≈ 5.67, Compare all shows the table and chart, and `QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest` exits 0.

### Stage 7: GUI, Memory tab
Implement `MemoryTab` per Section 5.2.
Done when: the standard example shows 15 / 12 / 9 faults, Belady works, bad input shows a warning dialog, and `--selftest` still exits 0.

### Stage 8: GUI, Deadlock tab and RAG
Implement `DeadlockTab` and `RagView` per Section 5.3.
Done when: the Banker's sample shows SAFE with `P1, P3, P4, P0, P2`, a P1 request of `1 0 2` is granted, the deadlock sample shows P1–P4 deadlocked in red with the cycle drawn, Recover clears it step by step, and `--selftest` still exits 0.

### Stage 9: Integration and export
Implement `IntegratedRun` (Section 4.5), an **Integrated run** button/panel in the Scheduling tab (choose frames, page algorithm, avoidance on/off, detection interval), the shared event log, and the export (Section 5.4). Share one process list between all tabs, and use the same pids in the Deadlock tab.
Add a test for the integrated run: with the sample files and avoidance **on**, no deadlock is ever detected; with avoidance **off** on the provided `requests_integrated.csv`, a deadlock is detected at least once (design the sample so this is true) and auto-recover clears it.
Run the whole app once with `-DSANITIZE=address` and fix all reports.
Done when: tests pass and the export files open and contain all sections.

### Stage 10: Polish, docs, final verification
1. Clean build from scratch (`rm -rf build`), run `ctest`, the selftest and the sanitizer builds one final time.
2. Write `README.md`: description, features, group members, Ubuntu 22.04 dependency install command (`sudo apt install build-essential cmake gdb git qt6-base-dev qt6-charts-dev libqt6charts6-dev`), build/run/test commands, folder structure and a short user guide for each tab.
3. Write `docs/REPORT.md` following this outline: introduction, objectives, OS concepts used (map each concept to the real files and functions), system design (include a text block diagram), implementation of each module, testing (include the acceptance tables and their real results), results and algorithm comparison (use real numbers from the samples), limitations, conclusion, references.
4. Write `docs/VIVA_NOTES.md`: for each OS concept, a plain-language explanation of **how this project's code uses it** (name the files and functions), plus 25 likely viva questions with short answers (threads, semaphores vs mutex, condition variables, pipe IPC, why core is thread-free, scheduling differences, starvation, page faults, Belady, Optimal impossibility, four deadlock conditions, safe vs unsafe vs deadlock, avoidance vs detection, recovery).
5. Tell the student which screenshots to take for the report (list of 6–8 with the exact steps to produce them) and where to save them (`docs/screenshots/`).
Done when: the Definition of done below is all true.

---

## 11. Stage 11: Process synchronization extension (implemented)

Files: `core/sync.h/.cpp`, `engine/sync_demo.h/.cpp`, `gui/SyncTab.*`, `gui/SyncViews.*`, `gui/SyncBridge.*`,
`tests/test_sync.cpp`, `tests/test_sync_engine.cpp`; console command `ossim_console sync [--real]`.

Rules (same golden rule as the rest of the project):
- `core/sync.*` is pure and deterministic: `SyncSim::step(pid)` executes exactly one pseudocode line, so every
  interleaving is reproducible (manual Step per process, scenario script, round robin, seeded random). The race
  condition is modelled by interleaving read / modify / write - never by unsynchronized C++ variables.
- Peterson must stay the lecture variant: `other = 1 - process; interested[process] = true; turn = process;
  while (interested[other] == true && turn == process);` ... `interested[process] = false;` (2 processes only).
  Real-thread versions use `std::atomic` with sequentially consistent ordering.
- `engine/sync_demo.*` runs real pthreads (`pthread_mutex`, `sem_init/wait/post/destroy`, `pthread_cond_*`),
  logs through `PipeLogger`, checks its result, and `cancel()` must always let `run()` join (no hang).
- The GUI animates with a `QTimer` (one instruction per tick); the real demo runs on a `std::thread` via
  `SyncBridge` with queued signals. Simulated waiting queues are FIFO; say clearly that POSIX gives no order.
- Tests: lost update (counter 1 of 2), synchronized counters exact, Peterson mutual exclusion over all reachable
  states, mutex ownership/hand-off, semaphore limits, buffer boundaries and blocking, monitor wait/signal,
  comparison, validation, reset/determinism, real demos and cancellation. The GUI `--selftest` covers the tab.

## 8. Definition of done

- [ ] `rm -rf build && mkdir build && cd build && cmake .. && make -j2 && ctest --output-on-failure` passes with **zero warnings** (`-Wall -Wextra`) and all tests green.
- [ ] Every number in Section 6 is asserted in a test and passes.
- [ ] `./gui/ossim` launches; all three tabs work; bad input never crashes; `--selftest` exits 0 under `QT_QPA_PLATFORM=offscreen`.
- [ ] The engine uses `pthread_create/join`, `pthread_mutex_*`, `sem_init/wait/post/destroy`, `pthread_cond_*` and `pipe()` for real, and produces byte-identical logs on 20 runs.
- [ ] TSAN and ASAN/UBSAN builds report nothing.
- [ ] Export writes `.txt` and `.csv` with all sections.
- [ ] `README.md`, `docs/REPORT.md`, `docs/VIVA_NOTES.md`, `PROGRESS.md` exist and are accurate.
- [ ] Git history has one commit per stage.
- [ ] Stage 11: the Synchronization tab works, `test_sync` and `test_sync_engine` pass, TSAN/ASAN clean.

---

## 9. Fallbacks and troubleshooting rules

- **QtCharts missing/crashing:** use `BarChartWidget` (custom `QPainter`), remove the QtCharts link.
- **VMware GUI slow:** keep painting simple (no gradients, no per-frame allocations), animate with `QTimer` at ≥ 50 ms.
- **`make` runs out of memory:** use `make -j1`.
- **Time pressure inside a stage:** deliver the simplest correct version first, commit, then improve. Preemptive priority and the real-process demo are the first things to drop; record any drop in `PROGRESS.md` under "Not done".
- **A test number looks wrong:** re-derive it by hand before changing the code or the test, and tell the student which one was wrong.
- **Never** weaken or delete a failing assertion just to get green. Fix the code, or explain why the expected value was wrong.
- **Do not** put algorithm logic inside threads or in GUI classes.
- **Do not** use `git push`, change global git config, or install anything except via `apt` commands you show the student first.

---

## 10. Start command

When the student says "start" (or any first message), do this immediately: read this file, create `PROGRESS.md`, tell the student in one sentence what you are about to do, and begin **Stage 1**. After each stage, report in 3–5 lines and continue to the next stage without waiting, unless a build tool is missing or you need the student to run an `apt install` command.
