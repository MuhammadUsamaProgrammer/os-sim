# Built-in samples: the Load Sample system

Every module and sub-module of OS-Sim has a prominent **Load Sample** button at the top of its
configuration panel. It opens the samples of *that* module only, grouped into categories. Loading a
sample fills the inputs (and clears old results); it never starts the simulation unless you choose
**Load && Run** in the *Browse samples* dialog. 180 samples in 24 modules.

## Load Sample vs Load File

| | What it does | Where |
|---|---|---|
| **Load Sample** | loads a predefined, built-in educational workload (no file dialog) | the button in every module; `File > Load Built-in Sample` (same catalogue, by module) |
| **Load sample** (file list) | the original menu: every file in `samples/`, routed by its file name | `File > Load sample` (unchanged) |
| **Open File...** | imports YOUR file; known names are routed automatically, otherwise you choose the importer (processes CSV, reference string, matrices, lifecycle CSV, disk requests, virtual disk image) | `File > Open File...` (Ctrl+O) |

## Architecture

```
core preset providers            tab presets / setters             samples/*.csv *.txt
(lifecyclePresets, syncScenarios, (loadPreset(i), loadScenario(i),   (embedded as :/samples,
 heapPresets, ipcPresets, ...)     loadCsv, loadFile, prefill...)     ui::samplesDir())
            \                              |                              /
             +-------------- gui/SampleCatalog.cpp (adapters) ------------+
                                           |
                              gui/SampleRegistry  (SampleDef list)
                              load(id): guard -> handler -> loaded()
                    /                      |                        \
   ModuleKit::SampleButton        File > Load Built-in Sample     --selftest / --sample-report
   (one per module page, in the    (reveal a hidden module          (all samples, contrast,
    sample slot of the config       only after asking)               clipping, visibility)
    panel made by assembleModule)
```

- **`SampleDef`** (`gui/SampleRegistry.h`): stable id (`<module>/<key>`), name, description, module id
  (the Module Visibility Manager id, e.g. `memory.heap`), category, mode / algorithm, parameters, level,
  backing file, `replacesShared` (Storage virtual disk), `load` handler, optional `run` handler.
- **Adapters** (`MainWindow::registerSamples`, `gui/SampleCatalog.cpp`) wrap the EXISTING presets and
  sample files; no workload is defined twice. GUI-local preset tables expose their names through small
  static accessors (`DiskTab::presetNames`, `MemAdvArea::presetList`, `AccessPage::presetNames`).
- **Guard** (`MainWindow::sampleGuard`): if a simulation of that page is running (animated replay,
  playback bar, synchronization animation) the user confirms, then it is stopped / paused; Storage &
  Files samples confirm before they replace the shared virtual disk. Never a prompt in the self-test.
- **Button** (`SampleButton`, `gui/ModuleKit`): accent colour = the palette's highlight colour (theme
  aware, text colour chosen by WCAG contrast: 4.88:1 light, 5.08:1 dark), folder icon, readable label,
  tooltip, menu with category sections (tooltip per entry: description, mode, parameters), *Browse
  samples* dialog, and a line naming the loaded sample. `assembleModule()` wraps every configuration
  panel with a sample slot (`withSampleSlot`); the four original pages call it themselves. The config
  panel's minimum width includes the button, so it is never clipped.
- **Resources**: `samples/*` is compiled in (`qt_add_resources`, prefix `:/samples`). `ui::samplesDir()`
  looks for `$OSSIM_SAMPLES`, then a folder next to the executable / working directory, and otherwise
  extracts the embedded copy to a private temporary folder. No absolute path is compiled in.
- **Workspace**: hidden modules keep their samples; loading never changes visibility; a File-menu
  sample for a hidden module asks before revealing it.
- **Native demos** (System Monitor worker, Live Linux demos) are only *prepared* by a sample; the user
  always starts them.

## Tests

`--selftest` section "samples" (every module: button, relevance, every sample loads, correct mode,
editing, repeated loads, guard, dialog, File menu, workspace, themes, 1024×768 and 900×650) and
ctest `gui_samples_anywhere` (`ossim --sample-report --load-all` from another directory with only
the embedded copy). Command: `QT_QPA_PLATFORM=offscreen ./gui/ossim --sample-report [--load-all]`.

## Inventory (generated with `ossim --sample-report`)

Format: module [id] - count / category / sample (mode, backing file).

```
Scheduling > CPU Scheduling [scheduling.cpu] - 15 sample(s)
  Basic Examples
    - FCFS: convoy effect (24 / 3 / 3)  (FCFS, samples/processes_fcfs.csv)
    - SJF: shortest job first (same input)  (SJF, samples/processes_fcfs.csv)
    - SRTF: preemption by a shorter job  (SRTF, samples/processes_srtf.csv)
    - Round Robin, quantum 4  (Round Robin q=4, samples/processes_fcfs.csv)
    - Priority (non-preemptive)  (Priority (NP), samples/processes_priority.csv)
  Algorithm-Specific Examples
    - LJF: longest job first  (LJF, samples/processes_ljf_lrtf.csv)
    - LRTF: preemption by a longer remaining time  (LRTF, samples/processes_ljf_lrtf.csv)
    - Priority (preemptive)  (Priority (P), samples/processes_priority.csv)
  Special Cases
    - LJF: simultaneous arrivals, equal bursts  (LJF, samples/processes_ljf_ties.csv)
    - LRTF: equal remaining times  (LRTF, samples/processes_ljf_ties.csv)
    - CPU idle interval  (FCFS, samples/processes_idle_gap.csv)
    - Starvation of a short job under LJF  (LJF, samples/processes_ljf_starvation.csv)
  Advanced Scenarios
    - Algorithm comparison workload  (Compare all, samples/processes_srtf.csv)
    - Integrated run: Banker's avoidance ON  (Integrated (RR q=2, LRU, avoidance), samples/requests_integrated.csv)
    - Integrated run: detection and recovery  (Integrated (RR q=2, LRU, detection))
Scheduling > Multilevel Queues [scheduling.mlq] - 3 sample(s)
  Basic Examples
    - MLQ: system / interactive / batch  (MLQ)
  Algorithm-Specific Examples
    - MLFQ: long jobs sink, short jobs stay on top  (MLFQ)
  Advanced Scenarios
    - MLFQ with priority boost (no starvation)  (MLFQ)
Scheduling > Real-Time Scheduling [scheduling.realtime] - 5 sample(s)
  Algorithm-Specific Examples
    - RM misses, EDF does not (A 2/5, B 4/7)  (RM / EDF)
  Basic Examples
    - Feasible under both (U = 0.70)  (RM / EDF)
  Special Cases
    - Overload (U = 1.17)  (RM / EDF)
    - Constrained deadlines (D < T)  (RM / EDF)
  Course Examples
    - Fictional mission-control workload  (RM / EDF)
Processes > Process Lifecycle [processes.lifecycle] - 10 sample(s)
  Basic Examples
    - Two processes and one I/O (reference)  (Five-state model, FCFS)
    - Round robin with preemption (q=2)  (Five-state model, Round Robin)
  Special Cases
    - Edge: CPU idle while every process waits for I/O  (Five-state model, FCFS)
  Advanced Scenarios
    - Process tree: shell with children and a grandchild  (Five-state model, SRTF)
  Course Examples
    - Reference workload (file)  (Five-state model, samples/lifecycle_reference.csv)
    - Parent-child process tree (file)  (Process tree, samples/lifecycle_tree.csv)
  Live Linux Demos (start manually)
    - Live Linux demo: fork  (Live Linux Demo)
    - Live Linux demo: wait  (Live Linux Demo)
    - Live Linux demo: zombie  (Live Linux Demo)
    - Live Linux demo: orphan  (Live Linux Demo)
Processes > System Monitor [processes.monitor] - 4 sample(s)
  Controlled demo workloads
    - Controlled demo workload  (Live System (read-only) + owned demo worker)
    - CPU-intensive demo  (Live System (read-only) + owned demo worker)
    - Memory-intensive demo  (Live System (read-only) + owned demo worker)
    - Resource alert demonstration  (Live System (read-only) + owned demo worker)
Processes > Job Control [processes.jobs] - 7 sample(s)
  Basic Examples
    - Background job: stop, resume, foreground, complete  (Simulated shell)
    - Foreground job: Ctrl-Z, bg, fg  (Simulated shell)
    - Background is not blocked  (Simulated shell)
  Advanced Scenarios
    - Background read: SIGTTIN  (Simulated shell)
    - Two-stage pipeline  (Simulated shell)
    - Ctrl-C, kill and wait  (Simulated shell)
  Special Cases
    - Invalid references are refused  (Simulated shell)
Memory > Page replacement [memory.replacement] - 6 sample(s)
  Course Examples
    - Standard string, FIFO  (FIFO, samples/refstring_standard.txt)
    - Standard string, LRU  (LRU, samples/refstring_standard.txt)
    - Standard string, Optimal  (Optimal, samples/refstring_standard.txt)
  Algorithm-Specific Examples
    - Standard string, compare all  (Compare all, samples/refstring_standard.txt)
  Special Cases
    - Belady's anomaly, 3 frames  (FIFO, samples/refstring_belady.txt)
    - Belady's anomaly, 4 frames  (FIFO, samples/refstring_belady.txt)
Memory > Allocation & Fragmentation [memory.allocation] - 4 sample(s)
  Course Examples
    - Textbook: holes 100 500 200 300 600  (First / Best / Worst Fit)
  Basic Examples
    - Free and coalesce  (First / Best / Worst Fit)
    - Compaction  (First / Best / Worst Fit)
    - Fixed partitions (internal fragmentation)  (First / Best / Worst Fit)
Memory > Paging & Address Translation [memory.paging] - 8 sample(s)
  Course Examples
    - Reference: address 2500 -> 5572  (Paging: address translation)
  Special Cases
    - Zero address  (Paging: address translation)
    - Last byte of page 0 (1023)  (Paging: address translation)
    - Exact page boundary (1024)  (Paging: address translation)
    - Invalid page (8191)  (Paging: address translation)
    - Outside the address space (8192)  (Paging: address translation)
    - Write to read-only page 1 (1100)  (Paging: address translation)
  Basic Examples
    - Valid page not resident (4101) -> page fault  (Paging: address translation)
Memory > Segmentation [memory.segmentation] - 5 sample(s)
  Course Examples
    - Valid: segment 2, offset 250 -> 4250  (Segmentation)
  Special Cases
    - Invalid: segment 2, offset 600 (limit 600)  (Segmentation)
    - Last valid byte: segment 2, offset 599  (Segmentation)
    - No such segment (9)  (Segmentation)
    - Write to the code segment (0)  (Segmentation)
Memory > Demand Paging [memory.demand] - 3 sample(s)
  Course Examples
    - Reference string (13 refs, 3 frames)  (Demand paging (FIFO / LRU / Optimal))
  Basic Examples
    - Dirty pages and write-back  (Demand paging (FIFO / LRU / Optimal))
  Special Cases
    - Edge: one frame  (Demand paging (FIFO / LRU / Optimal))
Memory > Swapping [memory.swapping] - 3 sample(s)
  Course Examples
    - Reference: 4 processes, RAM 100  (Swapping (whole processes))
  Basic Examples
    - Everything fits (no swapping)  (Swapping (whole processes))
    - Ping-pong: two big processes, RAM 60  (Swapping (whole processes))
Memory > Thrashing [memory.thrashing] - 2 sample(s)
  Course Examples
    - Cyclic 0 1 2 3 x5, LRU, 3 vs 4 frames  (Thrashing / working set)
  Basic Examples
    - Locality string, 3 vs 6 frames  (Thrashing / working set)
Memory > Heap Laboratory [memory.heap] - 8 sample(s)
  Basic Examples
    - Basics: split, free, coalesce  (Free list (first/best/worst fit), First Fit)
  Advanced Scenarios
    - First / Best / Worst Fit choice  (Free list (first/best/worst fit), Best Fit)
    - Fragmentation: enough free space, no block  (Free list (first/best/worst fit), First Fit)
    - realloc: in place, move, failure  (Free list (first/best/worst fit), First Fit)
    - calloc zeroing and overflow check  (Free list (first/best/worst fit), First Fit)
  Special Cases
    - Errors: double free, invalid handle, bounds, leak  (Free list (first/best/worst fit), First Fit)
    - Corruption inside the model  (Free list (first/best/worst fit), First Fit)
  Algorithm-Specific Examples
    - Buddy allocator: split and merge  (Buddy allocator)
Deadlock [deadlock] - 6 sample(s)
  Course Examples
    - Safe state (Banker's sample)  (Banker's safety algorithm, samples/banker_sample.txt)
    - Deadlock detection (P1-P4)  (Detection algorithm, samples/deadlock_sample.txt)
  Basic Examples
    - Resource request: P1 asks 1 0 2  (Banker's request algorithm, samples/banker_sample.txt)
    - Resource Allocation Graph with a cycle  (Resource Allocation Graph, samples/deadlock_rag_cycle.txt)
  Special Cases
    - Unsafe state  (Banker's safety algorithm, samples/banker_unsafe.txt)
  Advanced Scenarios
    - Deadlock recovery by termination  (Recovery by termination, samples/deadlock_sample.txt)
Synchronization [synchronization] - 27 sample(s)
  Race condition (no synchronization)
    - Lost update (textbook interleaving)  (Race condition (no synchronization))
    - Serial order (no overlap)  (Race condition (no synchronization))
    - 3 processes, round robin, 1 instruction per turn  (Race condition (no synchronization))
    - Random interleaving (seed 7)  (Race condition (no synchronization))
  Peterson's solution
    - Both want to enter at the same time  (Peterson's solution)
    - P0 alone (progress)  (Peterson's solution)
    - Round robin, 3 rounds each (bounded waiting)  (Peterson's solution)
    - Random interleaving (seed 11)  (Peterson's solution)
  Mutex lock
    - 4 processes contend for the lock  (Mutex lock)
    - Long turns (no contention)  (Mutex lock)
    - Random interleaving (seed 5)  (Mutex lock)
  Binary semaphore
    - 3 processes, S = 1  (Binary semaphore)
    - Random interleaving (seed 3)  (Binary semaphore)
  Counting semaphore
    - 5 processes share 2 printers  (Counting semaphore)
    - 6 processes, 3 instances, random (seed 9)  (Counting semaphore)
  Producer-consumer (semaphores)
    - Fast producer: buffer fills up  (Producer-consumer (semaphores))
    - Fast consumer: buffer is empty  (Producer-consumer (semaphores))
    - 2 producers, 2 consumers, N = 2, random (seed 3)  (Producer-consumer (semaphores))
  Monitor + condition variables
    - Fast producer: buffer fills up  (Monitor + condition variables)
    - Fast consumer: buffer is empty  (Monitor + condition variables)
    - 2 producers, 2 consumers, N = 2, random (seed 3)  (Monitor + condition variables)
  Readers-writers (semaphores)
    - Readers share, the writer waits  (Readers-writers (semaphores))
    - Readers' preference: the writer can starve  (Readers-writers (semaphores))
    - 3 readers, 2 writers, random order (seed 5)  (Readers-writers (semaphores))
  Dining philosophers (semaphores)
    - Naive: every philosopher takes the left chopstick -> DEADLOCK  (Dining philosophers (semaphores))
    - Asymmetric: odd philosophers take the right chopstick first  (Dining philosophers (semaphores))
    - Asymmetric, random order (seed 4)  (Dining philosophers (semaphores))
Threads [threads] - 6 sample(s)
  Threading models
    - Many-to-One, 3 threads x 4 ticks, 3 cores  (Many-to-One)
    - One-to-One, 3 threads x 4 ticks, 3 cores  (One-to-One)
    - Many-to-Many, 2 contexts, 2 cores, quantum 1  (Many-to-Many)
  Blocking and joining
    - Blocking call, Many-to-One  (Many-to-One)
    - Blocking call, One-to-One  (One-to-One)
    - Create and join (main joins B, B joins A)  (One-to-One)
IPC [ipc] - 8 sample(s)
  Shared memory
    - Shared memory: server writes HELLO, client reads it  (Shared memory)
    - Shared memory errors: bad offset, read-only, removal  (Shared memory)
  Message passing
    - Mailbox, capacity 2, two messages (FIFO)  (Message passing (indirect, mailbox))
    - Receiver blocks on an empty mailbox  (Message passing (indirect, mailbox))
    - Direct communication, capacity 1: sender blocks  (Message passing (direct))
  Pipe
    - Anonymous pipe: short string, then EOF  (Pipe (anonymous))
    - Pipe full: the writer blocks (modelled capacity 8)  (Pipe (anonymous))
    - Empty pipe with an open writer: read blocks, it is not EOF  (Pipe (anonymous))
I/O [io] - 7 sample(s)
  Basic Examples
    - Basic (4 units, device every 3 ticks)  (Polling / Interrupt / DMA)
  Special Cases
    - Small transfer (1 unit)  (Polling / Interrupt / DMA)
    - Slow device (ready every 10 ticks)  (Polling / Interrupt / DMA)
  Advanced Scenarios
    - Bulk transfer (32 units, fast device)  (Polling / Interrupt / DMA)
    - DMA with cycle stealing  (Polling / Interrupt / DMA)
  Course Examples
    - Hand-traced example (2 units)  (Polling / Interrupt / DMA)
  Algorithm-Specific Examples
    - Algorithm comparison (basic workload)  (Compare all three)
Disk [disk] - 16 sample(s)
  Course Examples
    - Textbook reference (head 53, higher)  (any algorithm)
    - Reference queue (file)  (any algorithm, samples/disk_reference.txt)
  Basic Examples
    - Same queue, direction lower  (any algorithm)
  Special Cases
    - Edge: head request, duplicates, both ends  (any algorithm)
    - Edge: all requests behind the head  (any algorithm)
    - Edge: SSTF tie (40 and 60 from 50)  (any algorithm)
    - Edge: single request  (any algorithm)
    - Edge: empty queue  (any algorithm)
    - Edge cases (file)  (any algorithm, samples/disk_edge_cases.txt)
  Algorithm-Specific Examples
    - Textbook queue with FCFS  (FCFS)
    - Textbook queue with SSTF  (SSTF)
    - Textbook queue with SCAN  (SCAN)
    - Textbook queue with C-SCAN  (C-SCAN)
    - Textbook queue with LOOK  (LOOK)
    - Textbook queue with C-LOOK  (C-LOOK)
  Advanced Scenarios
    - Algorithm comparison (all six)  (Compare all six)
Storage & Files > File Explorer [storage.explorer] - 5 sample(s)
  Basic Examples
    - Sample directory hierarchy  (Virtual filesystem)
    - File creation and deletion  (Virtual filesystem)
  Algorithm-Specific Examples
    - Hard links  (Hard link)
    - Symbolic links  (Symbolic link)
  Advanced Scenarios
    - File descriptors  (File descriptors)
Storage & Files > Access Control [storage.access] - 10 sample(s)
  Basic Examples
    - Owner, group and others  (rwx permission bits)
    - Read, write and execute bits  (rwx permission bits)
  Special Cases
    - Permission denied  (Access check)
  Permission presets
    - Private file (600)  (chmod)
    - Shared-group file (660, group staff)  (chmod)
    - Public readable file (644)  (chmod)
    - Private directory (700)  (chmod)
    - Group directory (770, group staff)  (chmod)
    - Traverse-only directory (711)  (chmod)
    - List-only directory (744)  (chmod)
Storage & Files > Block Allocation [storage.blocks] - 7 sample(s)
  Basic Examples
    - Contiguous allocation (sample disk)  (Contiguous)
    - Linked allocation (sample disk)  (Linked)
    - Indexed allocation (sample disk)  (Indexed)
  Fragmentation and limits
    - Contiguous fails despite enough free space  (Contiguous / Linked / Indexed)
    - Largest file (index limit)  (Contiguous / Linked / Indexed)
  Advanced Scenarios
    - Growing files (extend in place or relocate)  (Contiguous / Linked / Indexed)
    - Many small files (metadata overhead)  (Contiguous / Linked / Indexed)
Storage & Files > Recovery Lab [storage.recovery] - 5 sample(s)
  Basic Examples
    - Intact deleted file  (Metadata recovery)
    - Partially overwritten  (Metadata recovery)
    - Fully overwritten  (Metadata recovery)
  Advanced Scenarios
    - Metadata lost: signature carving  (Signature carving)
  Special Cases
    - Hard link and open descriptor  (Metadata recovery)
```
