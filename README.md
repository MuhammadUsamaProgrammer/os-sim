# OS-Sim: An Integrated Operating System Simulator

**CPU Scheduling, Memory Management and Deadlock Detection with a GUI**

Operating System Lab, BS (IT) semester 5, Bahria University. Instructor: Amarah Abdullah.

**Group NBA**

| Name | Enrolment |
|---|---|
| Aagha Ali Hassan | 03-135242-007 |
| Hamza Malik | 03-135242-021 |
| Muhammad Usama | 03-135242-038 |

OS-Sim is one Qt desktop application in which you create processes, run them through
different CPU schedulers, watch paging and page replacement, and avoid, detect and recover
from deadlocks. The animated mode replays a schedule with real POSIX threads, mutexes,
semaphores, condition variables and a pipe, so the OS primitives are visible in the code.

## Features

- **CPU scheduling:** FCFS, SJF, SRTF, Round Robin (quantum), Priority (non-preemptive and
  preemptive). Gantt chart, per-process completion/waiting/turnaround/response times, averages,
  context switches and CPU utilisation. *Compare all* runs the six algorithms on the same input,
  with a table (best values in bold green) and a bar chart.
- **Thread engine:** *Run animated* replays the schedule with one `pthread` per process plus a
  clock thread (mutex + `sem_t` handshake + `pthread_cond_*` for pause/step + `pipe()` log channel).
  Pause, Step, Stop and a speed slider are available. The logs are byte-identical on every run.
- **Memory:** FIFO, LRU and Optimal page replacement with a frame table (hits green, faults red,
  evicted pages), fault/hit/hit-ratio summaries, a comparison chart, a Belady's anomaly demo and a page
  table built by an equal-share frame allocator.
- **Deadlocks:** Banker's safety algorithm (with step trace), Banker's resource-request algorithm,
  multi-instance deadlock detection, recovery by terminating victims step by step, and a
  resource-allocation graph (deadlocked processes and cycle edges in red).
- **Integrated run:** scheduling, paging and resource requests together, tick by tick, with Banker's
  avoidance on or off, periodic detection and automatic recovery.
- **Export:** `File > Export report...` writes `<name>.txt` and `<name>.csv` with every section.
- Bad input never crashes the program. It shows a warning dialog instead.

## Install (Ubuntu 22.04)

```bash
sudo apt install build-essential cmake gdb git qt6-base-dev qt6-charts-dev libqt6charts6-dev
```

Only Qt Widgets is actually used (charts are drawn with our own `QPainter` widget), so
`qt6-base-dev` is enough. The GUI is optional: without Qt the core, the engine, the console
demo and the tests still build.

## Build, run, test

```bash
mkdir -p build && cd build
cmake .. && make -j2          # use make -j1 if the VM runs out of memory
ctest --output-on-failure     # 5 test programs, all must pass
./gui/ossim                   # the GUI
QT_QPA_PLATFORM=offscreen ./gui/ossim --selftest   # automatic GUI smoke test, exit code 0
```

Console demo (run from the project root so `samples/` is found):

```bash
./build/apps/ossim_console                          # everything
./build/apps/ossim_console sched samples/processes_srtf.csv 4
./build/apps/ossim_console memory "7,0,1,2,0,3,0,4" 3
./build/apps/ossim_console deadlock
./build/apps/ossim_console engine samples/processes_fcfs.csv rr 4 --real   # live thread log + fork/exec demo
```

Sanitizer builds (manual checks):

```bash
mkdir build-thread && cd build-thread && cmake .. -DSANITIZE=thread && make -j2 && ctest
mkdir build-address && cd build-address && cmake .. -DSANITIZE=address && make -j2 && ctest   # address + undefined
```

## Folder structure

```
os-sim/
├── CMakeLists.txt     top level (C++17, -Wall -Wextra, -DSANITIZE=thread|address)
├── core/              pure C++17 algorithms, no Qt, no threads (library oscore)
│   ├── pcb.h            PCB and process states
│   ├── scheduler.*      FCFS/SJF/SRTF/RR/Priority, schedulerPick()
│   ├── memory.*         FramePool (FIFO/LRU/Optimal), page tables, frame allocator
│   ├── deadlock.*       Banker's, detection, recovery, RAG
│   ├── integrated.*     integrated run (scheduling + paging + resources)
│   └── io_formats.*     sample file loaders/savers
├── engine/            pthreads + semaphores + mutex + condvar + pipe (library osengine)
│   ├── engine.*         SimulationEngine, realProcessDemo (fork/exec/wait)
│   └── logger.*         PipeLogger (pipe IPC)
├── apps/console_main.cpp   console demo (ossim_console)
├── gui/               Qt 6 Widgets application (ossim)
├── tests/             one test program per module, registered with ctest
├── samples/           process CSVs, reference strings, Banker/deadlock matrices, scripted requests
└── docs/              REPORT.md, VIVA_NOTES.md, screenshots/
```

## User guide

### Scheduling tab
1. Load a sample (`File > Load sample > processes_fcfs.csv`) or type processes into the table
   (Add / Remove / Clear, Load CSV / Save CSV). Columns: PID, Arrival, Burst, Priority (lower =
   higher priority), Pages.
2. On the **Scheduler** page choose an algorithm. The quantum is enabled only for Round Robin.
3. **Run** shows the Gantt chart and the Results tab (with an Averages row, context switches and
   CPU utilisation).
4. **Run animated** replays the schedule with the thread engine. Watch the Gantt chart grow, the
   *Live process states* tab and the event log. Use Pause/Resume, Step and Stop, and the speed slider
   (50–1000 ms per tick).
5. **Compare all** fills the *Comparison* tab: a table (best value per column in bold green) and a bar chart.
6. **Integrated run** page: `File > Load sample > requests_integrated.csv` sets everything up
   (processes_srtf.csv, RR q=2). Tick or untick *Deadlock avoidance (Banker's)* and press
   **Run integrated**. With avoidance off a deadlock is detected and P1 is killed. Its resource state is
   copied to the Deadlock tab.

### Memory tab
1. Type a reference string (commas and/or spaces), choose frames (1–10) and FIFO, LRU, Optimal or *Compare all*.
2. **Run** shows one frame table per algorithm (red = page loaded on a fault, green = hit, last rows
   = HIT/FAULT and evicted page), the summaries and a faults chart.
3. **Standard example** (15/12/9 faults), **Belady example** (FIFO: 9 faults with 3 frames, 10 with 4)
   and **Random string** (20 pages from 0–9).
4. The **Page table** panel shows the page table of any process of the Scheduling tab when the physical
   frames are shared equally.

### Deadlock tab
1. **Load Banker's sample** or **Load deadlock sample**, or set the numbers of processes/resources and
   type the Allocation, Max, Request and Available tables (Need is computed).
2. **Check safety** shows `SAFE: <P1, P3, P4, P0, P2>` (green) or `UNSAFE` (red) with the step trace.
3. **Request by** a process with a vector such as `1 0 2`, then **Request resources**: granted (and
   applied) or denied with the reason.
4. **Detect deadlock** paints the deadlocked processes red in the tables and in the graph.
   **Recover** terminates victims one by one and redraws.
5. **Use Scheduling tab pids** labels the rows with the Scheduling tab's processes.

### Export
`File > Export report...` asks for a base name and writes `<name>.txt` (readable report) and
`<name>.csv` (sections separated by `## <section>` lines).
