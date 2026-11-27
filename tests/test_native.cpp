// test_native.cpp - Live Linux demos (apps/ossim_helper): every demo exits normally, reports
// the real system-call results, reaps its children and removes its IPC objects / files.
// These tests check facts that do not depend on timing (PIDs, statuses, payloads, cleanup).
#include <cstdio>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "check.h"

struct Ev { std::string role, event, detail; int pid = 0, ppid = 0; };

// Runs "ossim_helper <args>" and parses its event lines; returns the exit status.
static int runHelper(const std::string& args, std::vector<Ev>& out) {
    out.clear();
    FILE* f = popen((std::string(OSSIM_HELPER) + " " + args).c_str(), "r");
    if (!f) return -1;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        if (!l.empty() && l.back() == '\n') l.pop_back();
        size_t bar = l.find(" | ");
        if (bar == std::string::npos) continue;
        Ev e;
        e.detail = l.substr(bar + 3);
        char role[64] = "", ev[64] = "";
        long t;
        if (std::sscanf(l.c_str(), "T=%ld ROLE=%63s PID=%d PPID=%d EVENT=%63s", &t, role, &e.pid, &e.ppid, ev) == 5) {
            e.role = role;
            e.event = ev;
            out.push_back(e);
        }
    }
    int st = pclose(f);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static const Ev* find(const std::vector<Ev>& v, const std::string& event, const std::string& role = "") {
    for (const Ev& e : v) if (e.event == event && (role.empty() || e.role == role)) return &e;
    return nullptr;
}
static bool has(const std::vector<Ev>& v, const std::string& event, const std::string& text) {
    for (const Ev& e : v) if (e.event == event && e.detail.find(text) != std::string::npos) return true;
    return false;
}
static bool noErrors(const std::vector<Ev>& v) {
    for (const Ev& e : v) if (e.event == "error" || e.event == "timeout") { std::printf("  helper: %s %s\n", e.event.c_str(), e.detail.c_str()); return false; }
    return true;
}

int main() {
    std::vector<Ev> v;
    for (int round = 0; round < 2; ++round) {          // repeated runs must behave the same
        // fork: different PIDs, fork() returns 0 in the child and the child's PID in the parent.
        CHECK(runHelper("fork", v) == 0 && noErrors(v));
        const Ev* p = find(v, "fork-returned", "parent");
        const Ev* c = find(v, "fork-returned", "child");
        CHECK(p && c && p->pid != c->pid && c->ppid == p->pid);
        CHECK(p && c && p->detail.find("returned " + std::to_string(c->pid)) != std::string::npos);
        CHECK(c && c->detail.find("returned 0") != std::string::npos);
        CHECK(has(v, "reaped", "exited with status 7"));

        // exec: same PID before and after exec, status of the new image.
        CHECK(runHelper("exec", v) == 0 && noErrors(v));
        const Ev* before = find(v, "before-exec");
        const Ev* after = find(v, "exec-image");
        CHECK(before && after && before->pid == after->pid);
        CHECK(has(v, "reaped", "exited with status 3"));

        // wait: three children, all reaped with statuses 1, 2, 3.
        CHECK(runHelper("wait", v) == 0 && noErrors(v));
        CHECK(has(v, "reaped", "status 1") && has(v, "reaped", "status 2") && has(v, "reaped", "status 3"));

        // zombie: observed as Z before wait, gone after.
        CHECK(runHelper("zombie", v) == 0 && noErrors(v));
        CHECK(find(v, "zombie-observed") != nullptr);
        CHECK(has(v, "reaped", "status 5") && has(v, "zombie-gone", "no longer exists"));

        // orphan: reparented to the subreaper (this helper) and reaped by it.
        CHECK(runHelper("orphan", v) == 0 && noErrors(v));
        const Ev* helper = find(v, "subreaper");
        const Ev* rep = find(v, "reparented");
        CHECK(helper && rep && rep->ppid == helper->pid);
        CHECK(has(v, "reaped-orphan", "status 9"));

        // signal: delivered to our own child, handler ran.
        CHECK(runHelper("signal", v) == 0 && noErrors(v));
        CHECK(find(v, "signal-received") != nullptr && has(v, "reaped", "status 0"));

        // shared memory: payload preserved, segment removed.
        CHECK(runHelper("shm", v) == 0 && noErrors(v));
        CHECK(has(v, "read", "\"HELLO\"") && find(v, "removed") != nullptr);
        // message queue: non-blocking failure on empty queue, FIFO order, removal.
        CHECK(runHelper("msg", v) == 0 && noErrors(v));
        CHECK(has(v, "nonblocking-fail", "ENOMSG"));
        std::vector<std::string> got;
        for (const Ev& e : v) if (e.event == "msgrcv") got.push_back(e.detail);
        CHECK(got.size() == 2 && got[0].find("first") != std::string::npos && got[1].find("second") != std::string::npos);
        CHECK(find(v, "removed") != nullptr);
        // anonymous pipe: exact bytes, then EOF.
        CHECK(runHelper("pipe", v) == 0 && noErrors(v));
        CHECK(has(v, "done", "\"hello through a pipe\" (20 bytes)") && find(v, "eof") != nullptr);
        // named pipe: separately launched writer, data received, FIFO removed.
        CHECK(runHelper("fifo", v) == 0 && noErrors(v));
        CHECK(has(v, "read", "message over a named pipe") && find(v, "removed") != nullptr);
        // threads: arguments and results.
        CHECK(runHelper("threads 4 10", v) == 0 && noErrors(v));
        int joined = 0;
        for (const Ev& e : v) if (e.event == "joined" && e.detail.find("(correct)") != std::string::npos) ++joined;
        CHECK(joined == 4);
        CHECK(has(v, "joined", "result 50005000"));          // thread 0: sum(1..10000)
    }
    CHECK(runHelper("threads 0 10", v) == 2);                // invalid arguments rejected
    CHECK(runHelper("bogus", v) == 2);
    // Phase 3: real file syscalls inside a private temporary directory (removed afterwards)
    CHECK(runHelper("fsdemo", v) == 0 && noErrors(v));
    CHECK(has(v, "fstat", "link count 1") && has(v, "unlink", "link count 0") && has(v, "read", "after unlink = 5 bytes: \"hello\""));
    if (const Ev* m = find(v, "mkdtemp")) {
        std::string dir = m->detail.substr(m->detail.rfind(' ') + 1);
        CHECK(access(dir.c_str(), F_OK) != 0);                       // the temporary directory is gone
    } else CHECK(false);
    // Phase 3: job control on own children - process groups, SIGSTOP / SIGCONT, pipeline, SIGTERM
    CHECK(runHelper("jobs", v) == 0 && noErrors(v));
    CHECK(has(v, "job-stopped", "stopped by signal") && has(v, "job-continued", "continued"));
    CHECK(has(v, "read", "ITEM 4") && has(v, "job-terminated", "killed by signal 15") && find(v, "done"));
    const Ev* prod = find(v, "write", "producer");
    const Ev* cons = find(v, "read", "consumer");
    CHECK(prod && cons && prod->ppid == cons->ppid);
    // Phase 3: bounded monitor worker (System Monitor demo process)
    CHECK(runHelper("worker 1 4 10", v) == 0 && find(v, "started") && find(v, "finished"));
    CHECK(runHelper("worker 999 4 10", v) == 2);                    // limits enforced
    return report("test_native");
}
