// test_disk.cpp - M1 disk scheduling (core/disk.*): Phase 2 reference table, directions,
// duplicates, head / boundary requests, one side, single / empty queue, ties, validation.
#include <algorithm>
#include "check.h"
#include "disk.h"

static std::vector<int> cyls(const DiskResult& r) {
    std::vector<int> v;
    for (int id : r.order) v.push_back(r.cfg.requests[id]);
    return v;
}

// Every valid request serviced exactly once; totals consistent with the move list.
static bool invariants(const DiskResult& r) {
    std::vector<int> o = r.order;
    std::sort(o.begin(), o.end());
    for (int i = 0; i < (int)o.size(); ++i) if (o[i] != i) return false;
    if ((int)o.size() != (int)r.cfg.requests.size()) return false;
    int sum = 0, head = r.cfg.head;
    for (const DiskMove& m : r.moves) {
        if (m.from != head || m.distance != std::abs(m.to - m.from)) return false;
        if (m.requestId >= 0 && r.cfg.requests[m.requestId] != m.to) return false;
        if (m.to < r.cfg.minCyl || m.to > r.cfg.maxCyl) return false;
        sum += m.distance;
        if (m.cumulative != sum) return false;
        head = m.to;
    }
    return sum == r.total;
}

int main() {
    DiskConfig c;                                  // Phase 2 reference preset
    c.requests = {98, 183, 37, 122, 14, 124, 65, 67};
    c.head = 53;
    struct Exp { DiskAlgo a; int total; std::vector<int> order; };
    std::vector<Exp> table = {
        {DiskAlgo::FCFS, 640, {98, 183, 37, 122, 14, 124, 65, 67}},
        {DiskAlgo::SSTF, 236, {65, 67, 37, 14, 98, 122, 124, 183}},
        {DiskAlgo::SCAN, 331, {65, 67, 98, 122, 124, 183, 37, 14}},
        {DiskAlgo::CSCAN, 382, {65, 67, 98, 122, 124, 183, 14, 37}},
        {DiskAlgo::LOOK, 299, {65, 67, 98, 122, 124, 183, 37, 14}},
        {DiskAlgo::CLOOK, 322, {65, 67, 98, 122, 124, 183, 14, 37}},
    };
    for (const Exp& e : table) {
        DiskResult r = runDisk(e.a, c);
        std::printf("%-6s total %d order", diskAlgoName(e.a), r.total);
        for (int x : cyls(r)) std::printf(" %d", x);
        std::printf("\n");
        CHECK(r.total == e.total);
        CHECK(cyls(r) == e.order);
        CHECK(r.serviced == 8);
        CHECK_NEAR(r.average, e.total / 8.0);
        CHECK(invariants(r));
    }
    // SCAN visits 199 as transit only; C-SCAN wraps 199 -> 0 (counted).
    DiskResult scan = runDisk(DiskAlgo::SCAN, c);
    bool boundary = false;
    for (const DiskMove& m : scan.moves) if (m.kind == MoveKind::Boundary && m.to == 199 && m.requestId < 0) boundary = true;
    CHECK(boundary);
    DiskResult cscan = runDisk(DiskAlgo::CSCAN, c);
    bool wrap = false;
    for (const DiskMove& m : cscan.moves) if (m.kind == MoveKind::Wrap && m.from == 199 && m.to == 0 && m.distance == 199) wrap = true;
    CHECK(wrap);
    DiskResult look = runDisk(DiskAlgo::LOOK, c);
    int reversals = 0;
    for (const DiskMove& m : look.moves) reversals += m.reversed ? 1 : 0;
    CHECK(reversals == 1);

    // Reverse direction (toward lower cylinders), hand-derived:
    // SCAN: 53->37->14->0 (53) then up to 183 (183) = 236; LOOK: 53->14 (39) + 14->183 (169) = 208
    // C-SCAN: 53->0 (53), wrap 0->199 (199), 199->65 (134) = 386; C-LOOK: 53->14 (39), jump 14->183 (169), 183->65 (118) = 326
    DiskConfig low = c;
    low.dir = DiskDir::Lower;
    CHECK(runDisk(DiskAlgo::SCAN, low).total == 236);
    CHECK(runDisk(DiskAlgo::LOOK, low).total == 208);
    CHECK(runDisk(DiskAlgo::CSCAN, low).total == 386);
    CHECK(runDisk(DiskAlgo::CLOOK, low).total == 326);
    CHECK((cyls(runDisk(DiskAlgo::SCAN, low)) == std::vector<int>{37, 14, 65, 67, 98, 122, 124, 183}));
    CHECK((cyls(runDisk(DiskAlgo::CLOOK, low)) == std::vector<int>{37, 14, 183, 124, 122, 98, 67, 65}));
    for (DiskAlgo a : allDiskAlgos()) CHECK(invariants(runDisk(a, low)));

    // Duplicates and a request at the head position.
    DiskConfig dup;
    dup.head = 50;
    dup.requests = {50, 70, 70, 20, 50};
    for (DiskAlgo a : allDiskAlgos()) {
        DiskResult r = runDisk(a, dup);
        CHECK(invariants(r));
        CHECK(r.serviced == 5);
    }
    DiskResult sd = runDisk(DiskAlgo::SSTF, dup);
    CHECK((sd.order == std::vector<int>{0, 4, 1, 2, 3}));      // 50, 50 (0 distance), then 70, 70, 20
    CHECK(sd.moves[0].distance == 0 && sd.moves[1].distance == 0);
    CHECK(runDisk(DiskAlgo::SCAN, dup).order.front() == 0);   // head request first, distance 0

    // SSTF tie: head 50, requests 40 and 60 (both distance 10) -> earlier input (40) first.
    DiskConfig tie;
    tie.head = 50;
    tie.requests = {40, 60};
    CHECK(runDisk(DiskAlgo::SSTF, tie).order.front() == 0);
    tie.requests = {60, 40};
    CHECK(runDisk(DiskAlgo::SSTF, tie).order.front() == 0);

    // Requests on the boundaries and all on one side.
    DiskConfig ends;
    ends.head = 100;
    ends.requests = {0, 199};
    CHECK(runDisk(DiskAlgo::SCAN, ends).total == 99 + 199);       // up to 199 (serviced), back to 0
    CHECK(runDisk(DiskAlgo::CSCAN, ends).total == 99 + 199);      // 199 serviced, wrap to 0 serviced
    DiskConfig oneSide;
    oneSide.head = 53;
    oneSide.requests = {60, 90, 150};
    CHECK(runDisk(DiskAlgo::SCAN, oneSide).total == 97);           // no boundary trip needed
    CHECK(runDisk(DiskAlgo::CSCAN, oneSide).total == 97);
    oneSide.requests = {10, 20};                                     // all behind the head
    CHECK(runDisk(DiskAlgo::SCAN, oneSide).total == (199 - 53) + (199 - 10));
    CHECK(runDisk(DiskAlgo::LOOK, oneSide).total == 53 - 10);
    CHECK(runDisk(DiskAlgo::CLOOK, oneSide).total == 53 - 10 + 10);  // jump to 10, then up to 20

    // Single request and empty queue.
    DiskConfig one;
    one.head = 53;
    one.requests = {100};
    for (DiskAlgo a : allDiskAlgos()) CHECK(runDisk(a, one).total == 47);
    DiskConfig none;
    for (DiskAlgo a : allDiskAlgos()) {
        DiskResult r = runDisk(a, none);
        CHECK(r.total == 0 && r.serviced == 0 && r.moves.empty() && r.average == 0);
    }

    // Random workloads: reproducible, every request serviced exactly once.
    CHECK(randomDiskRequests(20, 0, 199, 7) == randomDiskRequests(20, 0, 199, 7));
    for (unsigned seed = 1; seed <= 50; ++seed) {
        DiskConfig rc;
        rc.head = (int)(seed * 37 % 200);
        rc.dir = seed % 2 ? DiskDir::Higher : DiskDir::Lower;
        rc.requests = randomDiskRequests(15, 0, 199, seed);
        std::vector<DiskResult> all = runAllDisk(rc);
        for (const DiskResult& r : all) CHECK(invariants(r));
        // SSTF never moves more than FCFS on these workloads is NOT guaranteed; but LOOK <= SCAN is.
        CHECK(all[4].total <= all[2].total);
        CHECK(all[5].total <= all[3].total);
    }

    // Validation
    DiskConfig bad = c;
    bad.requests.push_back(200);
    CHECK_THROWS(runDisk(DiskAlgo::FCFS, bad), std::invalid_argument);
    bad = c;
    bad.head = -1;
    CHECK_THROWS(runDisk(DiskAlgo::FCFS, bad), std::invalid_argument);
    bad = c;
    bad.maxCyl = 0;
    CHECK_THROWS(runDisk(DiskAlgo::FCFS, bad), std::invalid_argument);

    // Sample file
    DiskConfig f = loadDiskFile(samplePath("disk_reference.txt"));
    CHECK(f.head == 53 && f.requests == c.requests && f.maxCyl == 199 && f.dir == DiskDir::Higher);
    CHECK_THROWS(loadDiskFile(samplePath("banker_sample.txt")), std::runtime_error);
    return report("test_disk");
}
