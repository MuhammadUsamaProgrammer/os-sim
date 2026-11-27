// test_jobctl.cpp - Job Control model (core/jobctl.h): job id vs PID/PGID, one foreground group,
// stop / continue transitions, group completion, pipeline data order (IPC pipe model), invalid
// references, the required background -> stop -> resume -> fg -> complete preset.
#include <cstdio>
#include "check.h"
#include "jobctl.h"

static const JcProc* procOf(const JobShell& sh, int pid) {
    for (const JcProc& p : sh.procs()) if (p.pid == pid) return &p;
    return nullptr;
}
static bool ok(const JobShell& sh) {
    std::vector<std::string> v = sh.checkInvariants();
    for (const std::string& s : v) std::printf("  invariant: %s\n", s.c_str());
    return v.empty();
}

static void testRequiredPreset() {
    JobShell sh;
    CHECK(sh.exec("compute 8 &").ok);
    const JcJob* j = sh.job(1);
    CHECK(j && j->id == 1 && j->pgid == 101 && j->pids.size() == 1 && j->pids[0] == 101);   // job id != pid
    CHECK(sh.shellHasTerminal());                                                           // background: shell keeps the tty
    sh.exec("tick 2");
    CHECK(procOf(sh, 101)->remaining == 6);
    CHECK(sh.exec("kill -STOP %1").ok && sh.job(1)->state == JcJobState::Stopped && procOf(sh, 101)->state == JcState::Stopped);
    sh.exec("tick 2");
    CHECK(procOf(sh, 101)->remaining == 6);                                                 // no progress while stopped
    CHECK(sh.exec("bg %1").ok && sh.job(1)->state == JcJobState::Running && sh.shellHasTerminal());
    sh.exec("tick 2");
    CHECK(procOf(sh, 101)->remaining == 4);                                                 // background job runs: not blocked
    CHECK(sh.exec("fg %1").ok && sh.fgPgid() == 101 && !sh.shellHasTerminal());
    CHECK(!sh.exec("jobs").ok);                                                             // the shell waits for the fg job
    CHECK(sh.exec("until-prompt").ok);
    CHECK(sh.job(1)->state == JcJobState::Done && sh.shellHasTerminal() && procOf(sh, 101)->exitCode == 0);
    CHECK(ok(sh));
    // the scenario list contains the same preset and replays identically
    JobShell r = runJobScript(jobScenarios()[0].commands);
    CHECK(r.job(1)->state == JcJobState::Done && r.shellHasTerminal() && ok(r));
}

static void testForegroundSignals() {
    JobShell sh;
    sh.exec("compute 6");
    CHECK(sh.fgPgid() == 101);
    sh.exec("tick 2");
    CHECK(sh.exec("ctrl-z").ok && sh.job(1)->state == JcJobState::Stopped && sh.shellHasTerminal());
    CHECK(!sh.exec("ctrl-z").ok);                                                           // nothing in the foreground now
    CHECK(sh.exec("bg").ok && sh.job(1)->state == JcJobState::Running);                     // %% = current job
    CHECK(!sh.exec("bg %1").ok);                                                            // already running
    sh.exec("fg");
    CHECK(sh.exec("ctrl-c").ok && sh.job(1)->state == JcJobState::Terminated && procOf(sh, 101)->signal == 2 && sh.shellHasTerminal());
    // at most one foreground group, always the shell or a running job
    for (const JcSnapshot& s : sh.snapshots()) {
        bool good = s.fgPgid == JobShell::kShellPid;
        for (const JcJob& j : s.jobs) if (j.pgid == s.fgPgid && j.state == JcJobState::Running) good = true;
        CHECK(good);
    }
    CHECK(ok(sh));
}

static void testBackgroundAndTty() {
    JobShell sh = runJobScript(jobScenarios()[2].commands);
    CHECK(sh.job(1)->state == JcJobState::Done && sh.job(2)->state == JcJobState::Done);
    CHECK(sh.shellHasTerminal());
    JobShell t;
    t.exec("readline &");
    t.exec("tick 1");
    CHECK(t.job(1)->state == JcJobState::Stopped);                                         // SIGTTIN
    bool ttin = false;
    for (const JcEvent& e : t.snapshots().back().events) if (e.text.find("SIGTTIN") != std::string::npos) ttin = true;
    CHECK(ttin);
    CHECK(!t.exec("type x").ok);                                                            // shell in the foreground
    t.exec("fg %1");
    CHECK(t.exec("until-prompt").ok == false);                                              // waits for input, does not spin
    CHECK(t.exec("type hello").ok);
    t.exec("tick 1");
    CHECK(t.job(1)->state == JcJobState::Done && t.shellHasTerminal());
    bool got = false;
    for (const std::string& l : t.snapshots().back().transcript) if (l == "readline: got \"hello\"") got = true;
    CHECK(got);
}

static void testPipeline() {
    JobShell sh;
    CHECK(sh.exec("gen 6 | upper").ok);
    const JcJob* j = sh.job(1);
    CHECK(j->pids.size() == 2 && procOf(sh, 101)->pgid == 101 && procOf(sh, 102)->pgid == 101);   // one group, two processes
    CHECK(!sh.exec("gen 2 | upper").ok);                                                    // shell busy (fg job)
    sh.exec("tick 3");
    CHECK(sh.exec("ctrl-z").ok && procOf(sh, 101)->state == JcState::Stopped && procOf(sh, 102)->state == JcState::Stopped);
    int before = procOf(sh, 102)->consumed;
    sh.exec("tick 3");
    CHECK(procOf(sh, 102)->consumed == before);                                             // stopped group: no data flows
    CHECK(!sh.exec("gen 2 | upper &").ok);                                                  // one pipeline at a time
    sh.exec("fg");
    CHECK(sh.exec("until-prompt").ok && sh.job(1)->state == JcJobState::Done);
    std::string order;
    for (const std::string& l : sh.snapshots().back().transcript) if (l.rfind("upper: ", 0) == 0) order += l.substr(7);
    CHECK(order == "ABCDEF");                                                               // FIFO byte order through the pipe
    CHECK(procOf(sh, 101)->produced == 6 && procOf(sh, 102)->consumed == 6);
    bool eof = false;
    for (const JcEvent& e : sh.snapshots().back().events) if (e.text.find("EOF") != std::string::npos) eof = true;
    CHECK(eof);
    // the pipe buffer never exceeds its capacity
    for (const JcSnapshot& s : sh.snapshots()) CHECK((int)s.pipeBuffer.size() <= std::max(3, s.pipeCapacity));
    // a slower reader makes the writer block on a full pipe
    bool blocked = false;
    JobShell b;
    b.exec("gen 9 | upper &");
    b.exec("kill -STOP %1");
    b.exec("bg %1");
    for (int i = 0; i < 8; ++i) {
        b.exec("tick 1");
        for (const JcProc& p : b.procs()) if (p.waitingOn.find("full") != std::string::npos) blocked = true;
    }
    CHECK(blocked);                                                                         // writer slept on a full pipe
    for (int i = 0; i < 20 && b.job(1)->state != JcJobState::Done; ++i) b.exec("tick 1");
    CHECK(b.job(1)->state == JcJobState::Done);
    CHECK(ok(sh) && ok(b));
}

static void testInvalid() {
    std::vector<JcResult> res;
    JobShell sh = runJobScript(jobScenarios()[6].commands, &res);
    CHECK(res.size() == 6);
    for (int i = 0; i < 5; ++i) CHECK(!res[i].ok);
    CHECK(res[2].reply.find("bare PIDs are refused") != std::string::npos);
    CHECK(res[3].reply.find("never executed") != std::string::npos);
    CHECK(sh.jobs().empty() && sh.procs().empty());
    JobShell k = runJobScript(jobScenarios()[5].commands, &res);
    CHECK(k.job(1)->state == JcJobState::Terminated && k.job(2)->state == JcJobState::Terminated && k.job(3)->state == JcJobState::Done);
    CHECK(!k.exec("fg %2").ok);                                                             // terminated jobs cannot come back
    JobShell w;
    w.exec("compute 5 &");
    w.exec("kill -STOP %1");
    CHECK(!w.exec("wait").ok);                                                              // would block forever
    CHECK(w.exec("kill -KILL %1").ok && w.job(1)->state == JcJobState::Terminated && procOf(w, 101)->signal == 9);
    CHECK(!w.exec("kill -HUP %1").ok);
    // cleanup: every process of every scenario ends (no leftovers after until-prompt / wait / kill)
    for (const JcScenario& s : jobScenarios()) {
        JobShell x = runJobScript(s.commands);
        CHECK(ok(x));
    }
}

int main() {
    testRequiredPreset();
    testForegroundSignals();
    testBackgroundAndTty();
    testPipeline();
    testInvalid();
    return report("test_jobctl");
}
