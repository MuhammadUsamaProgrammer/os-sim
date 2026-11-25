// test_engine.cpp - acceptance tests of Section 6.4 (threaded replay engine).
#include "check.h"
#include "engine.h"
#include "scheduler.h"
#include <csignal>
#include <unistd.h>

static PCB P(int pid, int arr, int burst, int prio = 0) {
    PCB p; p.pid = pid; p.arrival = arr; p.burst = burst; p.priority = prio; return p;
}

// Runs the engine to completion with no delay and returns its log.
static std::vector<std::string> runOnce(const std::vector<PCB>& procs, const SchedResult& r,
                                        std::vector<PCB>* finalPcbs = nullptr, int* events = nullptr) {
    SimulationEngine eng(procs, r.timeline, EngineOptions{0, false});
    int count = 0;
    eng.setObserver([&](const EngineEvent&) { ++count; });   // runs on the dispatcher thread only
    eng.start();
    eng.join();
    if (finalPcbs) *finalPcbs = eng.pcbs();
    if (events) *events = count;
    return eng.log();
}

// Polls until the engine reaches a tick (or the watchdog fires).
static void waitTick(SimulationEngine& e, int tick) {
    while (e.snapshot().tick < tick && !e.finished()) usleep(1000);
}

int main() {
    alarm(5);    // watchdog: any hang/deadlock kills the test with SIGALRM (= failure)

    std::vector<PCB> fcfs = {P(1, 0, 24), P(2, 0, 3), P(3, 0, 3)};
    SchedResult r = runScheduler(Algo::FCFS, fcfs);
    std::vector<PCB> fin;
    int events = 0;
    std::vector<std::string> log0 = runOnce(fcfs, r, &fin, &events);
    for (const PCB& p : fin) {
        int expected = (p.pid == 1) ? 24 : (p.pid == 2) ? 27 : 30;
        CHECK(p.finish == expected);
        CHECK(p.state == ProcState::Terminated && p.remaining == 0);
        int term = 0;
        for (const std::string& l : log0)
            if (l.find("P" + std::to_string(p.pid) + " TERMINATED") != std::string::npos) ++term;
        CHECK(term == 1);
    }
    CHECK(events == (int)log0.size());                // the observer saw every line once
    CHECK(!log0.empty() && log0.front() == "[t=0] CLOCK tick");
    CHECK(!log0.empty() && log0.back() == "[t=30] CLOCK finished");

    // Byte-identical logs on 20 runs
    bool same = true;
    for (int i = 0; i < 20; ++i) same = same && (runOnce(fcfs, r) == log0);
    CHECK(same);

    // Other algorithms: engine finish times equal core completion times
    std::vector<PCB> srtf = {P(1, 0, 8), P(2, 1, 4), P(3, 2, 9), P(4, 3, 5)};
    for (Algo a : {Algo::SRTF, Algo::RR, Algo::SJF}) {
        SchedResult rs = runScheduler(a, srtf, 2);
        runOnce(srtf, rs, &fin);
        for (const PCB& p : fin)
            for (const ProcMetrics& m : rs.metrics)
                if (m.pid == p.pid) CHECK(p.finish == m.completion);
    }
    // Idle gap: the clock logs CPU idle
    SchedResult gap = runScheduler(Algo::FCFS, {P(1, 0, 2), P(2, 5, 2)});
    std::vector<std::string> gl = runOnce({P(1, 0, 2), P(2, 5, 2)}, gap);
    bool idleSeen = false;
    for (const std::string& l : gl) if (l == "[t=3] CPU idle") idleSeen = true;
    CHECK(idleSeen);

    // Log line parsing
    EngineEvent e = parseLogLine("[t=3] P2 RUNNING remaining=1");
    CHECK(e.tick == 3 && e.pid == 2 && e.state == ProcState::Running);
    CHECK(parseLogLine("[t=7] CLOCK tick").pid == -1);

    // Pause -> step -> resume -> pause -> stop must not hang (watchdog above)
    {
        SimulationEngine eng(fcfs, r.timeline, EngineOptions{5, false});
        eng.start(true);                       // starts paused
        usleep(20000);
        CHECK(eng.snapshot().tick == 0 && eng.log().empty());   // nothing ran yet
        eng.step(); eng.step(); eng.step();    // exactly three ticks
        waitTick(eng, 2);
        usleep(30000);
        EngineSnapshot s = eng.snapshot();
        CHECK(s.tick == 2 && s.paused);
        eng.resume();
        waitTick(eng, 6);
        eng.pause();
        usleep(20000);
        int t1 = eng.snapshot().tick;
        usleep(30000);
        CHECK(eng.snapshot().tick == t1);      // really paused
        eng.stop();
        eng.join();
        CHECK(eng.finished());
        CHECK(eng.log().back().find("CLOCK stopped") != std::string::npos);
    }
    // Stop while running, and destroying an engine that was never started
    {
        SimulationEngine eng(fcfs, r.timeline, EngineOptions{2, false});
        eng.start();
        usleep(10000);
        eng.stop();
        eng.join();
        CHECK(eng.finished());
        SimulationEngine unused(fcfs, r.timeline);
    }
    alarm(0);
    return report("test_engine");
}
