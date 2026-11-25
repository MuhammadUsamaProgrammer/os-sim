# OS-Sim viva notes

Short, plain-language notes: what each OS concept is and **exactly where our code uses it**.

---

## Part A: Concepts and where they are in the code

### 1. Process and PCB
A process is a program in execution. The OS keeps a **Process Control Block (PCB)** for each
one. In our code it is `struct PCB` in `core/pcb.h` (pid, arrival, burst, remaining, priority,
pages, state, firstRun, finish, maxRes/allocRes). The states are `enum class ProcState { New, Ready,
Running, Waiting, Terminated }`, and `stateName()` prints them in the logs.
- New → Ready: `makeReady()` in `core/scheduler.cpp` when the arrival time is reached (the engine
  logs `P2 READY arrived`).
- Ready → Running: `schedulerPick()` dispatches it.
- Running → Ready: preemption (RR quantum expiry, or a better SRTF/priority job). The engine logs `READY preempted`.
- Running → Waiting: in `core/integrated.cpp`, when a resource request is denied (`P2 WAITING for 0 1 0`).
- Waiting → Ready: when the retried request is granted (`READY resources granted`).
- Running → Terminated: remaining reaches 0 (`TERMINATED finish=30`), or the process is killed by deadlock recovery.

### 2. fork / exec / wait
`realProcessDemo()` in `engine/engine.cpp` calls `fork()` n times. Each child replaces its image
with `execlp("sleep","sleep","1",nullptr)`, and the parent reaps each child with `waitpid()` and
logs its exit code. Run it with `./build/apps/ossim_console engine samples/processes_fcfs.csv fcfs 4 --real`.

### 3. Threads
`SimulationEngine::start()` in `engine/engine.cpp` calls `pthread_create` once per process
(`procMain` → `procLoop`) and once for the clock (`clockMain` → `clockLoop`). `join()` calls
`pthread_join` on all of them. All threads share the PCB vector, which is why we need a mutex.

### 4. Mutex
`pthread_mutex_t mtx_` in `SimulationEngine` protects the PCBs, the ready queue, `currentTick_`
and the paused/stop flags. Every access is between `pthread_mutex_lock(&mtx_)` and
`pthread_mutex_unlock(&mtx_)` (clock loop, process loop, pause/resume/step/stop, snapshot).
`PipeLogger` has its own mutex for the collected log lines.

### 5. Semaphores
`sem_t runSem_[N]` (one per process) and `sem_t doneSem_`, both initialised to 0 with `sem_init`.
For every tick the clock does `sem_post(&runSem_[i])`. Process i was blocked in `sem_wait(&runSem_[i])`;
it wakes, runs exactly one tick and calls `sem_post(&doneSem_)`, while the clock waits in
`sem_wait(&doneSem_)`. This handshake means only one thread works at a time, so the log order is
always the same. `sem_destroy` runs in the destructor.

### 6. Condition variables
`pthread_cond_t cond_` is used for **pause/resume/step**. The clock thread loops
`while (paused_ && stepBudget_ == 0 && !stopRequested_) pthread_cond_wait(&cond_, &mtx_);`.
`resume()` and `step()` change the flag and call `pthread_cond_broadcast`. The delay between ticks
uses `pthread_cond_timedwait`, so Stop or Step wake it immediately instead of waiting out a sleep.

### 7. IPC with a pipe
`PipeLogger` in `engine/logger.cpp` creates a `pipe()`. Threads call `logWrite()`, which does one
`write()` per line. A line is shorter than `PIPE_BUF`, so the write is atomic and lines never mix.
The read end is set to `O_NONBLOCK` with `fcntl`. The clock thread (the single dispatcher) calls
`drainLog()` after every tick, splits the bytes into lines and passes them to the observer. In the
GUI, `EngineBridge` turns them into Qt signals.

### 8. CPU scheduling
`core/scheduler.cpp`. One tick-based simulator, `runScheduler()`, handles all six algorithms. Each
tick it admits arrivals, asks `schedulerPick()` who runs, runs one tick and appends it to the Gantt
timeline. `better()` holds the selection key: arrival for FCFS, remaining time for SJF/SRTF, the
priority number for Priority. Ties go to earlier arrival, then lower pid.
- Non-preemptive (FCFS, SJF, Priority NP): the running process keeps the CPU until it finishes.
- Preemptive (SRTF, Priority P): every tick the running process competes again.
- RR: after `quantum` ticks the process goes to the back of the queue, behind processes that arrive at that tick.

### 9. Paging and page tables
`FrameAllocator::allocateEqual()` (`core/memory.cpp`) divides the physical frames equally between
processes. `buildPageTable()` gives each page a frame number and a valid bit: valid means the page
is in memory, invalid means it is on disk and touching it causes a page fault. The Memory tab's
"Page table" panel shows this.

### 10. Page replacement
`FramePool` in `core/memory.cpp`: `access()` returns hit or fault. On a fault with no empty frame,
`chooseVictim()` picks:
- **FIFO**: the page loaded earliest (`loaded_`);
- **LRU**: the page whose last use is oldest (`lastUse_`);
- **Optimal**: the page whose next use is furthest away, or never.

Ties go to the earliest-loaded page.

### 11. Deadlock avoidance: Banker's algorithm
`bankersSafety()` in `core/deadlock.cpp` computes Need = Max − Allocation and Work = Available,
then repeatedly finds a process with Need ≤ Work, "runs" it and adds its Allocation to Work. If
every process can finish, the state is safe and the order is the safe sequence (`P1, P3, P4, P0, P2`).
`bankersRequest()` checks request ≤ Need and request ≤ Available, pretends to allocate, runs the
safety check and rolls back if the state would be unsafe.

### 12. Deadlock detection
`detectDeadlock()` is the same idea, but it uses the **current Request** matrix instead of Need.
Whoever cannot finish is deadlocked. `buildRag()` builds the resource-allocation graph and finds a
cycle with DFS (`RagView` draws it).

### 13. Deadlock recovery
`recoverByTermination()` kills the deadlocked process holding the fewest resources (tie → lowest
pid), gives its resources back to Available, and runs detection again until no deadlock remains.

### 14. Integrated run
`runIntegrated()` in `core/integrated.cpp` reuses `schedulerPick()`, `FramePool`, `bankersRequest()`,
`detectDeadlock()` and `recoverByTermination()` in one tick loop. With Banker's ON our sample never
deadlocks. With it OFF, P1 holds A and wants B while P2 holds B and wants A. The deadlock is
detected at t=9 and P1 is killed.

### 15. Why `core/` has no threads, and why the GUI never touches threads
`core/` is pure, deterministic C++: the same input always gives the same numbers, so we can test it
against the textbook values. The engine only *replays* the result with real threads to demonstrate
the primitives. Qt widgets may only be used from the GUI thread, so the engine's callback just emits
a signal. Qt delivers it through a **queued connection** to the GUI thread (`gui/EngineBridge.cpp`).

---

## Part B: 25 likely viva questions

1. **What is the difference between a process and a thread?**
   A process has its own address space and resources. Threads are units of execution inside one
   process that share its memory. Our simulated processes are threads, so they share the PCB
   vector, which is why a mutex is needed.

2. **Why did you use `pthread_join`?**
   To wait until every thread has really finished before we destroy the semaphores, the mutex and
   the condition variable. Destroying them while a thread still uses them is undefined behaviour,
   and not joining would leak thread resources.

3. **Semaphore vs mutex?**
   A mutex is a lock with an owner: the thread that locks it must unlock it, and it gives mutual
   exclusion. A semaphore is a counter that any thread can post or wait on, so it is used for
   *signalling and ordering*. We use the mutex to protect shared data and the semaphores for the
   clock → process → clock handshake.

4. **Why do you need a condition variable if you already have semaphores?**
   To wait for a *condition* (not paused, or a step is allowed, or a stop is requested) while
   holding the mutex. `pthread_cond_wait` releases the mutex and sleeps atomically, so no wake-up
   is lost. Pause/resume is a condition, not a counter.

5. **Why is `pthread_cond_wait` inside a `while` loop and not an `if`?**
   Because of spurious wake-ups, and because the condition may have changed again before the thread
   runs. The loop re-checks the condition.

6. **How do you make the engine deterministic if threads run concurrently?**
   The handshake `sem_post(runSem[i])` → process runs one tick → `sem_post(doneSem)` → clock continues
   lets only one thread do work at any moment, and the order comes from the timeline computed by
   `core/`. We ran it 20 times and the logs were byte-identical.

7. **What is a race condition, and how did you check for them?**
   Two threads access shared data without synchronisation and at least one of them writes, so the
   result depends on timing. We protect all shared state with the mutex and built the tests with
   ThreadSanitizer (`-DSANITIZE=thread`), which reported nothing.

8. **How does the pipe IPC work? Why are the lines never mixed?**
   `pipe()` gives a read fd and a write fd. Each thread writes a whole line with a single `write()`.
   POSIX guarantees writes of at most `PIPE_BUF` bytes are atomic. The dispatcher reads with
   `O_NONBLOCK`, so it never blocks when the pipe is empty.

9. **What do `fork`, `exec` and `wait` do in your demo?**
   `fork()` duplicates the process. In the child, `execlp("sleep",...)` replaces the program image.
   The parent calls `waitpid()` to collect each child's exit status, which prevents zombies.

10. **Preemptive vs non-preemptive scheduling?**
    Non-preemptive: a process keeps the CPU until it finishes or blocks (FCFS, SJF, Priority NP).
    Preemptive: the OS can take the CPU away (SRTF when a shorter job arrives, Priority P, RR when
    the quantum expires).

11. **Why is SJF/SRTF optimal for average waiting time, and why isn't it used directly?**
    Running short jobs first minimises the total waiting time (SRTF gave 6.5 versus 8.75 for FCFS).
    But the burst length is not known in advance; real systems predict it, for example with an
    exponential average.

12. **What is the convoy effect?**
    In FCFS, short processes wait behind a long one: P1 = 24 makes P2 and P3 wait, so the average WT is 17.

13. **How does the time quantum affect Round Robin?**
    A very large quantum makes RR behave like FCFS. A very small one causes many context switches
    (overhead). RR gives good response time; in our comparison RR had the most context switches (7).

14. **What is starvation, and which of your algorithms can starve?**
    A ready process waits indefinitely. Priority scheduling (low-priority processes) and SJF/SRTF
    (long jobs) can starve. The fix is **aging**: increase the priority of a process the longer it waits.

15. **Define waiting, turnaround and response time.**
    Turnaround = completion − arrival. Waiting = turnaround − burst. Response = first time on the
    CPU − arrival. See `runScheduler()`.

16. **What is a page fault?**
    A reference to a page that is not in a frame (valid bit = 0). The OS must load it from disk,
    evicting a victim if memory is full. In the Memory tab these cells are red.

17. **What is Belady's anomaly?**
    With FIFO, adding frames can *increase* the number of faults: 1,2,3,4,1,2,5,1,2,3,4,5 gives 9
    faults with 3 frames and 10 with 4. LRU and Optimal are stack algorithms and never show it.

18. **Why can't Optimal be implemented in a real OS?**
    It needs to know the future reference string. It is used as a benchmark (a lower bound): 9
    faults versus LRU 12 and FIFO 15.

19. **How is LRU implemented in your code, and how do real systems approximate it?**
    We store a logical "last used" time for each frame and evict the smallest. Real hardware uses
    reference bits and approximations such as the clock / second-chance algorithm.

20. **What are the four necessary conditions for deadlock?**
    Mutual exclusion, hold and wait, no preemption, and circular wait. All four must hold at once.

21. **Safe state vs unsafe state vs deadlock?**
    Safe: there is an order in which all processes can finish. Unsafe: no such order is guaranteed,
    so a deadlock *may* happen. Deadlock: processes are actually blocked forever. Every deadlocked
    state is unsafe, but not every unsafe state is a deadlock.

22. **Avoidance vs detection: what is the trade-off?**
    Avoidance (Banker's) checks every request and refuses unsafe ones. It needs the maximum claims
    in advance and makes processes wait: P2 waited from t=3 to t=19 in our integrated run. Detection
    lets requests through and periodically looks for deadlocks, then has to recover, which costs
    killed work (P1 was killed).

23. **Does a cycle in the resource-allocation graph always mean deadlock?**
    Only for single-instance resources. With several instances a cycle is necessary but not
    sufficient, so our detection algorithm, not the cycle, decides who is deadlocked. After recovery
    a cycle P2 → C → P2 is still drawn, but there is no deadlock.

24. **How do you choose the victim during recovery? What are the alternatives?**
    The deadlocked process with the lowest total allocation (cheapest to kill), tie → lowest pid,
    and we repeat detection after each kill. Alternatives: kill all deadlocked processes, preempt
    resources and roll back, or use priority or the amount of work done as the cost.

25. **Why did you keep algorithms out of the GUI and out of the threads?**
    Separation of concerns. `core/` is pure and unit-tested against textbook numbers, the engine
    only demonstrates synchronisation, and the GUI only displays. Worker threads never touch Qt
    widgets (that is not allowed); `EngineBridge` sends events with queued signals.
