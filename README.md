# OS-Sim: An Integrated Operating System Simulator

**CPU Scheduling, Memory Management, Deadlock Detection and Process Synchronization with a GUI**

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
The **Synchronization** tab shows race conditions, Peterson's solution, mutex locks, binary and
counting semaphores, the producer-consumer problem and monitors, one instruction at a time,
and runs each primitive with real POSIX threads.
**Phase 3** adds a **Storage & Files** tab (virtual filesystem, permissions, block allocation, recovery), a Heap
Laboratory, a live System Monitor, Job Control, Real-Time (RM / EDF) and Multilevel Queue scheduling, and the
readers-writers and dining-philosophers problems (details: `docs/PHASE3_MODULES.md`).

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
- **Process synchronization:** race condition (lost updates through read/modify/write
  interleaving), Peterson's solution (lecture variant, 2 processes), mutex locks, binary and counting
  semaphores, producer-consumer (bounded buffer with `empty`/`full`/`mutex`) and a monitor with
  condition variables. Each one is a deterministic instruction-level simulation: process cards with
  states and a per-process **Step** button (you choose the interleaving), an animated scene
  (entry section, busy-wait lane, critical section, waiting queues, bounded buffer), live shared
  variables, line-by-line pseudocode highlighting, an execution timeline, an event log, expected vs
  actual results and statistics, Run/Pause/Resume/Step/Stop/Reset and a speed slider, plus sample
  scenarios. A **Comparison** page runs the same two-process workload unsynchronized, with Peterson,
  a mutex and a binary semaphore. A **Real POSIX demo** page runs each primitive with real threads
  (`pthread_mutex`, `sem_t`, `pthread_cond`, `std::atomic` seq_cst for Peterson).
- **Phase 2 learning modules** (details: `docs/PHASE2_MODULES.md`). Every view is driven by a deterministic
  model in `core/` and shares one playback bar (Run, Pause/Resume, Prev, Next, Stop, Reset, speed, timeline scrubber):
  - **Processes:** 5-state / 2-state diagram with animated tokens, CPU and queues, process tree, PCB inspector,
    manual validated transitions; the automatic mode reuses the Scheduling tab's dispatcher.
  - **Threads:** Many-to-One / One-to-One / Many-to-Many mapping of user threads onto kernel contexts and cores,
    blocking and joins, model comparison, concurrency vs parallelism.
  - **IPC:** shared memory, direct / indirect message passing (bounded, blocking / non-blocking), pipes (byte stream, EOF).
  - **I/O:** polling vs interrupts vs DMA on one workload, animated system bus diagram, CPU / device / bus / DMA lanes,
    aligned comparison with defined metrics.
  - **Disk:** FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK with a platter and a seek graph, comparison of all six.
  - **Memory** (now a Memory area): the original page-replacement page plus Allocation & Fragmentation,
    Paging & Address Translation, Segmentation, Demand Paging, Swapping and Thrashing.
  - **Live Linux Demos** (clearly labelled) run real `fork/exec/wait/sleep`, zombie, orphan, signals, System V
    shared memory and message queues, pipes, FIFOs and pthreads in a separate helper program (`ossim_helper`).
- **Phase 3 subsystems** (details and assumptions: `docs/PHASE3_MODULES.md`):
  - **Storage & Files** (new tab, one shared in-memory virtual disk): *File Explorer* (tree, breadcrumb, editor,
    inode inspector, hard / symbolic links with loop detection, per-process descriptors with shared offsets, Live Linux
    syscall demo in a private temp dir), *Access Control* (owner/group/other precedence, search permission on every path
    component, parent-directory rules, chmod / chown / chgrp, effective-access matrix for every user), *Block Allocation*
    (contiguous / linked / indexed, free-space bitmap, access traversal, side-by-side comparison on cloned disks),
    *Recovery Lab* (deleted records with block generations, partial / full overwrite, signature carving, restore into a
    new file, hard-link / open-descriptor lifetime).
  - **Memory > Heap Laboratory:** malloc / calloc / realloc / free over a bounded byte arena with real in-arena headers,
    First / Best / Worst Fit, splitting and coalescing, a buddy allocator, double-free / bounds / leak detection.
  - **Processes > System Monitor** (Live System): CPU (total and per core), memory, disk I/O, sortable process table and
    tree, alerts with debounce, sampled on a worker thread from `/proc` (Windows API backend compiled on Windows only,
    unverified); an owned demo worker can be suspended / resumed / stopped. Host processes are read-only.
  - **Processes > Job Control:** jobs, process groups, foreground terminal ownership, fg / bg / Ctrl-Z / Ctrl-C /
    kill / wait in a safe educational shell, a pipeline over the IPC pipe model; Live Linux process-group demo.
  - **Scheduling > Real-Time Scheduling:** Rate Monotonic and EDF, releases / deadlines / misses, utilization tests
    with assumptions, RM-vs-EDF comparison, a fictional mission-control preset.
  - **Scheduling > Multilevel Queues:** MLQ and MLFQ (demotion, preemption by higher queues, priority boost).
  - **Synchronization:** readers-writers (readers' preference) and dining philosophers (deadlock vs asymmetric order).
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
ctest --output-on-failure     # 22 test programs + the workspace restart test, all must pass
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
./build/apps/ossim_console sync --real          # synchronization scenarios, comparison, real pthread demos
./build/apps/ossim_helper zombie                # one Live Linux demo on its own (fork exec wait sleep zombie
                                                # orphan signal shm msg pipe fifo "threads 4 10"
                                                # Phase 3: fsdemo jobs "worker 10 16 30")
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
│   ├── sync.*           synchronization simulator (race, Peterson, mutex, semaphores, buffer, monitor)
│   ├── disk.*           disk scheduling (Phase 2, M1)
│   ├── procmgmt.*       process lifecycle, reuses schedulerPick (M2)
│   ├── ipc.*            shared memory, message passing, pipes (M3)
│   ├── memadv.*         allocation, paging / segmentation translation, demand paging, swapping, thrashing (M4)
│   ├── iomodel.*        polling / interrupts / DMA (M5)
│   ├── threads.*        threading models (M6)
│   ├── vfs.*            Phase 3: virtual filesystem (Storage & Files: explorer, permissions, blocks, recovery)
│   ├── heap.*           Phase 3: heap allocator over a bounded arena (free list fits, buddy)
│   ├── sysmon.*         Phase 3: /proc parsers, CPU / rate deltas, alerts, bounded history
│   ├── jobctl.*         Phase 3: job-control shell model (reuses the IPC pipe)
│   ├── rtsched.*        Phase 3: RM / EDF real-time scheduling
│   ├── mlq.*            Phase 3: multilevel queue / feedback queue scheduling
│   ├── workspace.*      Modules menu model: module visibility rules and presets (not an OS algorithm)
│   └── io_formats.*     sample file loaders/savers
├── engine/            pthreads + semaphores + mutex + condvar + pipe (library osengine)
│   ├── engine.*         SimulationEngine, realProcessDemo (fork/exec/wait)
│   ├── logger.*         PipeLogger (pipe IPC)
│   ├── sync_demo.*      real pthread demos of every synchronization primitive (cancellable)
│   └── sysmon_native.*  Phase 3: System Monitor backends (Linux /proc, Windows API)
├── apps/console_main.cpp   console demo (ossim_console)
├── apps/ossim_helper.cpp   Live Linux demo helper (single-threaded, launched with QProcess)
├── gui/               Qt 6 Widgets application (ossim); SyncTab, SyncViews, SyncBridge = Synchronization tab;
│                      ModuleManager + WorkspaceUi = Modules menu (docs/WORKSPACE.md)
├── tests/             one test program per module, registered with ctest
├── samples/           process CSVs, reference strings, Banker/deadlock matrices, scripted requests
└── docs/              REPORT.md, VIVA_NOTES.md, PHASE2/3_MODULES.md, WORKSPACE.md, screenshots/
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

### Panels and window size
Every module has the same resizable layout: configuration panel (left), visualization (centre), inspector (right)
and, where present, a bottom area. Drag the splitter handles to resize; double-click a handle to restore the
defaults. Panels scroll when their content does not fit, so nothing is ever cut off, and the whole page scrolls if the
window is very small. The **View** menu has Reset Panel Layout (Ctrl+Shift+R), Show Configuration Panel
(Ctrl+Shift+C), Show Inspector Panel (Ctrl+Shift+I), Focus Simulation View (Ctrl+Shift+F) and the event log. Panel
sizes are remembered between runs. Layout check: `QT_QPA_PLATFORM=offscreen ./gui/ossim --layout-report 1366x768,1920x1080`.

### Phase 2 tabs (Processes, Threads, IPC, I/O, Disk, Memory sub-tabs)
Each tab has the same layout: configuration and presets on the left, the visualization in the middle, an inspector
on the right, the playback bar and pages (log, timeline, comparison, Live Linux demo) at the bottom. Choose a preset,
press **Run** (or **Next** step by step, **Prev** to go back), change inputs to rebuild. Live demos: open the
*Live Linux demo* page, pick a demo and press **Start live demo** (Stop cancels it safely). A recommended viva
demonstration order is in `docs/PHASE2_MODULES.md`.

### Modules menu (workspace customisation)
The **Modules** menu (after Help) shows or hides any main module or child view, for example to keep only the topics
of today's lecture. Tick or untick entries in the tree; the menu stays open. Parents show how many children are
visible. Quick actions: Show All Modules, Show Current Module Only, Reset to Default, Workspace Presets (Complete
OS-Sim, CPU & Process Management, Memory & Storage, Synchronization & Deadlocks, Linux Practical Lab, plus your own).
**Manage Workspace...** (Ctrl+Shift+M) opens a searchable dialog where you can save, rename and delete presets.
Hidden modules keep all their data; a running simulation is paused when hidden and resumed by you. The workspace is
remembered between runs. Details: `docs/WORKSPACE.md`.

### Phase 3 views
- **Storage & Files:** the four pages share one virtual disk. *Virtual disk* box: geometry and strategy, New virtual disk,
  Load sample, Save / Load image (validated). Explorer: pick the current user, type a name or path, use the buttons;
  the *Descriptors* tab opens files per process; *Live Linux syscalls* runs the real demo. Access Control: choose a
  path, read the matrix, tick bits + chmod or apply a preset. Block Allocation: allocate on the shared disk or compare
  the three strategies with a preset workload. Recovery Lab: load a preset, select a candidate, restore or carve.
- **Memory > Heap Laboratory:** choose allocator, fit, arena and alignment; load a preset or write a script
  (`a = malloc 100; free a`); apply manual operations (after Prev they start a new branch); click blocks in the map.
- **Processes > System Monitor:** sampling starts when the page is shown (Start / Pause / Stop); filter, sort, use the
  tree; the demo worker box starts a bounded worker that can be suspended, resumed, stopped or killed (confirmation).
- **Processes > Job Control:** pick a scenario and play it, or type commands (`help`), or use the buttons.
- **Scheduling > Real-Time / Multilevel Queues:** choose a preset, edit tasks / queues / processes, play the trace,
  compare RM vs EDF.

### Synchronization tab
0. Phase 3 added *Readers-writers* and *Dining philosophers* (naive deadlock vs asymmetric order) to the list.
1. Choose an **algorithm** and a **sample scenario** (e.g. *Race condition > Lost update*). The
   scenario fills the parameters; changing a parameter switches to *Custom settings*.
2. **Run** animates one instruction per tick (speed slider 50-1000 ms). **Pause** / **Resume**,
   **Step** (one instruction chosen by the scheduler), **Stop**, **Reset**, **Run to end**.
3. To choose the interleaving yourself, press the **Step** button on a process card: that process
   executes exactly one line (e.g. P0 read, P1 read, P0 inc, P0 write, P1 inc, P1 write loses an update).
4. Watch the scene (who is in the critical section, who busy-waits, who sleeps in which queue), the
   pseudocode (chip = next line of each process, colour = line just executed), the live variables,
   the timeline and the statistics (expected vs actual, lost updates, violations, waiting steps).
5. **Comparison** page: the same 2-process counter workload unsynchronized / Peterson / mutex /
   binary semaphore, table + chart, theoretical guarantee vs what this run observed.
6. **Real POSIX demo** page: runs the primitive with real threads on a background thread (the GUI
   stays responsive; **Stop** cancels it safely) and checks the result.

### Export
`File > Export report...` asks for a base name and writes `<name>.txt` (readable report) and
`<name>.csv` (sections separated by `## <section>` lines).
