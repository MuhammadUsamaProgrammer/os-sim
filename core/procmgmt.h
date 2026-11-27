// procmgmt.h - process lifecycle simulator (M2): 5-state model, PCB, process tree.
// OS concepts: New -> Ready (admit), Ready -> Running (dispatch), Running -> Ready (preempt /
// quantum expiry), Running -> Waiting (blocking I/O), Waiting -> Ready (I/O completion),
// Running -> Terminated (exit). A separately labelled "kill" event terminates a process that
// is not running. One logical CPU: at most one process is Running.
//
// The automatic mode REUSES the CPU scheduler's dispatcher decision (schedulerPick() from
// core/scheduler.h) on the current CPU bursts, so FCFS / SJF / SRTF / RR / Priority behave
// exactly like in the Scheduling tab; this file only adds the I/O and lifecycle events.
// Event order inside one tick (documented): arrivals (create + admit) -> I/O completions ->
// dispatcher decision -> one tick of execution -> end-of-burst handling (block on I/O or exit).
#pragma once
#include <string>
#include <vector>
#include "pcb.h"
#include "scheduler.h"

// One process of the workload: CPU and I/O bursts alternate, starting and ending with CPU.
struct LcProcSpec {
    int pid = 0, ppid = 1;            // ppid 1 = the modelled init / shell (not a real Linux PID)
    std::string name;
    int arrival = 0, priority = 0;
    std::vector<int> bursts;          // CPU, IO, CPU, IO, ..., CPU  (odd length, all > 0)
    int pages = 0;                    // memory footprint in pages (modelled)
};

enum class LcEvent { Create, Admit, Dispatch, Preempt, BlockIO, IOComplete, Exit, Kill, Execute, Idle };
const char* lcEventName(LcEvent);

// Live PCB. "regs" are ILLUSTRATIVE values derived from pid and program counter, not real registers.
struct LcPCB {
    int pid = 0, ppid = 1;
    std::string name;
    ProcState state = ProcState::New;
    int priority = 0, arrival = 0, pages = 0;
    std::vector<int> bursts;
    int burstIndex = 0;               // which burst is current
    int burstLeft = 0;                // ticks left in the current CPU burst
    int ioLeft = 0;                   // ticks left in the current I/O (while Waiting)
    int cpuUsed = 0, cpuTotal = 0;    // CPU ticks used / needed in total
    int pc = 0;                       // modelled program counter (instruction index)
    int regs[4] = {0, 0, 0, 0};       // modelled register context saved at context switches
    std::string waitReason;           // e.g. "disk I/O (3 ticks)"
    int firstRun = -1, finish = -1, waitTicks = 0, readyTicks = 0, dispatches = 0;
    bool killed = false;
};

struct LcSnapshot {
    int clock = 0;                    // logical time (ticks)
    LcEvent event = LcEvent::Idle;
    int pid = -1;                     // process of the event (-1 = CPU / system)
    ProcState from = ProcState::New, to = ProcState::New;
    std::string text;                 // explanation of the event
    std::vector<LcPCB> pcbs;
    std::vector<int> readyQueue;      // pids in queue order
    std::vector<int> waitQueue;       // pids in the order they blocked
    int running = -1;                 // pid on the CPU, -1 = idle
    int contextSwitches = 0;
};

class LifecycleSim {
public:
    // Validates the workload (throws std::invalid_argument). manual = no automatic events.
    LifecycleSim(const std::vector<LcProcSpec>& procs, Algo policy, int quantum, bool manual = false);

    // Automatic mode: runs to completion and returns the number of events.
    int runAuto(int maxTicks = 2000);

    // ---- validated transitions (manual commands; also used by runAuto). Return "" on
    // success, otherwise the reason why the transition is not allowed (state unchanged).
    std::string create(const LcProcSpec& spec);             // -> New (a child of spec.ppid)
    std::string admit(int pid);                             // New -> Ready
    std::string dispatch(int pid);                          // Ready -> Running (CPU must be free)
    std::string preempt();                                  // Running -> Ready
    std::string blockIO(int ticks);                         // Running -> Waiting
    std::string completeIO(int pid);                        // Waiting -> Ready
    std::string exitRunning();                              // Running -> Terminated
    std::string kill(int pid);                              // New/Ready/Waiting -> Terminated (labelled kill)
    std::string tick();                                     // logical time +1 (CPU / I/O accounting)

    const std::vector<LcSnapshot>& trace() const { return trace_; }
    // Keep only the first n+1 snapshots (a manual command after stepping back discards the future).
    void truncate(int n);
    const LcSnapshot& current() const { return trace_.back(); }
    bool allTerminated() const;
    Algo policy() const { return policy_; }
    int quantum() const { return quantum_; }

private:
    int idx(int pid) const;
    void record(LcEvent ev, int pid, ProcState from, ProcState to, const std::string& text);
    void saveContext(LcPCB& p);
    std::vector<LcPCB> pcbs_;
    std::vector<int> ready_, wait_;
    int running_ = -1, clock_ = 0, switches_ = 0;
    Algo policy_;
    int quantum_;
    bool manual_;
    // scheduler mirror for the automatic mode (indices = positions in pcbs_)
    std::vector<PCB> sched_;
    SchedState st_;
    std::vector<LcSnapshot> trace_;
    std::vector<LcProcSpec> pending_;   // automatic mode: processes that have not arrived yet
};

// Two-state view: Running vs Not Running (aggregates New, Ready, Waiting; Terminated excluded).
const char* twoStateName(ProcState);

// Parses "pid,ppid,name,arrival,priority,bursts(space separated),pages" lines ('#' comments).
std::vector<LcProcSpec> loadLifecycleCsv(const std::string& path);
// Built-in presets (index 0 = the specification's two-process I/O example).
struct LcPreset { std::string name, description; std::vector<LcProcSpec> procs; Algo policy; int quantum; };
std::vector<LcPreset> lifecyclePresets();
