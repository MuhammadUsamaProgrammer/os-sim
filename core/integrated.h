// integrated.h - integrated simulation: CPU scheduling + paging + resource
// allocation (Banker's avoidance or detection & recovery), tick by tick.
// Pure and deterministic like the rest of core/: same input -> same log.
#pragma once
#include <string>
#include <vector>
#include "deadlock.h"
#include "io_formats.h"
#include "memory.h"
#include "scheduler.h"

struct IntegratedConfig {
    Algo algo = Algo::RR;
    int quantum = 2;
    int numFrames = 6;                       // one shared pool of physical frames
    PageAlgo pageAlgo = PageAlgo::LRU;
    Vec totalRes;                            // total instances of each resource type
    std::vector<ScriptedRequest> requests;   // pid asks for req after executing runTick ticks
    bool avoidance = true;                   // true: Banker's request algorithm; false: grant if available
    int detectInterval = 5;                  // run detection every K ticks
    bool autoRecover = true;                 // terminate victims when a deadlock is detected
};

// State of the resource tables at the first detected deadlock (shown in the Deadlock tab).
struct DeadlockSnapshot { int tick = -1; Matrix alloc, max, request; Vec avail; std::vector<int> deadlockedRows; };

struct IntegratedResult {
    std::vector<std::string> log;            // "[t=..] ..." lines, same format as the engine
    std::vector<Segment> timeline;           // Gantt chart
    std::vector<ProcMetrics> metrics;        // sorted by pid; killed processes have completion = kill time
    std::vector<int> killedPids;
    double avgWaiting = 0, avgTurnaround = 0, avgResponse = 0;   // over processes that completed normally
    int memFaults = 0, memHits = 0;
    double hitRatio = 0;
    int requestsGranted = 0, requestsDenied = 0;
    int deadlocksDetected = 0;
    bool stalled = false;                    // deadlock without auto-recover: run stopped
    int endTime = 0;
    DeadlockSnapshot firstDeadlock;          // tick = -1 if no deadlock happened
    Matrix maxClaim;                         // per process (row = index in procs), sum of its requests
};

// Deterministic per-process page reference string with locality (std::mt19937(pid)).
std::vector<int> makeReferenceString(int pid, int pages, int length);

// Runs everything. Throws std::invalid_argument on inconsistent input.
IntegratedResult runIntegrated(const std::vector<PCB>& procs, const IntegratedConfig& cfg);
