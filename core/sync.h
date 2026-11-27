// sync.h - process synchronization simulator (pure C++17, deterministic, no threads).
//
// OS concepts: race condition, critical section, Peterson's solution, mutex locks,
// binary and counting semaphores, the bounded-buffer (producer-consumer) problem,
// monitors with condition variables, readers-writers (first problem, readers' preference) and the
// dining philosophers (naive = deadlock possible, asymmetric = deadlock-free) - Phase 3 gap fill.
//
// How it works: every simulated process runs a small program of pseudocode lines.
// One call to SyncSim::step(pid) executes exactly ONE line of that process
// (e.g. "reg = counter;"), so any interleaving of instructions can be reproduced
// exactly - either chosen by the user (manual stepping), by a fixed script
// (sample scenarios) or by a deterministic scheduler (round robin / seeded random).
// Shared memory is modelled by plain fields of SyncSim; because only one line runs
// at a time there is no real data race, the race condition is shown through the
// interleaving itself (lost updates), never through undefined behaviour.
#pragma once
#include <deque>
#include <random>
#include <string>
#include <utility>
#include <vector>

enum class SyncAlgo { RACE, PETERSON, MUTEX, BINARY_SEM, COUNTING_SEM, PRODUCER_CONSUMER, MONITOR, READERS_WRITERS, DINING_PHILOSOPHERS };
const char* syncAlgoName(SyncAlgo);

// Process states shown in the GUI. Running = executed the last step.
// Spinning = busy-waiting (still uses the CPU), Blocked = asleep in a waiting queue.
enum class SyncProcState { Ready, Running, Spinning, Blocked, Done };
const char* syncStateName(SyncProcState);

// One pseudocode instruction.
enum class Op {
    Nop,              // local statement ("int other = 1 - process;", "item = produce_item();")
    Read,             // reg = counter;
    Inc,              // reg = reg + 1;
    Write,            // counter = reg;
    SetInterested,    // interested[process] = arg (1 = true, 0 = false)
    SetTurn,          // turn = process;
    PetersonWait,     // while (interested[other] == true && turn == process);   (busy wait)
    Lock, Unlock,     // mutex acquire / release (obj = mutex)
    SemWait, SemSignal,           // semaphore wait (P) / signal (V) (obj = semaphore)
    UseResource, ReleaseResource, // counting semaphore: take / give back one resource instance
    Insert, Remove,               // bounded buffer: buffer[in] = item / item = buffer[out]
    MonEnter, MonExit,            // monitor entry (lock) / exit (unlock)
    CondWhile,        // while (<condition>) - arg 0: buffer full, arg 1: buffer empty
    CondWait,         // cond.wait(): release the monitor and sleep (obj = condition)
    CondSignal,       // cond.signal(): move one waiter to the monitor entry queue (Mesa)
    Produce,          // item = produce_item();
    Consume,          // consume_item(item);
    RcInc, RcDec,     // readers-writers: read_count++ / read_count--
    RcFirstWait,      // if (read_count == 1) wait(rw_mutex);   (obj = rw_mutex)
    RcLastSignal,     // if (read_count == 0) signal(rw_mutex); (obj = rw_mutex)
    DoneRead,         // reader leaves the shared data (one read completed)
    WriteData,        // writer modifies the shared data (one write completed)
    Eat               // philosopher eats (one meal)
};

struct Instr {
    Op op = Op::Nop;
    int obj = -1;             // sync object used (index into objects()), -1 = none
    int arg = 0;              // extra argument (see Op)
    std::string code;         // pseudocode text shown in the GUI
    bool enterCS = false;     // the process is inside the critical section after this line completes
    bool leaveCS = false;     // the process leaves the critical section with this line
};

enum class ObjKind { Mutex, Semaphore, Condition, Monitor };
const char* objKindName(ObjKind);

// A synchronization object with its FIFO waiting queue (process indices).
struct SyncObject {
    std::string name;
    ObjKind kind = ObjKind::Mutex;
    int value = 0;            // semaphore value
    int owner = -1;           // mutex / monitor owner (process index), -1 = free
    std::deque<int> queue;    // waiting processes, first = next to wake
};

struct SyncProc {
    int id = 0;               // index (P0, P1, ... in the GUI)
    std::string name;         // "P0", "Producer 0", "Consumer 1"
    int program = 0;          // which program it runs (index into programs())
    int pc = 0;               // next line to execute
    int lastLine = -1;        // line executed in its last step
    int iterations = 1;       // how many times it runs its program
    int itersDone = 0;
    int reg = 0;              // private register (read-modify-write)
    int item = -1;            // item produced / consumed (bounded buffer)
    SyncProcState state = SyncProcState::Ready;
    bool inCS = false;
    int waitingOn = -1;       // object index while Blocked
    int resumePc = -1;        // monitor: line to continue at after a condition wait
    int steps = 0, busyWaitSteps = 0, blockedSteps = 0, csEntries = 0;
};

struct SyncConfig {
    SyncAlgo algo = SyncAlgo::RACE;
    int processes = 2;        // RACE, MUTEX, BINARY_SEM, COUNTING_SEM (PETERSON must be 2)
    int iterations = 1;       // loop count per process (items per producer for the buffer problems)
    int resources = 2;        // counting semaphore: number of resource instances
    int producers = 1, consumers = 1, capacity = 3;
    int producerSpeed = 1, consumerSpeed = 1;   // instructions per turn (round robin)
    int slice = 1;            // instructions per turn for the other algorithms
    bool randomOrder = false; // false: round robin, true: seeded random choice
    unsigned seed = 1;
    std::vector<int> script;  // forced process order for the first steps (sample scenarios)
    int readers = 3, writers = 1;      // READERS_WRITERS
    int philosophers = 5;              // DINING_PHILOSOPHERS
    bool asymmetric = false;           // philosophers: odd ones take the RIGHT chopstick first (no deadlock)
};
// Throws std::invalid_argument with a clear message on a bad configuration.
void validateSyncConfig(const SyncConfig&);

// Timeline cell of one process in one step.
enum class SyncCell : char { Idle = '.', Run = 'r', RunCS = 'C', Spin = 's', Blocked = 'b', Done = ' ' };

struct SyncStepRecord {
    int step = 0;             // 1, 2, 3 ...
    int pid = -1;             // process that executed
    int program = 0, line = 0;
    std::string text;         // log line
    std::vector<SyncCell> cells;   // one per process
};

struct SyncStats {
    int steps = 0;
    int expected = 0;         // final value that a correct run must reach
    int actual = 0;           // value reached so far
    int lostUpdates = 0;      // increments written but overwritten (counter algorithms)
    int csViolations = 0;     // entries while the critical section was already full
    int maxInCS = 0;          // largest number of processes inside at the same time
    int busyWaitSteps = 0, blockedSteps = 0, csEntries = 0;
    int overflows = 0, underflows = 0, produced = 0, consumed = 0;
    bool completed = false;   // every process finished its program
    bool stuck = false;       // nobody can run although not all finished
    int waitingSteps() const { return busyWaitSteps + blockedSteps; }
};

class SyncSim {
public:
    explicit SyncSim(const SyncConfig& cfg);   // validates and builds programs/objects

    bool canRun(int pid) const;                // not Blocked and not Done
    std::vector<int> runnable() const;
    int pickNext();                            // next pid by script / policy, -1 = none
    bool step(int pid);                        // executes one line of pid; false if it cannot run
    bool stepAuto();                           // pickNext() + step()
    int runToEnd(int maxSteps = 20000);        // returns the number of steps executed
    bool finished() const;                     // all done, or nobody can run

    const SyncConfig& config() const { return cfg_; }
    const std::vector<SyncProc>& procs() const { return procs_; }
    const std::vector<SyncObject>& objects() const { return objs_; }
    const std::vector<std::vector<Instr>>& programs() const { return programs_; }
    const std::vector<std::string>& programTitles() const { return titles_; }
    std::vector<std::pair<std::string, std::string>> variables() const;   // live shared-variable view
    const std::vector<int>& buffer() const { return buffer_; }            // -1 = empty slot
    int bufferIn() const { return in_; }
    int bufferOut() const { return out_; }
    int bufferCount() const { return count_; }
    int counter() const { return counter_; }
    bool interested(int i) const { return interested_[i & 1]; }
    int turn() const { return turn_; }
    int inCSCount() const;
    int csCapacity() const;                    // allowed inside at once (readers share, philosophers N/2)
    const std::vector<SyncStepRecord>& history() const { return history_; }
    const std::vector<std::string>& log() const { return log_; }
    SyncStats stats() const;
    std::string stateKey() const;              // compact state (used by the exhaustive Peterson test)

private:
    void enterCS(int p);
    void leaveCS(int p);
    void block(int p, int obj);
    void wake(int p);                          // finishes the blocking line of p
    void grant(int obj, int p);                // gives a mutex / monitor to p
    void release(int obj, int p);              // frees a mutex / monitor (hand-off to the first waiter)
    void finishLine(int p);                    // pc++ and end-of-program handling
    int addObj(const std::string& name, ObjKind kind, int value);

    SyncConfig cfg_;
    std::vector<SyncProc> procs_;
    std::vector<SyncObject> objs_;
    std::vector<std::vector<Instr>> programs_;
    std::vector<std::string> titles_;
    int monitor_ = -1;                         // monitor object index

    // shared memory
    int counter_ = 0, writes_ = 0;
    bool interested_[2] = {false, false};
    int turn_ = 0;
    std::vector<int> buffer_;
    int in_ = 0, out_ = 0, count_ = 0, nextItem_ = 0, inUse_ = 0, uses_ = 0;
    std::vector<int> producedItems_, consumedItems_;
    int readCount_ = 0, reads_ = 0, rwWrites_ = 0, meals_ = 0;   // readers-writers / philosophers

    // statistics and history
    int steps_ = 0, csViolations_ = 0, maxInCS_ = 0, overflows_ = 0, underflows_ = 0;
    std::vector<SyncStepRecord> history_;
    std::vector<std::string> log_;

    // scheduler
    size_t scriptPos_ = 0;
    int current_ = -1, usedSlice_ = 0;
    std::mt19937 rng_;
};

// A ready-made example ("sample scenario") for one algorithm.
struct SyncScenario { std::string name; std::string description; SyncConfig cfg; };
std::vector<SyncScenario> syncScenarios(SyncAlgo);

// Same two-process counter workload under four mechanisms.
struct SyncComparisonRow { SyncAlgo algo; std::string label; SyncStats stats; std::string guarantee; };
std::vector<SyncComparisonRow> compareSync(int iterations, int slice, bool randomOrder, unsigned seed);

// Plain-text explanation of an algorithm (shown in the GUI and the report).
std::string syncExplanation(SyncAlgo);
// One-line theoretical guarantee (mutual exclusion / progress / bounded waiting ...).
std::string syncGuarantee(SyncAlgo);
