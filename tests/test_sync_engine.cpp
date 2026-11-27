// test_sync_engine.cpp - real pthread synchronization demos (engine/sync_demo.*):
// correctness of every primitive, safe cancellation, repeated execution.
// A 30-second alarm() watchdog turns any hang (deadlock) into a test failure.
#include <atomic>
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#include "check.h"
#include "sync_demo.h"

static void onAlarm(int) {
    const char msg[] = "FAIL: watchdog - synchronization demo hung\n";
    (void)!write(2, msg, sizeof msg - 1);
    _exit(1);
}

struct CancelArg { SyncDemoRunner* r; SyncDemoKind k; SyncDemoParams p; SyncDemoResult res; };
static void* runInThread(void* a) {
    auto* c = static_cast<CancelArg*>(a);
    c->res = c->r->run(c->k, c->p);
    return nullptr;
}

int main() {
    signal(SIGALRM, onAlarm);
    alarm(30);
    SyncDemoRunner r;
    int observed = 0;
    r.setObserver([&](const std::string&) { ++observed; });   // runs on the dispatcher (this thread)

    SyncDemoParams p;
    p.threads = 4;
    p.iterations = 20000;

    // Correct primitives: exact counter, never more than one thread inside.
    for (SyncDemoKind k : {SyncDemoKind::MUTEX, SyncDemoKind::BINARY_SEM, SyncDemoKind::PETERSON}) {
        SyncDemoParams q = p;
        if (k == SyncDemoKind::PETERSON) q.iterations = 5000;
        SyncDemoResult res = r.run(k, q);
        std::printf("%s: %s (%.1f ms)\n", syncDemoName(k), res.summary.c_str(), res.millis);
        CHECK(res.ok);
        CHECK(res.actual == res.expected);
        CHECK(res.maxConcurrent == 1);
        CHECK(!res.cancelled);
    }
    CHECK(observed > 0);                         // log lines travelled through the pipe

    // Race demo: well-defined (atomics), never more than expected; lost updates are possible.
    SyncDemoResult race = r.run(SyncDemoKind::RACE, p);
    std::printf("race: %s\n", race.summary.c_str());
    CHECK(race.ok && race.actual <= race.expected && race.actual > 0);

    // Counting semaphore: never more than S threads inside.
    SyncDemoParams cs = p;
    cs.threads = 6; cs.iterations = 5; cs.resources = 2; cs.delayUs = 300;
    SyncDemoResult cres = r.run(SyncDemoKind::COUNTING_SEM, cs);
    std::printf("counting: %s\n", cres.summary.c_str());
    CHECK(cres.ok && cres.maxConcurrent <= 2 && cres.actual == 30);

    // Producer-consumer and monitor: every item consumed once, no overflow / underflow.
    SyncDemoParams pc;
    pc.producers = 3; pc.consumers = 2; pc.capacity = 2; pc.items = 40;
    for (SyncDemoKind k : {SyncDemoKind::PRODUCER_CONSUMER, SyncDemoKind::MONITOR}) {
        SyncDemoResult res = r.run(k, pc);
        std::printf("%s: %s\n", syncDemoName(k), res.summary.c_str());
        CHECK(res.ok);
        CHECK(res.actual == 120 && res.overflows == 0 && res.underflows == 0);
        CHECK(res.maxConcurrent == 1);
    }

    // Cancellation: slow runs are stopped while threads are blocked; run() must return.
    for (SyncDemoKind k : {SyncDemoKind::PRODUCER_CONSUMER, SyncDemoKind::MONITOR, SyncDemoKind::COUNTING_SEM,
                           SyncDemoKind::BINARY_SEM, SyncDemoKind::PETERSON, SyncDemoKind::MUTEX}) {
        for (int round = 0; round < 2; ++round) {
            SyncDemoRunner rc;
            CancelArg a{&rc, k, SyncDemoParams{}, {}};
            a.p.threads = 4; a.p.iterations = 1000; a.p.resources = 1;
            a.p.producers = 1; a.p.consumers = 3; a.p.capacity = 1; a.p.items = 200;
            a.p.delayUs = 5000;                  // slow: threads will be waiting when we cancel
            pthread_t t;
            pthread_create(&t, nullptr, runInThread, &a);
            usleep(60000);
            rc.cancel();
            pthread_join(t, nullptr);
            CHECK(a.res.cancelled && !a.res.ok);
            CHECK(a.res.overflows == 0 && a.res.underflows == 0);
            CHECK(a.res.maxConcurrent <= a.res.limit);
            // The same runner works again after a cancel ("reset").
            SyncDemoParams small;
            small.threads = 2; small.iterations = 50; small.producers = 1; small.consumers = 1; small.items = 5;
            SyncDemoResult again = rc.run(k, small);
            CHECK(again.ok && !again.cancelled);
        }
    }

    // Repeated execution: 10 runs, the checks hold every time.
    bool allOk = true;
    for (int i = 0; i < 10; ++i) {
        SyncDemoParams q;
        q.threads = 3; q.iterations = 2000; q.producers = 2; q.consumers = 2; q.capacity = 3; q.items = 15;
        for (SyncDemoKind k : {SyncDemoKind::MUTEX, SyncDemoKind::PRODUCER_CONSUMER, SyncDemoKind::MONITOR})
            allOk = allOk && r.run(k, q).ok;
    }
    CHECK(allOk);

    // Validation
    SyncDemoParams bad;
    bad.threads = 0;
    CHECK_THROWS(r.run(SyncDemoKind::MUTEX, bad), std::invalid_argument);
    bad = SyncDemoParams{};
    bad.capacity = 0;
    CHECK_THROWS(r.run(SyncDemoKind::PRODUCER_CONSUMER, bad), std::invalid_argument);
    alarm(0);
    return report("test_sync_engine");
}
