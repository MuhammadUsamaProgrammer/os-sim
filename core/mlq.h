// mlq.h - Phase 3 gap fill: Multilevel Queue (MLQ) and Multilevel Feedback Queue (MLFQ) CPU
// scheduling (pure C++17, deterministic, tick based). Reuses the scheduler's data types (PCB,
// Segment, ProcMetrics, SchedResult) and appendTick() so results look like every other algorithm.
//
// Policy (documented and tested):
//   - queues are ordered by priority: queue 0 is the highest; a lower queue runs only when every
//     higher queue is empty (fixed priority between queues);
//   - each queue is Round Robin with its own quantum, or FCFS (no quantum);
//   - MLQ: a process stays in the queue given by its "queue" (= the priority column, clamped);
//   - MLFQ: new processes enter queue 0; a process that uses its whole quantum is DEMOTED one level;
//     a process preempted by a higher queue keeps its level and goes to the tail with a fresh
//     quantum; optional priority boost every S ticks moves every process back to queue 0
//     (prevents starvation of long jobs);
//   - order at tick t: arrivals (pid order) -> quantum expiry of the previous runner (re-queued AFTER
//     the arrivals, as in the RR rule of the scheduler) -> boost -> preemption check -> dispatch.
#pragma once
#include <string>
#include <vector>
#include "scheduler.h"

enum class QueuePolicy { RR, FCFS };

struct MlqQueue { std::string name; QueuePolicy policy = QueuePolicy::RR; int quantum = 2; };

struct MlqConfig {
    bool feedback = false;              // false = MLQ, true = MLFQ
    std::vector<MlqQueue> queues;       // index 0 = highest priority (1..4 queues)
    int boostInterval = 0;              // MLFQ priority boost period (0 = off)
    bool preemptive = true;             // a process in a higher queue preempts a lower one
};
void validateMlqConfig(const MlqConfig&, const std::vector<PCB>& procs);   // throws std::invalid_argument

struct MlqTick {
    int t = 0;
    int pid = -1, queue = -1;           // process that runs in [t, t+1) and its queue
    std::vector<std::vector<int>> queues;   // pids waiting in each queue (after the dispatch)
    std::vector<std::string> events;    // arrivals, demotions, boosts, preemptions, completions
};

struct MlqResult {
    MlqConfig cfg;
    SchedResult sched;                  // timeline, metrics, averages, context switches
    std::vector<MlqTick> ticks;
    std::vector<int> queueOf;           // final queue per process (PCB order)
    int demotions = 0, boosts = 0, preemptions = 0;
};
MlqResult runMlq(const MlqConfig& cfg, std::vector<PCB> procs);

struct MlqPreset { std::string name, description; MlqConfig cfg; std::vector<PCB> procs; };
std::vector<MlqPreset> mlqPresets();
