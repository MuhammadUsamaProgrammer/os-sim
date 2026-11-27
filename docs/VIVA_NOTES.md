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

### 16. Process synchronization tab (race, Peterson, mutex, semaphores, producer-consumer, monitor)
- **Step model** (`core/sync.cpp`, `SyncSim::step()`): every process runs a program of pseudocode lines,
  and one step executes exactly one line of one process. You can pick the process yourself (the
  **Step** button on its card, `SyncTab::stepProcess()`), so you can build any interleaving.
- **Race condition**: `counter++` is three lines: `reg = counter` (READ), `reg = reg + 1` (MODIFY),
  `counter = reg` (WRITE). P0 READ, P1 READ, P0 MODIFY, P0 WRITE, P1 MODIFY, P1 WRITE gives counter 1
  instead of 2: one **lost update**. Two processes between READ and WRITE at once = a critical-section
  violation (`enterCS()` counts it). We never use an unsynchronized C++ variable for this: the race is
  in the interleaving. The real demo (`raceBody()` in `engine/sync_demo.cpp`) uses an atomic load and a
  separate atomic store, so it is well-defined C++ and still loses updates.
- **Peterson** (exact lecture variant, 2 processes): `other = 1 - process; interested[process] = true;
  turn = process; while (interested[other] && turn == process);` ... `interested[process] = false;`.
  Whoever writes `turn` last waits. The busy-wait lane and the orange cards show spinning. The real
  version (`petersonBody()`) needs `std::atomic` with sequentially consistent order, because a
  normal compiler/CPU may reorder the stores and loads.
- **Mutex** (`Op::Lock/Unlock`): owner + waiting queue; the waiter sleeps (blocked, no CPU). The
  simulation hands the lock to the first waiter (FIFO); a real `pthread_mutex` gives no such promise.
- **Semaphores** (`Op::SemWait/SemSignal`): `wait` decrements or sleeps, `signal` wakes one sleeper or
  increments. Binary S = 1 works like a lock; counting S = k lets k processes use k instances.
  Real: `sem_init`, `sem_wait`, `sem_post`, `sem_destroy` (`semBody()`).
- **Producer-consumer**: `empty = N`, `full = 0`, `mutex = 1`. The producer blocks on `wait(empty)`
  when no slot is free; the consumer blocks on `wait(full)` when there is no item. The buffer can never
  overflow or underflow (checked in every test).
- **Monitor**: the buffer lives inside the monitor; `insert` / `remove` hold the monitor lock. If the
  condition is false the process calls `notFull.wait()` / `notEmpty.wait()`, which releases the lock and
  sleeps. `signal` moves one sleeper to the entry queue (Mesa / pthreads), so it must re-check with
  `while`. Real: `pthread_mutex` + `pthread_cond_wait/signal` (`monitorInsert()`, `monitorRemove()`).
- **GUI rule**: the animation is a `QTimer` that calls `stepAuto()` once per tick (no blocking).
  The real demo runs on a `std::thread` (`SyncBridge`), its lines arrive through queued signals,
  and **Stop** calls `cancel()`, which posts the semaphores / broadcasts the condition variables so
  every blocked thread wakes up and can be joined.

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

---

## Part C: Process synchronization questions

26. **What is a race condition? Show one with the simulator.**
    The result depends on the order in which processes run. Race condition > *Lost update*: both
    processes read 0 before either writes, so the counter ends at 1 instead of 2.

27. **What is a critical section, and what three properties must a solution have?**
    The code that touches shared data. A solution needs mutual exclusion (one process inside), progress
    (if nobody is inside, a process that wants to enter is not blocked forever by processes that are
    not interested) and bounded waiting (a limit on how often others enter before a waiting process).

28. **Why does Peterson's solution guarantee mutual exclusion?**
    Both inside would need each to see `turn` set to the other process, but `turn` has one value. The
    process that wrote `turn` last waits. `test_sync` checks this in all 1211 reachable states.

29. **Why does Peterson need `std::atomic` / sequential consistency on real hardware?**
    Compilers and CPUs may reorder an ordinary store (`interested[i] = true`) after a later load
    (`interested[other]`). Then both processes can see the other as not interested and both enter.
    seq_cst atomics forbid that reordering.

30. **Busy waiting vs blocking?**
    Peterson spins in its while loop and uses the CPU (busy-wait steps). A mutex or a semaphore puts
    the waiter to sleep in a queue (blocked steps) and wakes it later: no CPU is wasted, but sleeping
    and waking cost a context switch. Spinning is fine only for very short waits on multiple CPUs.

31. **Is the FIFO order you show guaranteed by POSIX?**
    No. The simulation uses FIFO so that it is deterministic. POSIX mutexes and semaphores give mutual
    exclusion but do not specify which waiter wakes first, so bounded waiting is not guaranteed.

32. **Binary semaphore vs counting semaphore?**
    A binary semaphore has values 0 and 1 (mutual exclusion or signalling). A counting semaphore starts
    at the number of identical resources; with S = 2, at most 2 processes use a printer at once.

33. **In producer-consumer, why `wait(empty)` before `wait(mutex)`?**
    If a producer took the mutex first and then slept on `empty` (buffer full), no consumer could get
    the mutex to remove an item: a deadlock.

34. **Why `while` and not `if` around `cond.wait()`?**
    With Mesa / pthread semantics the signaller keeps the lock, and the woken process re-enters later.
    By then another process may have changed the state (and spurious wake-ups are allowed), so the
    condition must be checked again.

35. **What does a monitor add compared with semaphores?**
    Encapsulation: the shared data can only be reached through the monitor's procedures, which always
    run with the lock held, so a forgotten `signal(mutex)` cannot happen. Condition variables handle
    waiting for a state.

36. **One run of the unsynchronized program gave the right answer. Is it correct?**
    No. A test run can only show the presence of a bug. The Comparison page lists the theoretical
    guarantee separately from what one run observed.

37. **How do you stop a real demo whose threads are blocked?**
    `SyncDemoRunner::cancel()` sets a flag and posts the semaphores / broadcasts the condition
    variables. A woken thread sees the flag, passes the wake-up on and exits, and `run()` joins them all.
