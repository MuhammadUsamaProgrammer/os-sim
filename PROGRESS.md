# OS-Sim progress

Read this first after a session restart; continue from the first unfinished stage.

| Stage | Title | Status |
|---|---|---|
| 1 | Skeleton | DONE |
| 2 | Scheduling core | DONE |
| 3 | Memory core | DONE |
| 4 | Deadlock core | DONE |
| 5 | Thread engine | DONE |
| 6 | GUI: Scheduling tab | todo |
| 7 | GUI: Memory tab | todo |
| 8 | GUI: Deadlock tab + RAG | todo |
| 9 | Integration + export | todo |
| 10 | Polish, docs, final verification | todo |

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

## Not done
(nothing dropped yet)
