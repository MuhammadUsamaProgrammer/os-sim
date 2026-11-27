// test_sysmon.cpp - System Monitor arithmetic with synthetic fixtures (no live values asserted):
// parsers, deltas, zero intervals, counter resets, disappearing processes, PID reuse, unsupported
// fields, alert debounce, bounded history; plus a native availability smoke test.
#include <cstdio>
#include <unistd.h>
#include "check.h"
#include "sysmon.h"
#include "sysmon_native.h"

static const char* kStat1 =
    "cpu  100 0 100 800 0 0 0 0 0 0\n"
    "cpu0 50 0 50 400 0 0 0 0 0 0\n"
    "cpu1 50 0 50 400 0 0 0 0 0 0\n"
    "intr 12345\nctxt 999\n";
static const char* kStat2 =                     // +100 busy, +100 idle overall; cpu0 fully busy, cpu1 idle
    "cpu  150 0 150 900 0 0 0 0 0 0\n"
    "cpu0 100 0 100 400 0 0 0 0 0 0\n"
    "cpu1 50 0 50 500 0 0 0 0 0 0\n";
static const char* kMem = "MemTotal:       8000000 kB\nMemFree:         1000000 kB\nMemAvailable:    3000000 kB\nBuffers: 1 kB\n";
static const char* kMemOld = "MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 150 kB\n";

static RawSample sample(double t, const char* stat, std::vector<ProcRaw> procs, unsigned long long rd = 0, unsigned long long wr = 0) {
    RawSample s;
    s.timeSec = t;
    s.cpuOk = parseProcStat(stat, s.cpu);
    s.memOk = parseMeminfo(kMem, s.memTotalKb, s.memAvailableKb);
    s.procsOk = true;
    s.procs = procs;
    s.diskOk = true;
    s.diskReadBytes = rd;
    s.diskWrittenBytes = wr;
    s.ticksPerSec = 100;
    s.logicalCpus = 2;
    return s;
}
static ProcRaw proc(int pid, unsigned long long ticks, unsigned long long start = 10) {
    ProcRaw p;
    p.pid = pid; p.ppid = 1; p.name = "p" + std::to_string(pid); p.state = 'S'; p.cpuTicks = ticks; p.startTime = start; p.rssKb = 1000; p.threads = 1;
    return p;
}

static void testParsers() {
    CpuSample c;
    CHECK(parseProcStat(kStat1, c) && c.cores.size() == 2 && c.total.total() == 1000 && c.total.idleAll() == 800);
    CHECK(!parseProcStat("garbage\n", c));
    CHECK(parseProcStat("cpu 1 2 3 4\n", c) && c.total.iowait == 0);          // short line: missing fields = 0
    long long t, a;
    CHECK(parseMeminfo(kMem, t, a) && t == 8000000 && a == 3000000);
    CHECK(parseMeminfo(kMemOld, t, a) && a == 300);                           // no MemAvailable: estimate
    CHECK(!parseMeminfo("MemTotal: 0 kB\n", t, a));
    ProcRaw p;
    std::string st = "4242 (weird) name) S 1 4242 4242 0 -1 4194304 100 0 0 0 7 3 0 0 20 0 5 0 777 1000000 250 18446744073709551615";
    CHECK(parsePidStat(st, p, 4) && p.pid == 4242 && p.name == "weird) name" && p.state == 'S' && p.ppid == 1);
    CHECK(p.cpuTicks == 10 && p.threads == 5 && p.startTime == 777 && p.rssKb == 1000);
    CHECK(!parsePidStat("12 (truncated) S 1", p, 4));
    CHECK(!parsePidStat("no parenthesis", p, 4));
    ProcRaw k;                                                               // kernel thread: no VmRSS line
    k.rssKb = 0;
    parsePidStatus("Name:\tkworker/0:1\nState:\tI (idle)\nPPid:\t2\nUid:\t0\t0\t0\t0\nThreads:\t1\n", k);
    CHECK(k.name == "kworker/0:1" && k.state == 'I' && k.ppid == 2 && k.uid == 0 && k.threads == 1 && k.rssKb == 0);
    parsePidStatus("VmRSS:\t  2048 kB\nThreads:\tbogus\n", k);
    CHECK(k.rssKb == 2048 && k.threads == 1);                                // unparsable field ignored
    unsigned long long r, w;
    const char* ds = "   8       0 sda 100 0 2000 0 50 0 4000 0 0 0 0\n"
                     "   8       1 sda1 100 0 2000 0 50 0 4000 0 0 0 0\n"
                     " 259       0 nvme0n1 1 0 10 0 1 0 20 0 0 0 0\n"
                     " 259       1 nvme0n1p1 1 0 10 0 1 0 20 0 0 0 0\n"
                     "   7       0 loop0 5 0 500 0 0 0 0 0 0 0 0\n";
    CHECK(parseDiskstats(ds, r, w) && r == (2000 + 10) * 512ULL && w == (4000 + 20) * 512ULL);   // partitions and loop skipped
    CHECK(!parseDiskstats("", r, w));
}

static void testDeltas() {
    CpuSample a, b;
    parseProcStat(kStat1, a);
    parseProcStat(kStat2, b);
    bool v;
    CHECK_NEAR(cpuBusyPercent(a.total, b.total, v), 50.0);
    CHECK(v);
    CHECK_NEAR(cpuBusyPercent(a.cores[0], b.cores[0], v), 100.0);
    CHECK_NEAR(cpuBusyPercent(a.cores[1], b.cores[1], v), 0.0);
    cpuBusyPercent(a.total, a.total, v);
    CHECK(!v);                                                               // zero interval
    cpuBusyPercent(b.total, a.total, v);
    CHECK(!v);                                                               // counter reset
    CHECK_NEAR(procCpuPercent(100, 150, 1.0, 100, v), 50.0);
    CHECK_NEAR(procCpuPercent(100, 400, 1.0, 100, v), 300.0);                // 3 busy threads: > 100 % of one core
    procCpuPercent(100, 150, 0.0, 100, v);
    CHECK(!v);
    procCpuPercent(200, 150, 1.0, 100, v);
    CHECK(!v);
    CHECK_NEAR(ratePerSec(1000, 3000, 2.0, v), 1000.0);
    ratePerSec(3000, 1000, 2.0, v);
    CHECK(!v);
}

static void testModel() {
    SysMonModel m;
    SystemSnapshot s1 = m.update(sample(10.0, kStat1, {proc(1, 0), proc(50, 100), proc(60, 10)}, 0, 0));
    CHECK(s1.warmingUp && !s1.cpuValid && s1.memValid && std::fabs(s1.memPercent - 62.5) < 1e-9);
    for (const ProcView& p : s1.procs) CHECK(p.fresh && !p.cpuValid);       // warming up
    // 2 s later: pid 50 used 100 ticks (50 % of a core); pid 60 vanished; pid 70 is new;
    // pid 1 was reused (different start time)
    SystemSnapshot s2 = m.update(sample(12.0, kStat2, {proc(1, 5, 999), proc(50, 200), proc(70, 0)}, 0, 4096));
    CHECK(!s2.warmingUp && s2.cpuValid && std::fabs(s2.cpuPercent - 50.0) < 1e-9 && std::fabs(s2.intervalSec - 2.0) < 1e-9);
    CHECK(s2.corePercent.size() == 2 && std::fabs(s2.corePercent[0] - 100.0) < 1e-9);
    CHECK(s2.diskValid && std::fabs(s2.diskWriteBps - 2048.0) < 1e-9);
    CHECK(s2.processCount == 3);
    for (const ProcView& p : s2.procs) {
        if (p.pid == 50) CHECK(p.cpuValid && std::fabs(p.cpuPercent - 50.0) < 1e-9);
        if (p.pid == 70) CHECK(p.fresh && !p.cpuValid);
        if (p.pid == 1) CHECK(p.fresh && !p.cpuValid);                      // PID reuse is not a delta
    }
    // same timestamp again: zero interval -> nothing computed, a note explains it
    SystemSnapshot s3 = m.update(sample(12.0, kStat2, {proc(50, 220)}));
    CHECK(!s3.cpuValid && !s3.notes.empty());
    // counter reset (e.g. counters restarted): CPU invalid for this sample
    SystemSnapshot s4 = m.update(sample(13.0, kStat1, {proc(50, 230)}));
    CHECK(!s4.cpuValid);
    // reset: warming up again
    m.reset();
    CHECK(m.update(sample(20.0, kStat1, {})).warmingUp);
    // unsupported data: no CPU / memory from the backend
    RawSample none;
    none.timeSec = 1;
    SysMonModel m2;
    m2.update(none);
    none.timeSec = 2;
    SystemSnapshot n = m2.update(none);
    CHECK(!n.cpuValid && !n.memValid && !n.diskValid && n.corePercent.empty());
}

static void testAlertsAndHistory() {
    AlertMonitor a("CPU", 80, 3, 5);
    AlertEvent ev;
    double spike[] = {90, 50, 95, 96, 70};                                   // isolated spikes: no alert
    for (int i = 0; i < 5; ++i) CHECK(!a.feed(i, spike[i], ev));
    CHECK(!a.feed(5, 85, ev) && !a.feed(6, 85, ev));
    CHECK(a.feed(7, 85, ev) && ev.raised && a.active());                     // third consecutive sample
    CHECK(!a.feed(8, 99, ev));                                               // no alert storm while active
    CHECK(!a.feed(9, 78, ev) && !a.feed(10, 70, ev) && !a.feed(11, 70, ev)); // 78 is within the hysteresis
    CHECK(a.feed(12, 70, ev) && !ev.raised && !a.active());                  // cleared
    a.configure(50, 1);
    CHECK(a.feed(13, 60, ev) && ev.raised);                                  // debounce 1 = immediate
    MetricHistory h(3);
    for (int i = 0; i < 10; ++i) h.push(i);
    CHECK(h.values().size() == 3 && h.values().front() == 7 && h.values().back() == 9);
    h.setCapacity(2);
    CHECK(h.values().size() == 2 && h.values().front() == 8);
    CHECK(formatKb(-1) == "n/a" && formatKb(512) == "512 KiB" && formatKb(2048) == "2.0 MiB");
}

static void testNativeSmoke() {
    // Availability only (no assertions on live values): the backend of this platform returns data.
    RawSample s = sampleSystem();
#ifdef __linux__
    CHECK(std::string(sysmonBackendName()) == "Linux /proc");
    CHECK(s.cpuOk && s.memOk && s.procsOk && s.logicalCpus >= 1 && s.ticksPerSec > 0);
    bool self = false;
    for (const ProcRaw& p : s.procs) if (p.pid == (int)getpid()) self = p.name.find("test_sysmon") != std::string::npos;
    CHECK(self);
    SysMonModel m;
    m.update(s);
    usleep(50000);
    SystemSnapshot snap = m.update(sampleSystem());
    CHECK(!snap.warmingUp && snap.intervalSec > 0 && snap.memValid);
#else
    (void)s;
#endif
}

int main() {
    testParsers();
    testDeltas();
    testModel();
    testAlertsAndHistory();
    testNativeSmoke();
    return report("test_sysmon");
}
