// test_scheduler.cpp - acceptance tests of Section 6.1 (CPU scheduling).
#include "check.h"
#include "scheduler.h"
#include "io_formats.h"
#include <stdexcept>

// Builds a PCB quickly.
static PCB P(int pid, int arr, int burst, int prio = 0) {
    PCB p; p.pid = pid; p.arrival = arr; p.burst = burst; p.priority = prio; return p;
}
// Returns the metrics row of one pid.
static const ProcMetrics& M(const SchedResult& r, int pid) {
    for (const auto& m : r.metrics) if (m.pid == pid) return m;
    static ProcMetrics none{}; return none;
}
static bool seg(const Segment& s, int pid, int a, int b) { return s.pid == pid && s.start == a && s.end == b; }

int main() {
    std::vector<PCB> fcfs = {P(1, 0, 24), P(2, 0, 3), P(3, 0, 3)};

    // FCFS 24/3/3
    SchedResult r = runScheduler(Algo::FCFS, fcfs);
    CHECK(M(r, 1).waiting == 0); CHECK(M(r, 2).waiting == 24); CHECK(M(r, 3).waiting == 27);
    CHECK_NEAR(r.avgWaiting, 17.0);
    CHECK_NEAR(r.avgTurnaround, 27.0);
    CHECK(r.timeline.size() == 3);
    if (r.timeline.size() == 3) {
        CHECK(seg(r.timeline[0], 1, 0, 24)); CHECK(seg(r.timeline[1], 2, 24, 27)); CHECK(seg(r.timeline[2], 3, 27, 30));
    }
    CHECK(M(r, 1).completion == 24 && M(r, 2).completion == 27 && M(r, 3).completion == 30);
    CHECK(r.contextSwitches == 2);

    // SJF on the same input: order P2, P3, P1
    r = runScheduler(Algo::SJF, fcfs);
    CHECK(r.timeline.size() == 3 && r.timeline[0].pid == 2 && r.timeline[1].pid == 3 && r.timeline[2].pid == 1);
    CHECK(M(r, 1).waiting == 6); CHECK(M(r, 2).waiting == 0); CHECK(M(r, 3).waiting == 3);
    CHECK_NEAR(r.avgWaiting, 3.0);

    // RR q=4
    r = runScheduler(Algo::RR, fcfs, 4);
    CHECK(M(r, 1).waiting == 6); CHECK(M(r, 2).waiting == 4); CHECK(M(r, 3).waiting == 7);
    CHECK_NEAR(r.avgWaiting, 17.0 / 3.0);
    CHECK(r.timeline.size() == 4);   // P1 0-4, P2 4-7, P3 7-10, P1 10-30 (merged)
    if (r.timeline.size() == 4) CHECK(seg(r.timeline[3], 1, 10, 30));
    CHECK(r.quantum == 4);

    // SRTF 8/4/9/5
    std::vector<PCB> srtf = {P(1, 0, 8), P(2, 1, 4), P(3, 2, 9), P(4, 3, 5)};
    r = runScheduler(Algo::SRTF, srtf);
    CHECK_NEAR(r.avgWaiting, 6.5);
    // SJF non-preemptive on the same input
    r = runScheduler(Algo::SJF, srtf);
    CHECK_NEAR(r.avgWaiting, 7.75);

    // Idle gap
    r = runScheduler(Algo::FCFS, {P(1, 0, 2), P(2, 5, 2)});
    CHECK(r.timeline.size() == 3);
    if (r.timeline.size() == 3) {
        CHECK(seg(r.timeline[0], 1, 0, 2)); CHECK(seg(r.timeline[1], -1, 2, 5)); CHECK(seg(r.timeline[2], 2, 5, 7));
    }
    CHECK(r.contextSwitches == 1);

    // Priority non-preemptive
    std::vector<PCB> pr = {P(1, 0, 10, 3), P(2, 0, 1, 1), P(3, 0, 2, 4), P(4, 0, 1, 5), P(5, 0, 5, 2)};
    r = runScheduler(Algo::PRIORITY_NP, pr);
    int order[] = {2, 5, 1, 3, 4};
    CHECK(r.timeline.size() == 5);
    for (size_t i = 0; i < 5 && i < r.timeline.size(); ++i) CHECK(r.timeline[i].pid == order[i]);
    CHECK_NEAR(r.avgWaiting, 8.2);

    // Preemptive priority: a higher-priority arrival takes the CPU at once
    r = runScheduler(Algo::PRIORITY_P, {P(1, 0, 5, 2), P(2, 2, 2, 1)});
    CHECK(r.timeline.size() == 3);
    if (r.timeline.size() == 3) { CHECK(seg(r.timeline[0], 1, 0, 2)); CHECK(seg(r.timeline[1], 2, 2, 4)); CHECK(seg(r.timeline[2], 1, 4, 7)); }

    // RR rule: arrival at the quantum expiry tick is queued before the preempted process
    r = runScheduler(Algo::RR, {P(1, 0, 4), P(2, 2, 2)}, 2);
    CHECK(r.timeline.size() == 3);
    if (r.timeline.size() == 3) { CHECK(seg(r.timeline[0], 1, 0, 2)); CHECK(seg(r.timeline[1], 2, 2, 4)); CHECK(seg(r.timeline[2], 1, 4, 6)); }

    // Validation
    CHECK_THROWS(runScheduler(Algo::FCFS, {P(1, 0, 0)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::FCFS, {P(1, 0, 3), P(1, 1, 2)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::FCFS, {P(1, -1, 3)}), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::RR, {P(1, 0, 3)}, 0), std::invalid_argument);
    CHECK_THROWS(runScheduler(Algo::FCFS, {}), std::invalid_argument);

    // runAll gives one result per standard algorithm (eight since LJF/LRTF); CSV sample avg WT 17
    std::vector<PCB> loaded = loadProcessesCsv(samplePath("processes_fcfs.csv"));
    CHECK(loaded.size() == 3);
    std::vector<SchedResult> all = runAll(loaded, 4);
    CHECK(all.size() == 8 && all.size() == allAlgos().size());
    CHECK_NEAR(all[0].avgWaiting, 17.0);
    CHECK_THROWS(loadProcessesCsv(samplePath("does_not_exist.csv")), std::runtime_error);

    return report("test_scheduler");
}
