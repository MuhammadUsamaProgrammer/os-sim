// test_deadlock.cpp - acceptance tests of Section 6.3 (Banker's, detection, recovery).
#include "check.h"
#include "deadlock.h"
#include "io_formats.h"
#include <stdexcept>

int main() {
    Matrix alloc = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    Matrix max   = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    Vec avail = {3,3,2};

    // Safety
    SafetyResult s = bankersSafety(alloc, max, avail);
    CHECK(s.safe);
    CHECK((s.need == Matrix{{7,4,3},{1,2,2},{6,0,0},{0,1,1},{4,3,1}}));
    CHECK((s.sequence == std::vector<int>{1,3,4,0,2}));
    CHECK(s.trace.size() == 5);
    CHECK((s.trace[0].workBefore == Vec{3,3,2}) && (s.trace[0].workAfter == Vec{5,3,2}));
    CHECK((s.trace[4].workAfter == Vec{10,5,7}));

    // Request P1 (1,0,2) -> granted
    RequestResult r = bankersRequest(1, {1,0,2}, alloc, max, avail);
    CHECK(r.granted);
    CHECK(r.safetyAfter.safe);
    // Apply the grant, then P4 (3,3,0) exceeds available and P0 (0,2,0) is unsafe
    Matrix alloc2 = alloc; Vec avail2 = avail;
    for (int j = 0; j < 3; ++j) { alloc2[1][j] += (Vec{1,0,2})[j]; avail2[j] -= (Vec{1,0,2})[j]; }
    CHECK((avail2 == Vec{2,3,0}));
    r = bankersRequest(4, {3,3,0}, alloc2, max, avail2);
    CHECK(!r.granted);
    CHECK(r.reason.find("exceeds available") != std::string::npos);
    r = bankersRequest(0, {0,2,0}, alloc2, max, avail2);
    CHECK(!r.granted);
    CHECK(r.reason.find("unsafe") != std::string::npos);
    CHECK(!r.safetyAfter.safe);
    // Request above Need
    r = bankersRequest(1, {5,0,0}, alloc, max, avail);
    CHECK(!r.granted);
    CHECK(r.reason.find("exceeds maximum claim") != std::string::npos);

    // Detection: no deadlock, pass-scan finish order P0, P2, P3, P4, P1
    Matrix dAlloc = {{0,1,0},{2,0,0},{3,0,3},{2,1,1},{0,0,2}};
    Matrix dReq   = {{0,0,0},{2,0,2},{0,0,0},{1,0,0},{0,0,2}};
    Vec dAvail = {0,0,0};
    DetectionResult d = detectDeadlock(dAlloc, dReq, dAvail);
    CHECK(!d.deadlocked);
    CHECK((d.finishOrder == std::vector<int>{0,2,3,4,1}));
    // P2 requests one more C -> P1..P4 deadlocked
    dReq[2] = {0,0,1};
    d = detectDeadlock(dAlloc, dReq, dAvail);
    CHECK(d.deadlocked);
    CHECK((d.deadlocked_pids == std::vector<int>{1,2,3,4}));
    // Recovery
    std::vector<RecoveryStep> rec = recoverByTermination(dAlloc, dReq, dAvail);
    CHECK(!rec.empty());
    CHECK(!rec.empty() && !rec.back().after.deadlocked);
    CHECK(!rec.empty() && rec[0].killedPid == 1);           // P1 and P4 hold 2 each -> lower pid
    CHECK(!rec.empty() && (rec[0].released == Vec{2,0,0}));

    // RAG: has request and assignment edges and finds a cycle in the deadlocked state
    RagGraph g = buildRag(dAlloc, dReq);
    CHECK(g.nProcs == 5 && g.nRes == 3);
    CHECK(!g.cycleNodes.empty());
    RagGraph g2 = buildRag({{1,0},{0,1}}, {{0,0},{0,0}});
    CHECK(g2.cycleNodes.empty() && g2.edges.size() == 2);
    RagGraph g3 = buildRag({{1,0},{0,1}}, {{0,1},{1,0}});   // classic 2-process cycle
    CHECK(g3.cycleNodes.size() == 4);

    // Validation
    CHECK_THROWS(bankersSafety(alloc, {{1,1,1}}, avail), std::invalid_argument);
    CHECK_THROWS(bankersSafety(alloc, max, {1,2}), std::invalid_argument);
    CHECK_THROWS(detectDeadlock(dAlloc, {{0,0}}, dAvail), std::invalid_argument);
    CHECK_THROWS(bankersSafety({{5,0,0}}, {{1,0,0}}, {0,0,0}), std::invalid_argument);   // alloc > max

    // Sample files
    MatrixData bs = loadMatrixFile(samplePath("banker_sample.txt"));
    CHECK(bs.nProcs == 5 && bs.nRes == 3 && bs.alloc == alloc && bs.max == max && bs.available == avail);
    CHECK(bs.request.empty());
    MatrixData ds = loadMatrixFile(samplePath("deadlock_sample.txt"));
    CHECK(ds.request == dReq && ds.alloc == dAlloc);
    CHECK(detectDeadlock(ds.alloc, ds.request, ds.available).deadlocked);
    CHECK_THROWS(loadMatrixFile(samplePath("processes_fcfs.csv")), std::runtime_error);
    std::vector<ScriptedRequest> reqs = loadRequestsCsv(samplePath("requests_integrated.csv"));
    CHECK(!reqs.empty());

    return report("test_deadlock");
}
