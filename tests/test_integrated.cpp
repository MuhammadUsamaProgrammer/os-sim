// test_integrated.cpp - integrated run: scheduling + paging + resources (Stage 9).
#include "check.h"
#include "integrated.h"
#include <algorithm>

// Counts log lines containing a text.
static int count(const IntegratedResult& r, const std::string& s) {
    int c = 0;
    for (const std::string& l : r.log) if (l.find(s) != std::string::npos) ++c;
    return c;
}

int main() {
    std::vector<PCB> procs = loadProcessesCsv(samplePath("processes_srtf.csv"));
    IntegratedConfig cfg;
    cfg.algo = Algo::RR;
    cfg.quantum = 2;
    cfg.numFrames = 6;
    cfg.pageAlgo = PageAlgo::LRU;
    cfg.requests = loadRequestsCsv(samplePath("requests_integrated.csv"), &cfg.totalRes);
    CHECK((cfg.totalRes == Vec{1, 1, 2}));
    CHECK(cfg.requests.size() == 6);

    // Avoidance ON: Banker's never lets the system deadlock
    cfg.avoidance = true;
    IntegratedResult on = runIntegrated(procs, cfg);
    CHECK(on.deadlocksDetected == 0);
    CHECK(on.killedPids.empty());
    CHECK(!on.stalled);
    CHECK(on.requestsDenied > 0);                         // some request had to wait
    for (const ProcMetrics& m : on.metrics) CHECK(m.completion > 0);
    CHECK(on.memFaults + on.memHits == 26);              // one reference per executed tick (total burst 8+4+9+5)
    CHECK(count(on, "TERMINATED") == 4);

    // Avoidance OFF: a real deadlock happens, detection finds it, recovery clears it
    cfg.avoidance = false;
    cfg.autoRecover = true;
    IntegratedResult off = runIntegrated(procs, cfg);
    CHECK(off.deadlocksDetected >= 1);
    CHECK(off.firstDeadlock.tick >= 0);
    CHECK(!off.killedPids.empty());
    CHECK(!off.stalled);
    CHECK(count(off, "DEADLOCK detected P1,P2") >= 1);
    CHECK(count(off, "RECOVERY killed") == (int)off.killedPids.size());
    CHECK(count(off, "TERMINATED") + (int)off.killedPids.size() == 4);   // everybody left the system

    // Without auto-recover the run stops in the deadlock
    cfg.autoRecover = false;
    IntegratedResult stuck = runIntegrated(procs, cfg);
    CHECK(stuck.stalled && stuck.deadlocksDetected == 1);

    // Deterministic: identical logs on repeated runs
    cfg.autoRecover = true;
    bool same = true;
    for (int i = 0; i < 10; ++i) same = same && runIntegrated(procs, cfg).log == off.log;
    CHECK(same);

    // Reference strings are deterministic, in range, and show locality
    std::vector<int> a = makeReferenceString(3, 5, 50), b = makeReferenceString(3, 5, 50);
    CHECK(a == b && a.size() == 50);
    CHECK(std::all_of(a.begin(), a.end(), [](int p) { return p >= 0 && p < 5; }));
    CHECK(makeReferenceString(1, 0, 10).empty());

    // Each page algorithm with every scheduler (avoidance on: every process completes all 26 ticks)
    cfg.avoidance = true;
    for (PageAlgo pa : {PageAlgo::FIFO, PageAlgo::LRU, PageAlgo::OPTIMAL})
        for (Algo al : allAlgos()) {
            cfg.pageAlgo = pa; cfg.algo = al;
            IntegratedResult r = runIntegrated(procs, cfg);
            CHECK(r.memFaults + r.memHits == 26 && !r.stalled);
        }

    // Bad input
    IntegratedConfig bad = cfg;
    bad.requests.push_back({99, 0, {1, 0, 0}});
    CHECK_THROWS(runIntegrated(procs, bad), std::invalid_argument);
    bad = cfg;
    bad.requests.push_back({1, 0, {5, 0, 0}});
    CHECK_THROWS(runIntegrated(procs, bad), std::invalid_argument);

    return report("test_integrated");
}
