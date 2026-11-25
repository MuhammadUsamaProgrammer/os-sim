// pcb.h - Process Control Block (OS concept: process management).
// Every simulated process is described by one PCB. The state field walks
// through the classic 5-state model: New -> Ready -> Running -> Waiting -> Terminated.
#pragma once
#include <vector>

enum class ProcState { New, Ready, Running, Waiting, Terminated };

// Returns the upper-case name of a process state, e.g. "RUNNING" (used in logs).
inline const char* stateName(ProcState s) {
    switch (s) {
        case ProcState::New:        return "NEW";
        case ProcState::Ready:      return "READY";
        case ProcState::Running:    return "RUNNING";
        case ProcState::Waiting:    return "WAITING";
        case ProcState::Terminated: return "TERMINATED";
    }
    return "?";
}

struct PCB {
    int pid = 0;
    int arrival = 0;
    int burst = 0;
    int remaining = 0;      // set to burst before a run
    int priority = 0;       // lower number = higher priority
    int pages = 0;          // number of pages the process needs
    ProcState state = ProcState::New;
    int firstRun = -1;      // first time on CPU (for response time)
    int finish = -1;        // completion time
    std::vector<int> maxRes;    // resource Max vector (for Banker's)
    std::vector<int> allocRes;  // resource Allocation vector
};
