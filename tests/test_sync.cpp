// test_sync.cpp - synchronization simulator (core/sync.*): race condition, Peterson,
// mutex, semaphores, producer-consumer, monitor, comparison, validation, determinism.
#include <algorithm>
#include <deque>
#include <set>
#include "check.h"
#include "sync.h"

static SyncConfig cfgOf(SyncAlgo a, int procs, int iters, int slice = 1) {
    SyncConfig c;
    c.algo = a;
    c.processes = procs;
    c.iterations = iters;
    c.slice = slice;
    return c;
}

// Race: the textbook interleaving loses exactly one update.
static void testRace() {
    SyncConfig c = cfgOf(SyncAlgo::RACE, 2, 1);
    c.script = {0, 1, 0, 0, 1, 1};              // P0 read, P1 read, P0 inc, P0 write, P1 inc, P1 write
    SyncSim s(c);
    CHECK(s.runToEnd() == 6);
    SyncStats st = s.stats();
    CHECK(st.completed);
    CHECK(st.expected == 2);
    CHECK(st.actual == 1);
    CHECK(st.lostUpdates == 1);
    CHECK(st.csViolations == 1);                 // both were between read and write
    CHECK(s.counter() == 1);

    // Serial order: nothing lost.
    c.script = {0, 0, 0, 1, 1, 1};
    SyncSim serial(c);
    serial.runToEnd();
    CHECK(serial.counter() == 2 && serial.stats().lostUpdates == 0 && serial.stats().csViolations == 0);

    // Manual stepping (what the GUI's Step buttons do): same lost update.
    SyncSim m(cfgOf(SyncAlgo::RACE, 2, 1));
    for (int p : {0, 1, 0, 0, 1, 1}) CHECK(m.step(p));
    CHECK(m.counter() == 1 && m.finished());
    CHECK(!m.step(0));                           // a finished process cannot run

    // Round robin with 1 instruction per turn: 3 x 3 increments, many lost, deterministic.
    SyncSim rr1(cfgOf(SyncAlgo::RACE, 3, 3)), rr2(cfgOf(SyncAlgo::RACE, 3, 3));
    rr1.runToEnd();
    rr2.runToEnd();
    CHECK(rr1.stats().expected == 9);
    CHECK(rr1.stats().lostUpdates > 0);
    CHECK(rr1.stats().actual + rr1.stats().lostUpdates == 9);
    CHECK(rr1.log() == rr2.log());               // identical every time
    // With 3 instructions per turn each read-modify-write runs alone: correct result.
    SyncSim rr3(cfgOf(SyncAlgo::RACE, 3, 3, 3));
    rr3.runToEnd();
    CHECK(rr3.counter() == 9 && rr3.stats().lostUpdates == 0);
}

// Synchronized counters are always correct (several policies and seeds).
static void testSynchronizedCounters() {
    for (SyncAlgo a : {SyncAlgo::PETERSON, SyncAlgo::MUTEX, SyncAlgo::BINARY_SEM}) {
        int maxProcs = a == SyncAlgo::PETERSON ? 2 : 5;
        for (int n = (a == SyncAlgo::PETERSON ? 2 : 1); n <= maxProcs; ++n)
            for (int slice = 1; slice <= 4; ++slice) {
                SyncSim s(cfgOf(a, n, 3, slice));
                s.runToEnd();
                SyncStats st = s.stats();
                CHECK(st.completed && !st.stuck);
                CHECK(st.actual == n * 3 && st.lostUpdates == 0);
                CHECK(st.csViolations == 0 && st.maxInCS <= 1);
                CHECK(st.csEntries == n * 3);
            }
        for (unsigned seed = 1; seed <= 30; ++seed) {
            SyncConfig c = cfgOf(a, a == SyncAlgo::PETERSON ? 2 : 4, 3);
            c.randomOrder = true;
            c.seed = seed;
            SyncSim s(c);
            s.runToEnd();
            CHECK(s.stats().completed && s.stats().lostUpdates == 0 && s.stats().csViolations == 0);
        }
    }
}

// Peterson: explore EVERY reachable state (all interleavings) for 2 x 2 iterations.
// Mutual exclusion must hold in every state, and no state may be stuck (progress).
static void testPetersonExhaustive() {
    SyncSim start(cfgOf(SyncAlgo::PETERSON, 2, 2));
    std::set<std::string> seen{start.stateKey()};
    std::deque<SyncSim> todo{start};
    bool mutualExclusion = true, neverStuck = true, finalCorrect = true;
    int states = 0;
    while (!todo.empty()) {
        SyncSim s = todo.front();
        todo.pop_front();
        ++states;
        if (s.inCSCount() > 1 || s.stats().csViolations > 0) mutualExclusion = false;
        if (s.stats().stuck) neverStuck = false;
        if (s.stats().completed && s.counter() != 4) finalCorrect = false;
        for (int p = 0; p < 2; ++p) {
            if (!s.canRun(p)) continue;
            SyncSim n = s;
            n.step(p);
            if (seen.insert(n.stateKey()).second) todo.push_back(n);
        }
    }
    std::printf("Peterson: %d reachable states explored\n", states);
    CHECK(states > 100);
    CHECK(mutualExclusion);
    CHECK(neverStuck);
    CHECK(finalCorrect);

    // Specific interleaving: both interested, P1 writes turn last -> P1 spins, P0 enters.
    SyncSim b(cfgOf(SyncAlgo::PETERSON, 2, 1));
    for (int p : {0, 1, 0, 1, 0, 1}) b.step(p);  // other, other, interested, interested, turn=0, turn=1
    CHECK(b.turn() == 1 && b.interested(0) && b.interested(1));
    b.step(0);
    CHECK(b.procs()[0].inCS);                    // P0 passes: turn != 0
    b.step(1);
    CHECK(b.procs()[1].state == SyncProcState::Spinning && !b.procs()[1].inCS);
    CHECK(b.procs()[1].busyWaitSteps == 1);

    // Progress: P0 alone never waits.
    SyncSim alone(cfgOf(SyncAlgo::PETERSON, 2, 1));
    for (int i = 0; i < 8; ++i) alone.step(0);
    CHECK(alone.procs()[0].state == SyncProcState::Done && alone.procs()[0].busyWaitSteps == 0);

    // Bounded waiting: while a process waits (after turn = process), the other enters at most once.
    for (unsigned seed = 1; seed <= 200; ++seed) {
        SyncConfig c = cfgOf(SyncAlgo::PETERSON, 2, 4);
        c.randomOrder = true;
        c.seed = seed;
        SyncSim s(c);
        int waitingSince[2] = {-1, -1}, entriesOfOther[2] = {0, 0};
        bool bounded = true;
        while (!s.finished()) {
            int before[2] = {s.procs()[0].csEntries, s.procs()[1].csEntries};
            int p = s.pickNext();
            int lineBefore = s.procs()[p].pc;
            s.step(p);
            if (lineBefore == 2) { waitingSince[p] = 1; entriesOfOther[p] = 0; }   // just set turn
            int other = 1 - p;
            if (s.procs()[p].csEntries > before[p]) waitingSince[p] = -1;            // p entered
            if (s.procs()[p].csEntries > before[p] && waitingSince[other] > 0)
                if (++entriesOfOther[other] > 1) bounded = false;
        }
        CHECK(bounded);
        CHECK(s.counter() == 8);
    }
    CHECK_THROWS(SyncSim(cfgOf(SyncAlgo::PETERSON, 3, 1)), std::invalid_argument);
}

// Mutex: owner, FIFO queue, hand-off on release, free at the end.
static void testMutex() {
    SyncSim s(cfgOf(SyncAlgo::MUTEX, 3, 1));
    CHECK(s.step(0));                            // P0 acquires
    CHECK(s.objects()[0].owner == 0);
    CHECK(s.step(1));                            // P1 blocks
    CHECK(s.procs()[1].state == SyncProcState::Blocked && s.procs()[1].waitingOn == 0);
    CHECK(s.step(2));                            // P2 blocks behind P1
    CHECK(s.objects()[0].queue.size() == 2 && s.objects()[0].queue.front() == 1);
    CHECK(!s.step(1));                           // a blocked process cannot run
    for (int i = 0; i < 4; ++i) s.step(0);       // read, inc, write, release
    CHECK(s.objects()[0].owner == 1);            // handed to the first waiter (FIFO)
    CHECK(s.procs()[1].inCS && !s.procs()[0].inCS);
    CHECK(s.procs()[2].state == SyncProcState::Blocked);
    s.runToEnd();
    CHECK(s.objects()[0].owner == -1 && s.objects()[0].queue.empty());
    CHECK(s.counter() == 3 && s.stats().csViolations == 0);
    CHECK(s.stats().blockedSteps > 0);
}

// Semaphores: value limits and the number of processes inside.
static void testSemaphores() {
    SyncSim b(cfgOf(SyncAlgo::BINARY_SEM, 4, 2));
    bool ok = true;
    while (!b.finished()) {
        b.stepAuto();
        if (b.objects()[0].value < 0 || b.objects()[0].value > 1 || b.inCSCount() > 1) ok = false;
    }
    CHECK(ok);
    CHECK(b.counter() == 8 && b.stats().maxInCS == 1);

    SyncConfig c = cfgOf(SyncAlgo::COUNTING_SEM, 5, 2);
    c.resources = 2;
    SyncSim s(c);
    ok = true;
    bool sawBlocked = false;
    while (!s.finished()) {
        s.stepAuto();
        if (s.objects()[0].value < 0 || s.objects()[0].value > 2 || s.inCSCount() > 2) ok = false;
        if (!s.objects()[0].queue.empty()) sawBlocked = true;
    }
    CHECK(ok);
    CHECK(sawBlocked);                           // more processes than instances -> some block
    SyncStats st = s.stats();
    CHECK(st.completed && st.actual == 10 && st.expected == 10);
    CHECK(st.maxInCS == 2 && st.csViolations == 0);
    CHECK(s.objects()[0].value == 2);            // all permits returned
    c.resources = 0;
    CHECK_THROWS(SyncSim{c}, std::invalid_argument);
}

// Producer-consumer / monitor: buffer boundaries, blocking, every item consumed once.
static void testBuffer(SyncAlgo a) {
    std::vector<SyncScenario> sc = syncScenarios(a);
    CHECK(sc.size() == 3);
    // Scenario 0: fast producer -> producer blocks on a full buffer.
    {
        SyncSim s(sc[0].cfg);
        bool producerBlockedFull = false, ok = true;
        while (!s.finished()) {
            s.stepAuto();
            const SyncProc& p = s.procs()[0];
            if (p.state == SyncProcState::Blocked && s.bufferCount() == s.config().capacity) producerBlockedFull = true;
            if (s.bufferCount() < 0 || s.bufferCount() > s.config().capacity) ok = false;
            if (s.inCSCount() > 1) ok = false;
        }
        CHECK(producerBlockedFull);
        CHECK(ok);
        SyncStats st = s.stats();
        CHECK(st.completed && st.overflows == 0 && st.underflows == 0);
        CHECK(st.produced == 6 && st.consumed == 6 && st.actual == st.expected);
    }
    // Scenario 1: fast consumer -> consumer blocks on an empty buffer.
    {
        SyncSim s(sc[1].cfg);
        bool consumerBlockedEmpty = false;
        while (!s.finished()) {
            s.stepAuto();
            if (s.procs()[1].state == SyncProcState::Blocked && s.bufferCount() == 0) consumerBlockedEmpty = true;
        }
        CHECK(consumerBlockedEmpty);
        CHECK(s.stats().completed && s.stats().consumed == 6);
    }
    // Many producers / consumers, many seeds: never overflow / underflow, never stuck.
    for (unsigned seed = 1; seed <= 40; ++seed) {
        SyncConfig c = sc[2].cfg;
        c.seed = seed;
        c.capacity = 1 + seed % 3;
        SyncSim s(c);
        s.runToEnd();
        SyncStats st = s.stats();
        CHECK(st.completed && !st.stuck);
        CHECK(st.overflows == 0 && st.underflows == 0 && st.csViolations == 0);
        CHECK(st.consumed == c.producers * c.iterations);
    }
}

// Monitor specifics: condition wait releases the monitor; signal moves the waiter to the entry queue.
static void testMonitor() {
    SyncConfig c;
    c.algo = SyncAlgo::MONITOR;
    c.producers = 1; c.consumers = 1; c.capacity = 1; c.iterations = 2;
    SyncSim s(c);
    const int mon = 0, notFull = 1, notEmpty = 2;
    // Consumer (pid 1) enters first and finds the buffer empty.
    s.step(1);                                   // enter monitor
    CHECK(s.objects()[mon].owner == 1);
    s.step(1);                                   // while (count == 0) -> true
    s.step(1);                                   // notEmpty.wait()
    CHECK(s.procs()[1].state == SyncProcState::Blocked && s.procs()[1].waitingOn == notEmpty);
    CHECK(s.objects()[mon].owner == -1);         // wait() released the monitor
    CHECK(!s.procs()[1].inCS);
    // Producer makes an item and signals.
    for (int i = 0; i < 5; ++i) s.step(0);       // produce, enter, while(false), insert, signal
    CHECK(s.objects()[notEmpty].queue.empty());
    CHECK(s.objects()[mon].queue.size() == 1 && s.procs()[1].waitingOn == mon);   // in the entry queue
    CHECK(s.procs()[1].state == SyncProcState::Blocked);                          // still needs the lock
    s.step(0);                                   // leave monitor -> hand-off to the consumer
    CHECK(s.objects()[mon].owner == 1 && s.procs()[1].inCS);
    CHECK(s.procs()[1].pc == 1);                 // re-checks the while condition (Mesa)
    s.runToEnd();
    CHECK(s.stats().completed && s.stats().consumed == 2);
    CHECK(s.objects()[notFull].queue.empty() && s.objects()[mon].owner == -1);
}

// Comparison: same workload under the four mechanisms.
static void testComparison() {
    std::vector<SyncComparisonRow> r = compareSync(3, 1, false, 1);
    CHECK(r.size() == 4);
    CHECK(r[0].algo == SyncAlgo::RACE && r[0].stats.lostUpdates > 0 && r[0].stats.csViolations > 0);
    for (int i = 1; i < 4; ++i) {
        CHECK(r[i].stats.completed);
        CHECK(r[i].stats.lostUpdates == 0 && r[i].stats.csViolations == 0);
        CHECK(r[i].stats.actual == 6);
    }
    CHECK(r[1].stats.busyWaitSteps > 0);         // Peterson spins
    CHECK(r[2].stats.blockedSteps > 0 && r[2].stats.busyWaitSteps == 0);   // mutex sleeps
    for (const SyncComparisonRow& row : r) CHECK(!row.guarantee.empty());
}

// Validation, determinism and repeated execution ("reset" = build a new SyncSim).
static void testValidationAndReset() {
    SyncConfig bad = cfgOf(SyncAlgo::MUTEX, 0, 1);
    CHECK_THROWS(SyncSim{bad}, std::invalid_argument);
    bad = cfgOf(SyncAlgo::RACE, 2, 0);
    CHECK_THROWS(SyncSim{bad}, std::invalid_argument);
    SyncConfig pc;
    pc.algo = SyncAlgo::PRODUCER_CONSUMER;
    pc.capacity = 0;
    CHECK_THROWS(SyncSim{pc}, std::invalid_argument);
    pc.capacity = 2; pc.producers = 1; pc.consumers = 3; pc.iterations = 1;   // 1 item, 3 consumers
    CHECK_THROWS(SyncSim{pc}, std::invalid_argument);
    SyncConfig sc = cfgOf(SyncAlgo::RACE, 2, 1);
    sc.script = {0, 5};
    CHECK_THROWS(SyncSim{sc}, std::invalid_argument);

    for (int a = 0; a <= (int)SyncAlgo::MONITOR; ++a) {
        for (const SyncScenario& s : syncScenarios((SyncAlgo)a)) {
            SyncSim x(s.cfg), y(s.cfg);
            x.runToEnd();
            for (int i = 0; i < 3; ++i) y.stepAuto();    // partial run, then "reset"
            y = SyncSim(s.cfg);
            y.runToEnd();
            CHECK(x.log() == y.log());
            CHECK(x.stats().completed);
            CHECK(!syncExplanation((SyncAlgo)a).empty());
        }
    }
}

int main() {
    testRace();
    testSynchronizedCounters();
    testPetersonExhaustive();
    testMutex();
    testSemaphores();
    testBuffer(SyncAlgo::PRODUCER_CONSUMER);
    testBuffer(SyncAlgo::MONITOR);
    testMonitor();
    testComparison();
    testValidationAndReset();
    return report("test_sync");
}
