// test_sync_classic.cpp - Phase 3 gap fill in the Synchronization model (core/sync.h):
// readers-writers (first problem) and dining philosophers (naive deadlock, asymmetric solution).
// test_sync.cpp is unchanged; these classical problems are tested here.
#include <cstdio>
#include "check.h"
#include "sync.h"

static void testReadersWriters() {
    std::vector<SyncScenario> sc = syncScenarios(SyncAlgo::READERS_WRITERS);
    CHECK(sc.size() == 3);
    // scenario 0: two readers read together, the writer blocks until the last reader leaves
    SyncSim s(sc[0].cfg);
    bool readersTogether = false, writerWaitsWhileReading = false;
    for (int i = 0; i < 2000 && !s.finished(); ++i) {
        s.stepAuto();
        int readersIn = 0;
        bool writerBlocked = false;
        for (const SyncProc& p : s.procs()) {
            if (p.inCS && p.program == 0) ++readersIn;
            if (p.program == 1 && p.state == SyncProcState::Blocked) writerBlocked = true;
        }
        if (readersIn >= 2) readersTogether = true;
        if (readersIn >= 1 && writerBlocked) writerWaitsWhileReading = true;
    }
    SyncStats st = s.stats();
    CHECK(readersTogether && writerWaitsWhileReading);
    CHECK(st.completed && st.csViolations == 0 && st.actual == st.expected && st.expected == 4);
    CHECK(st.maxInCS >= 2 && s.csCapacity() == 3);
    // any interleaving: a writer is never inside with anybody else
    for (unsigned seed = 1; seed <= 150; ++seed) {
        SyncConfig c = sc[2].cfg;
        c.seed = seed;
        SyncSim r(c);
        r.runToEnd();
        SyncStats rs = r.stats();
        CHECK(rs.completed && rs.csViolations == 0 && rs.actual == rs.expected);
    }
    SyncSim starve(sc[1].cfg);
    starve.runToEnd();
    CHECK(starve.stats().completed && starve.stats().csViolations == 0);
    SyncConfig bad = sc[0].cfg;
    bad.readers = 0;
    CHECK_THROWS(SyncSim x(bad), std::invalid_argument);
    bad.readers = 6; bad.writers = 4;
    CHECK_THROWS(SyncSim y(bad), std::invalid_argument);
}

static void testPhilosophers() {
    std::vector<SyncScenario> sc = syncScenarios(SyncAlgo::DINING_PHILOSOPHERS);
    CHECK(sc.size() == 3);
    // naive: everybody holds the left chopstick and waits for the right one -> stuck (deadlock)
    SyncSim d(sc[0].cfg);
    d.runToEnd();
    SyncStats ds = d.stats();
    CHECK(ds.stuck && !ds.completed && ds.actual == 0);
    for (const SyncObject& o : d.objects()) CHECK(o.value == 0 && o.queue.size() == 1);   // each chopstick held, one waiter
    for (const SyncProc& p : d.procs()) CHECK(p.state == SyncProcState::Blocked);
    // asymmetric, same start: no deadlock, everybody eats
    SyncSim a(sc[1].cfg);
    a.runToEnd();
    SyncStats as = a.stats();
    CHECK(as.completed && !as.stuck && as.actual == 5 && as.csViolations == 0);
    // random interleavings: the asymmetric order never deadlocks and neighbours never eat together
    for (unsigned seed = 1; seed <= 150; ++seed) {
        SyncConfig c = sc[2].cfg;
        c.seed = seed;
        SyncSim r(c);
        r.runToEnd();
        SyncStats rs = r.stats();
        CHECK(rs.completed && rs.csViolations == 0 && rs.maxInCS <= 2);
    }
    // the naive order can deadlock but never lets neighbours eat together
    int deadlocks = 0;
    for (unsigned seed = 1; seed <= 150; ++seed) {
        SyncConfig c = sc[2].cfg;
        c.asymmetric = false;
        c.seed = seed;
        SyncSim r(c);
        r.runToEnd();
        CHECK(r.stats().csViolations == 0);
        if (r.stats().stuck) ++deadlocks;
    }
    std::printf("naive philosophers: %d of 150 random runs deadlocked\n", deadlocks);
    SyncConfig two = sc[1].cfg;
    two.philosophers = 2; two.script.clear();
    SyncSim t(two);
    t.runToEnd();
    CHECK(t.stats().completed);
    SyncConfig bad = sc[0].cfg;
    bad.philosophers = 7;
    CHECK_THROWS(SyncSim x(bad), std::invalid_argument);
    CHECK(!syncExplanation(SyncAlgo::DINING_PHILOSOPHERS).empty() && !syncGuarantee(SyncAlgo::READERS_WRITERS).empty());
}

int main() {
    testReadersWriters();
    testPhilosophers();
    return report("test_sync_classic");
}
