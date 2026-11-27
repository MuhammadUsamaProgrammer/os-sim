# OS-Sim Phase 2: module documentation

Phase 2 adds six modules to OS-Sim: Disk Scheduling (M1), Process Management (M2), IPC (M3),
Advanced Memory (M4), I/O Management (M5) and Multithreading (M6). Every visualisation, table and
metric is computed by a deterministic model in `core/` (pure C++17, no Qt, no threads). The GUI in
`gui/` only displays snapshots of a precomputed trace. The Process Synchronization module (Stage 11)
and all earlier modules were not changed, apart from the Memory tab becoming the first sub-tab of
the Memory area.

**Simulation vs Live Linux Demo.** Pages marked **Simulation** are deterministic: the same input
always gives the same trace, timed in *logical ticks* or *steps*, never wall-clock time. Pages
marked **Live Linux Demo** run real system calls in the helper program `apps/ossim_helper`
(launched with `QProcess`, never `fork()` inside the multithreaded Qt process). They show only the
real return values, PIDs and exit statuses. Their timings are measured and vary between runs.

**Common controls** (`gui/ModuleKit.*`, `PlaybackBar`):

| Control | Meaning |
|---|---|
| Run | play the trace one step per tick (restarts if at the end) |
| Pause / Resume | stop / continue playback at the current step |
| ◀ Prev / Next ▶ | restore the previous snapshot / show the next step |
| Stop | halt and return to the initial state (step 0) |
| Reset | rebuild the trace from the current inputs (old history discarded) |
| Speed slider | wall-clock animation rate only - results never change |
| Timeline scrubber | jump to any step |

Changing any input rebuilds the trace and returns to step 0. In the manual modes (Process
Management manual transitions, IPC operations), a command issued after stepping back discards the
later steps: it starts a new branch, and the GUI says so.

**Semantic colours** (always shown together with a text label): grey = idle / New, blue = ready /
pending, green = running / active / hit, orange = waiting / blocked, red = error / fault / trap,
purple = completed / terminated.

---

## M1 Disk Scheduling (`core/disk.*`, `gui/DiskTab.*`, `gui/DiskViews.*`, `tests/test_disk.cpp`)

**Concept.** An HDD request queue is a list of cylinders. The scheduling algorithm decides the
service order, and the order decides the total head movement. This is an educational
cylinder-distance model: there is no seek time in ms and no SSD behaviour.

**Algorithms.** FCFS, SSTF, SCAN, C-SCAN, LOOK and C-LOOK, each with an initial direction (higher
or lower).

**Conventions (finite batch)**
- **SSTF ties:** equal distances go to the earlier request in input order.
- **Head position:** requests at the head position are serviced first, with distance 0.
- **SCAN / C-SCAN:** travel to the physical boundary only when requests remain behind the head.
- **LOOK / C-LOOK:** turn (or jump) at the furthest outstanding request.
- **Transit only:** a boundary visited only for movement is not a serviced request.
- **Wrap distance:** the C-SCAN / C-LOOK circular return is counted in the total.
- **Duplicates:** duplicate cylinders stay distinct requests, served in input order.

**Inputs.**
- cylinder range (default 0..199)
- initial head (53)
- direction
- request table: edit, add, delete, move up / down
- reproducible random queue (`std::mt19937(seed)`)
- sample files `samples/disk_*.txt`

**Outputs.** One `DiskMove` per head movement: from, to, distance, cumulative total, kind
(service / boundary / wrap), reversal flag, reason and pseudocode line. The platter, seek graph,
service table, log, metrics and comparison chart are all drawn from this same move list.

**Metrics.** Total head movement = sum of |next − current| including boundary and wrap travel.
Average = total ÷ serviced requests (shown as "n/a" for an empty queue).

**Reference** (`samples/disk_reference.txt`): requests `98 183 37 122 14 124 65 67`, head 53,
bounds 0..199, direction higher. Real output:

| Algorithm | Total | Service order |
|---|---:|---|
| FCFS | 640 | 98, 183, 37, 122, 14, 124, 65, 67 |
| SSTF | 236 | 65, 67, 37, 14, 98, 122, 124, 183 |
| SCAN | 331 | 65, 67, 98, 122, 124, 183, 37, 14 |
| C-SCAN | 382 | 65, 67, 98, 122, 124, 183, 14, 37 |
| LOOK | 299 | 65, 67, 98, 122, 124, 183, 37, 14 |
| C-LOOK | 322 | 65, 67, 98, 122, 124, 183, 14, 37 |

Direction lower (hand-derived, tested): SCAN 236, LOOK 208, C-SCAN 386, C-LOOK 326.

**Visual legend.**
- **Platter:** blue dots = pending requests, green = being serviced, purple = serviced; dashed ring
  = current track.
- **Seek graph:** solid = service move, grey dashed = boundary transit, red dash-dot = wrap
  (counted).

**Tests.** 487 checks:
- the six reference cases
- the reverse direction
- duplicates and requests at the head
- SSTF ties
- boundaries, and all requests on one side
- a single request and an empty queue
- 50 random workloads where every request is serviced exactly once and the move list is consistent
- validation and the sample file loader

**Limitations.** Static batch only (no dynamic arrivals). No rotational latency or seek-time model.

---

## M2 Process Management (`core/procmgmt.*`, `gui/ProcessTab.*`, `gui/LifecycleViews.*`, `tests/test_procmgmt.cpp`)

**Concept.** The 5-state model (New, Ready, Running, Waiting, Terminated) and the 2-state view
(Running / Not Running, where Not Running aggregates the other live states). It also covers the
PCB and the process tree.

| Transition | Trigger | Function |
|---|---|---|
| New → Ready | admission | `admit()` |
| Ready → Running | dispatch (CPU must be free) | `dispatch()` |
| Running → Ready | preemption / quantum expiry | `preempt()` |
| Running → Waiting | blocking I/O | `blockIO()` |
| Waiting → Ready | I/O completion (never Waiting → Running) | `completeIO()` |
| Running → Terminated | exit | `exitRunning()` |
| New/Ready/Waiting → Terminated | separately labelled **kill** | `kill()` |

Every transition is validated, and a refused command returns the reason. There is one logical
CPU: at most one process is Running.

**Reuse of the CPU scheduler.** The automatic mode asks the existing dispatcher, `schedulerPick()`
from `core/scheduler.cpp`, for its decision on copies of the scheduler state. It then applies that
decision through the validated transitions. FCFS, SJF, SRTF, RR and Priority NP/P therefore behave
exactly as in the Scheduling tab. A test checks that CPU-only workloads give the same completion
times as `runScheduler()` for all six policies.

**Event order inside a tick:** arrivals (create + admit) → I/O completions → dispatcher decision →
one tick of execution → end-of-burst handling (block on I/O or exit).

**PCB fields.**
- pid, ppid, name, state, priority, arrival
- CPU used / total, current CPU burst left, I/O left, bursts
- program counter (modelled instruction index)
- **modelled** register values (labelled; not real Linux registers)
- waiting reason, memory (pages, modelled)
- ready / waiting time, dispatches, first run / finish

Fields that changed since the previous step are highlighted.

**Reference** (`samples/lifecycle_reference.csv`): P2 = CPU 2, I/O 3, CPU 2; P3 = CPU 4; both
arrive at t=0; FCFS. Real event sequence: create P2, admit P2, create P3, admit P3, dispatch P2,
block on I/O P2, dispatch P3, I/O complete P2, exit P3, dispatch P2, exit P2. P3 finishes at t=6
and P2 at t=8.

**Other presets.**
- round robin with preemptions
- CPU idle while every process waits
- a process tree: bash → ls, make → gcc (`samples/lifecycle_tree.csv`)

**Manual mode.** Every transition is a button. The self-test checks these refusals:
- a second dispatch while the CPU is busy
- Waiting → Running
- a command after Prev, which starts a new branch

**Live Linux demos** (`ossim_helper fork|exec|wait|sleep|zombie|orphan|signal`):
- **fork:** parent and child PIDs; fork returns 0 in the child and the child's PID in the parent.
- **exec:** the same PID before and after `execl`, then exit status 3.
- **wait:** three children reaped with statuses 1, 2 and 3.
- **sleep:** a real `sleep(1)`.
- **zombie:** `/proc/<pid>/stat` shows `Z` before `waitpid` and the entry is gone afterwards.
- **orphan:** the helper sets `PR_SET_CHILD_SUBREAPER`, so the orphan is re-parented to the helper
  (elsewhere it would go to init or a session subreaper) and is reaped by it.
- **signal:** `SIGUSR1` is sent only to the demo's own child; the handler only sets a
  `sig_atomic_t` flag.

**Tests.**
- **`test_procmgmt` (62 checks):** the reference sequence; queue / state invariants after every
  event; scheduler consistency; preemption; idle CPU; the process tree; every validation rule;
  truncation and new branches; determinism; the sample loader.
- **`test_native`:** the real demos twice each (PIDs, return values, statuses, zombie and orphan
  facts, cleanup).

---

## M3 Inter-Process Communication (`core/ipc.*`, `gui/IpcTab.*`, `tests/test_ipc.cpp`)

**Concept.** Shared memory, message passing (direct and indirect, bounded FIFO, blocking and
non-blocking) and pipes (byte stream, EOF). Synchronization algorithms are not repeated here: the
shared-memory preset uses a deterministic hand-off and points to the Synchronization tab.

**Model.** Every operation is one event. A blocked operation stays pending and completes in a
separate **WAKE-UP** event. A failed operation is recorded as **FAILED** with its reason.

**Shared memory.**
- create (modelled id 4711, permissions 0600 labelled illustrative)
- attach read-write or read-only, at a different modelled virtual address per process
- write / read by offset, with a bounds check
- detach
- `IPC_RMID` marks the segment for removal; it is destroyed after the last detach, and new
  attaches are refused

**Message passing.** Indirect communication uses a mailbox with bounded capacity. Direct
communication uses one link per sender / receiver pair.
- A send to a full queue blocks, or fails at once if non-blocking.
- A receive from an empty queue blocks, or fails if non-blocking.
- Delivery is FIFO.

**Pipes.** Each process holds read / write ends that it closes explicitly. The pipe has a modelled
capacity (not the real Linux capacity) and carries a byte stream with no message boundaries.
- A read on an empty pipe with an open writer blocks: this is not EOF.
- EOF (`read` returns 0) comes only when the pipe is empty and every write end is closed.
- A write to a full pipe writes what fits, then blocks.
- A write with no reader fails with EPIPE.

**Presets.**
- shared memory: HELLO
- shared memory: error cases
- mailbox with capacity 2 and two messages
- receiver blocks on an empty mailbox
- direct link with capacity 1
- anonymous pipe with EOF
- full pipe
- empty pipe with an open writer

**Live Linux demos** (`ossim_helper shm|msg|pipe|fifo`):
- **System V shared memory:** `shmget(IPC_PRIVATE)`, `shmat`, `shmdt`, `shmctl(IPC_RMID)`, with a
  pipe hand-off.
- **System V message queue:** a non-blocking `msgrcv` on the empty queue fails with ENOMSG; then a
  blocking receive gets "first" then "second"; then `msgctl(IPC_RMID)`.
- **Anonymous pipe:** reads in 8-byte pieces until EOF.
- **FIFO:** `mkfifo` in a private `mkdtemp` directory, a separately exec'ed writer, then everything
  is removed.

**Comparison panel.** A qualitative table: data path, message boundaries, coordination and typical
use. It makes no performance claims.

**Tests.**
- **`test_ipc` (52 checks):** every preset; FIFO order; exact payloads; capacity never exceeded;
  blocking vs non-blocking; EOF rules; EPIPE; detach and removal; invalid offsets / processes;
  replay after stepping back; determinism.
- **`test_native`:** the real HELLO payload, the ENOMSG failure, FIFO message order, exact pipe
  bytes then EOF, and removal of every object.
- **Manual check:** no shared-memory segment, message queue or FIFO was left behind after the
  runs (`ipcs`, `ls /tmp/ossim-fifo-*`).

---

## M4 Advanced Memory (`core/memadv.*`, `gui/MemAdvTab.*`, `gui/MemAdvViews.*`, `tests/test_memadv.cpp`)

The Memory tab is now a Memory **area**. Its first sub-tab is the original page-replacement page
(unchanged: 15 / 12 / 9, Belady). Six sub-tabs follow.

**Allocation & Fragmentation.**
- **Variable partitions:** First / Best / Worst Fit (ties → lowest address), splitting, and
  coalescing on free.
- **Fixed partitions:** internal fragmentation is shown hatched red.
- **Compaction:** blocks move and their base addresses change, which needs run-time relocation.
- **Base / limit check:** valid when `0 <= offset < limit`, then physical = base + offset;
  otherwise a modelled protection fault.

The three algorithms run side by side on the same operations. Holes that are large enough are
dashed blue and the chosen hole is green.
- **Metrics:** allocated, requested, free, number of holes, largest hole, internal waste, failures,
  and failures with enough total free space (external fragmentation).
- **Reference:** holes 100, 500, 200, 300, 600; requests 212, 417, 112, 426. First Fit and Worst
  Fit cannot place 426 (959 units are free in total); Best Fit places everything.

**Paging & Address Translation.**
- page = address / page size, offset = address % page size, physical = frame × page size + offset.
- Steps: validate → split (bit fields shown) → page-table lookup → check valid / present / write
  bits → frame → physical address → byte inside the frame.
- Three different failures are distinguished: an illegal address (outside the space), an invalid
  page, and a page fault (valid but not present). A write to a read-only page is a protection
  violation.
- Nothing is ever mapped to frame 0 silently. Duplicate frames are refused unless shared mappings
  are enabled. Arithmetic is overflow-checked.
- **Reference:** page size 1024, address 2500 → page 2, offset 452, frame 5, physical 5572.
- **Presets:** zero, last byte, page boundary, non-resident page, invalid page, out of range, write
  to a read-only page.

**Segmentation.** (segment, offset) → segment table → limit check → permission check →
base + offset. Segments have variable sizes and the table must not overlap.
- (2, 250) with base 4000 and limit 600 → 4250.
- (2, 600) → protection fault.

**Demand Paging.**
- **Reuse:** victim selection is the **same `FramePool`** code as the page-replacement tab, plus
  dirty bits.
- **Fault steps:** check the page table → free frame or victim → write back a dirty victim → load →
  update → restart.
- **Cost model:** access, page-in and write-back costs are labelled model parameters.
- **Optimal:** labelled an offline baseline, because it needs the future.
- **Reference:** `7,0,1,2,0,3,0,4,2,3,0,3,2`, three frames → FIFO 10, LRU 9, Optimal 7. On the
  standard 20-reference string the frame contents equal `runPageReplacement` at every step.

**Swapping.** Whole processes move between RAM and the backing store; the process loaded longest
ago goes out first. This is illustrative: no host memory is touched.

**Thrashing.**
- **Rolling fault rate, working set vs frames:** two frame counts on the same references.
- **Reference:** cyclic `0 1 2 3` × 5 with LRU → 3 frames fault on all 20 references, 4 frames
  fault only on the first 4.
- **Multiprogramming model (logical, labelled).** Each process loops over its working set; a fault
  makes that process wait `faultTime` ticks on a single paging disk.

  makespan = max(total CPU, total disk, longest single process)
  utilisation = CPU ÷ makespan

  With the defaults (24 frames, working set 4, 200 references, fault time 10) utilisation peaks at
  1.00 at degree 2 and collapses to 0.10 at degree 12.

**Tests.** `test_memadv` (108 checks), plus the unchanged `test_memory`.

---

## M5 I/O Management (`core/iomodel.*`, `gui/IoTab.*`, `gui/IoViews.*`, `tests/test_io.cpp`)

**Model.** Conceptual logical ticks; not hardware emulation. The three modes share one workload:
N units, a device that readies a unit every R ticks, a controller buffer of B units (back-pressure
when full) and W ticks of useful CPU work.

**Event order inside a tick.**
1. The device produces a unit, and in interrupt mode raises an IRQ.
2. The CPU acts. Interrupts are taken at the tick boundary and wait in a pending queue, so none are
   lost.
3. The DMA controller transfers.
4. After its last unit, the DMA controller raises one completion interrupt.

**CPU state.** Each tick the CPU is in exactly one state: Useful, Poll, Copy, ISR, DMA setup, DMA
completion ISR, Idle or Bus stall.

**The three modes.**
- **Polling:** check the status every P ticks (1 tick each); if data is ready, copy every buffered
  unit at C ticks each.
- **Interrupt-driven:** an ISR of I ticks, then copy that unit (C ticks).
- **DMA:** setup S ticks (source, destination, count, direction). Each unit takes D ticks; only its
  last tick is a bus cycle. Completion ISR takes E ticks.
- **Cycle stealing** (optional): a useful-work tick that coincides with a DMA bus cycle becomes a
  bus stall.

**Metrics.**
- latency (ticks)
- useful work, and useful % of the observation window
- overhead (poll + copy + ISR + setup + completion)
- idle, stall
- polls, interrupts, transferred units, throughput (units per tick)

The comparison uses a **common window** (the slowest latency) for all three modes.

**Hand-traced reference** (N=2, R=3, P=2, C=1, I=1, S=2, E=1, D=1), verified by the tests:

| Mode | CPU per tick | Latency | Useful |
|---|---|---|---|
| Polling | P U P U P C P C | 8 | 2 |
| Interrupt | U U U I C U I C | 8 | 4 |
| DMA | S S U U U U U D | 8 | 5 |

**Other presets** (real numbers, common window):
- **Small transfer (1 unit):** DMA overhead 5 ticks vs interrupt overhead 2, so DMA is not always
  best.
- **Bulk transfer (32 units):** DMA useful 94 vs interrupt 1, with interrupt overhead 96.
- **Slow device:** polling makes many wasted checks.
- **Cycle stealing:** exactly 32 stalled ticks, one per unit.
- **Back-pressure:** the device stalls on a full buffer; nothing is lost.

**Tests.** `test_io` (49 checks).

---

## M6 Multithreading (`core/threads.*`, `gui/ThreadsTab.*`, `tests/test_threads.cpp`)

**Models.** Educational only: Linux pthreads are one-to-one.
- **Many-to-One:** one kernel context; a blocking call stalls the whole process.
- **One-to-One:** one context per thread; a blocking call blocks only that thread.
- **Many-to-Many:** a pool of K contexts. A blocked thread keeps its context and no new context is
  created, so parallelism ≤ min(cores, free contexts).

**Rules.**
- Round robin with a quantum.
- Threads keep their core / context when possible, and migrations are counted.
- Joins: a thread finishes its own work, waits (Joining) and completes in the tick its target
  completes.
- Every tick is checked: each context runs ≤ 1 thread, each core ≤ 1 context, and no thread runs on
  two cores.

**Reference** (3 threads × 4 ticks, no blocking): Many-to-One 12 ticks on any number of cores;
One-to-One with 3 cores 4 ticks; Many-to-Many with 2 contexts, 2 cores and quantum 1 6 ticks
(work-conserving). With quantum 4, Many-to-Many takes 8 ticks.

**Blocking example.** T0 blocks after 1 tick for 3 ticks: Many-to-One takes 15 ticks (the whole
process stalls), One-to-One takes 7.

**Join example.** A, then B, then main (A finishes at 3, B and main at 6).

**GUI.**
- user-thread, kernel-context and core lanes with animated mapping links
- per-thread and per-core timelines
- a thread inspector
- a three-model comparison (completion, max parallel, core occupancy, waiting, stalled,
  migrations)
- a concurrency (1 core) vs parallelism (N cores) page

**Live Linux demo** (`ossim_helper threads N base`). Real `pthread_create` with stable per-thread
argument structs. Odd threads return through `pthread_exit` and even threads return normally. The
main thread runs `pthread_join` in order and checks each result against the formula. Linux TIDs are
shown and labelled platform specific. No detach and no asynchronous cancellation.

**Tests.** `test_threads` (35 checks), plus `test_native` (argument / result correctness).

---

## Demonstration order for the viva (about 10 minutes)

1. **Disk**
   - Load `disk_reference.txt` and choose SCAN. Run it: the head goes to 199 (dashed transit), then
     reverses.
   - Open **Compare all six**: 640 / 236 / 331 / 382 / 299 / 322.
2. **Processes**
   - Use the reference preset and step to t=2: P2 moves to WAITING and P3 is dispatched.
   - Switch to Manual: try *Dispatch* on a waiting process (refused).
   - Live demo **zombie**: state `Z` until `waitpid`.
3. **I/O**
   - Load the hand-traced preset. Compare the three CPU rows (P U P U..., U U U I C..., S S U U...).
   - Load the bulk preset: DMA keeps the CPU useful.
   - Load the small-transfer preset: DMA loses.
4. **Memory area**
   - Allocation preset: P4 fails with First and Worst Fit, fits with Best Fit.
   - Paging: 2500 → 5572.
   - Segmentation: offset 600 traps.
   - Demand paging: 10 / 9 / 7.
   - Thrashing: 3 frames give 20 faults, 4 frames give 4.
5. **IPC**
   - Shared-memory HELLO.
   - The receiver that blocks on an empty mailbox.
   - The pipe preset reaching EOF.
   - Live demo **msg**: a real ENOMSG.
6. **Threads**
   - Show the three reference presets (12 / 4 / 6).
   - The blocking presets: Many-to-One stalls, One-to-One does not.
   - The live demo with 4 threads.

## Limitations

- **Disk:** static batch only; distance in cylinders (no rotational latency or seek-time model).
- **Process management:** single CPU. The manual mode does not force an exit at the end of a CPU
  burst; the user decides. Register values are illustrative.
- **IPC:** the simulated capacities, ids, permissions and virtual addresses are modelled values.
  Real pipe capacity and System V ordering rules come from the OS (see the live demo).
- **Memory:** sizes are abstract units and there is no TLB. Swapping and the multiprogramming curve
  are labelled models.
- **I/O:** a single device, single bus and single DMA channel. It is not cycle accurate.
- **Threads:**
  - Many-to-Many does not create extra contexts when all of them block.
  - User-level scheduling is plain round robin.
  - The live demo cannot prove parallel execution.
- **Live Linux demos:** need Linux and the `ossim_helper` binary built next to the GUI. Elsewhere
  the page shows an availability message and the simulations still work. Zombie observation uses
  `/proc`.
- **Visual checks:** done with offscreen screenshots (light theme, dark theme, 1100×700), not by a
  human on a physical display.
