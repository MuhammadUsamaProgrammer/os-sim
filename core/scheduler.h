// scheduler.h - CPU scheduling algorithms (OS concept: CPU scheduling).
// Pure, deterministic C++17: no threads and no Qt. All algorithms are driven
// by one tick-based simulator; schedulerPick() is the "dispatcher decision"
// for a single tick and is shared with the integrated run (Stage 9).
#pragma once
#include <string>
#include <vector>
#include "pcb.h"

// LJF and LRTF were added later; they are appended so the old enum values stay unchanged.
// Never cast a combo-box index to Algo: allAlgos() order differs from the enum order.
enum class Algo { FCFS, SJF, SRTF, RR, PRIORITY_NP, PRIORITY_P, LJF, LRTF };
const char* algoName(Algo);
// All eight standard algorithms in display order: FCFS, SJF, SRTF, LJF, LRTF, RR,
// Priority (NP), Priority (P). Used by runAll and the GUI combo boxes.
const std::vector<Algo>& allAlgos();
// True for algorithms that can take the CPU away from a running process.
bool isPreemptive(Algo);
// Short console / CSV id ("fcfs", "sjf", "srtf", "ljf", "lrtf", "rr", "prio", "prio-p").
const char* algoId(Algo);
// Parses algoId() (case-insensitive) back to an Algo; returns false if unknown.
bool algoFromId(const std::string& id, Algo& out);

struct Segment { int pid; int start; int end; };   // pid == -1 means CPU idle

struct ProcMetrics { int pid; int arrival; int burst; int completion;
                     int waiting; int turnaround; int response; };

struct SchedResult {
    Algo algo = Algo::FCFS;
    int quantum = 0;                   // used by RR, else 0
    std::vector<Segment> timeline;     // adjacent segments of the same pid are merged
    std::vector<ProcMetrics> metrics;  // sorted by pid
    double avgWaiting = 0, avgTurnaround = 0, avgResponse = 0;
    int contextSwitches = 0;           // number of times the running pid changes (idle ignored)
};

// Runs one algorithm on a copy of the processes and returns the full result.
// Throws std::invalid_argument on bad input.
SchedResult runScheduler(Algo, std::vector<PCB> procs, int quantum = 4);
// Runs all eight standard algorithms (allAlgos() order) on the same input.
std::vector<SchedResult> runAll(const std::vector<PCB>&, int quantum);
// Checks burst > 0, arrival >= 0, unique pids (and quantum > 0 when RR is used).
void validateProcesses(const std::vector<PCB>&, Algo algo, int quantum);
// CPU utilisation in percent = busy ticks / total ticks * 100.
double cpuUtilization(const SchedResult&);

// ---- tick-level building blocks (shared by runScheduler and IntegratedRun) ----
// The ready queue and the currently running process (indices into the PCB vector).
struct SchedState {
    std::vector<int> ready;   // ready queue, in the order processes became ready
    int running = -1;         // index of running process, -1 = CPU free
    int sliceUsed = 0;        // ticks the running process has used in its RR quantum
};
// Moves process idx to the Ready state and appends it to the ready queue.
void makeReady(std::vector<PCB>& procs, SchedState& st, int idx);
// Dispatcher decision for one tick: applies preemption rules of the algorithm,
// then picks the process that runs during this tick. Returns its index or -1 (idle).
int schedulerPick(Algo algo, std::vector<PCB>& procs, SchedState& st, int quantum);
// Appends one tick [t, t+1) for pid to a timeline, merging with the previous segment.
void appendTick(std::vector<Segment>& timeline, int pid, int t);
