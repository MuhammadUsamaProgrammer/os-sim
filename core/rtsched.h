// rtsched.h - Phase 3 "Real-Time Scheduling": preemptive Rate Monotonic (RM) and Earliest Deadline
// First (EDF) on ONE logical CPU (pure C++17, deterministic, integer time).
//
// Model and semantics (documented and tested):
//   - periodic tasks with execution time C, period T, relative deadline D and release offset O; job k of
//     a task is released at O + k*T with absolute deadline O + k*T + D. Constrained deadlines are
//     REQUIRED: 0 < C <= D <= T (overlapping jobs of one task are still possible after a miss under the
//     "continue" policy and are handled FIFO within the task).
//   - order of events at an instant t: (1) completions of the slot [t-1, t), (2) deadline checks
//     (remaining > 0 and absolute deadline == t -> MISS; a job that completes exactly at its deadline is
//     on time), (3) releases at t, (4) dispatch of the highest-priority ready job for the slot [t, t+1).
//   - RM: fixed priority, shorter period = higher priority; ties -> lower task index; within a task FIFO.
//   - EDF: earliest absolute deadline; ties -> earlier release -> lower task index.
//   - deadline-miss policy: Continue (the late job keeps running; counted once) or Abort (dropped at its
//     deadline; counted once).
//   - horizon H: slots [0, H); deadlines at H are still checked; unfinished jobs with deadline > H are
//     reported as "incomplete at the horizon", not as misses.
//   - no overhead (context switches cost 0), independent tasks, no blocking.
#pragma once
#include <string>
#include <vector>

enum class RtAlgo { RM, EDF };
const char* rtAlgoName(RtAlgo);
enum class MissPolicy { Continue, Abort };
const char* missPolicyName(MissPolicy);

struct RtTask { std::string name; int C = 1, T = 4, D = 4, offset = 0; };
struct RtConfig {
    std::vector<RtTask> tasks;
    int horizon = 20;
    RtAlgo algo = RtAlgo::RM;
    MissPolicy policy = MissPolicy::Continue;
};
void validateRtConfig(const RtConfig&);   // throws std::invalid_argument

struct RtJob {
    int task = 0, index = 0;                 // job index k of the task
    int release = 0, deadline = 0, remaining = 0;
    int start = -1, finish = -1;             // first dispatch, completion time
    bool missed = false, dropped = false;
    int responseTime() const { return finish >= 0 ? finish - release : -1; }
};

enum class RtEventKind { Release, Dispatch, Preempt, Complete, DeadlineMiss, Drop, Idle, Incomplete };
const char* rtEventName(RtEventKind);
struct RtEvent { int time = 0; RtEventKind kind = RtEventKind::Release; int task = -1, job = -1; std::string text; };

struct RtSlice { int start = 0, end = 0, task = -1, job = -1; };   // task -1 = idle; adjacent equal slices merged

struct RtTaskStats {
    int released = 0, completed = 0, missed = 0, dropped = 0, incomplete = 0;
    int worstResponse = -1;
    double avgResponse = 0;
    int maxLateness = 0;                     // completion - deadline of late jobs (0 if never late)
};

struct RtResult {
    RtConfig cfg;
    std::vector<RtSlice> timeline;           // CPU timeline
    std::vector<int> slotJob;                // job index (into jobs) running in slot t, -1 idle
    std::vector<RtEvent> events;             // in time order
    std::vector<RtJob> jobs;                 // every released job
    std::vector<RtTaskStats> stats;          // per task
    double utilization = 0;
    int misses = 0, firstMiss = -1, preemptions = 0, contextSwitches = 0, idleSlots = 0;
};
RtResult runRt(const RtConfig&);

// Classical utilization tests (assumptions stated in the text).
struct RtAnalysis {
    double U = 0, rmBound = 0;
    bool implicitDeadlines = true;           // D == T for every task
    bool rmGuaranteed = false;               // U <= n(2^(1/n) - 1) and implicit deadlines (sufficient, not necessary)
    bool edfFeasible = false;                // U <= 1 and implicit deadlines (necessary and sufficient under the assumptions)
    std::string text;
};
RtAnalysis analyseRt(const RtConfig&);
double rmUtilizationBound(int n);

struct RtPreset { std::string name, description; RtConfig cfg; };
std::vector<RtPreset> rtPresets();
