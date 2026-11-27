// jobctl.cpp - deterministic shell job-control model (see jobctl.h).
#include "jobctl.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

const char* jcStateName(JcState s) {
    switch (s) {
        case JcState::Running: return "running";
        case JcState::Sleeping: return "sleeping";
        case JcState::Stopped: return "stopped";
        case JcState::Exited: return "exited";
        case JcState::Killed: return "killed";
    }
    return "?";
}
const char* jcJobStateName(JcJobState s) {
    switch (s) {
        case JcJobState::Running: return "Running";
        case JcJobState::Stopped: return "Stopped";
        case JcJobState::Done: return "Done";
        case JcJobState::Terminated: return "Terminated";
    }
    return "?";
}

static bool alive(const JcProc& p) { return p.state != JcState::Exited && p.state != JcState::Killed; }
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

JobShell::JobShell(int pipeCapacity) : pipeCap_(std::max(1, std::min(16, pipeCapacity))) {
    say("OS-Sim educational shell (session " + std::to_string(kShellPid) + ", shell PID " + std::to_string(kShellPid) +
        "). Type 'help' for the commands. Nothing is executed on the host.");
    snapshot("(session start)");
}

const JcJob* JobShell::job(int id) const {
    for (const JcJob& j : jobs_) if (j.id == id) return &j;
    return nullptr;
}
JcProc* JobShell::proc(int pid) {
    for (JcProc& p : procs_) if (p.pid == pid) return &p;
    return nullptr;
}

void JobShell::event(const std::string& kind, const std::string& text, int job, int pid) {
    events_.push_back(JcEvent{tick_, kind, text, job, pid});
}

void JobShell::snapshot(const std::string& label) {
    JcSnapshot s;
    s.tick = tick_;
    s.command = command_;
    s.label = label;
    s.fgPgid = fg_;
    s.procs = procs_;
    s.jobs = jobs_;
    s.transcript = out_;
    s.events = events_;
    s.pendingInput = input_;
    if (pipe_) { s.pipeBuffer = pipe_->trace().back().pipeBuf; s.pipeCapacity = pipeCap_; }
    snaps_.push_back(s);
}

int JobShell::currentJob() const {
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) if (it->state == JcJobState::Stopped) return it->id;
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) if (it->state == JcJobState::Running && it->pgid != fg_) return it->id;
    return 0;
}

int JobShell::resolveJob(const std::string& spec, std::string& err) const {
    int id = 0;
    if (spec.empty() || spec == "%%" || spec == "%+") {
        id = currentJob();
        if (!id) { err = "no current job"; return 0; }
    } else if (spec[0] == '%' && spec.size() > 1 && std::all_of(spec.begin() + 1, spec.end(), ::isdigit)) {
        id = std::stoi(spec.substr(1));
    } else {
        err = "'" + spec + "': use a job id such as %1 - bare PIDs are refused (this shell only signals its own jobs)";
        return 0;
    }
    const JcJob* j = job(id);
    if (!j || j->reported) { err = "%" + std::to_string(id) + ": no such job"; return 0; }
    return id;
}

// Delivers a signal to every process of a process group (the way kill(-pgid, sig) does).
void JobShell::signalGroup(int pgid, const std::string& sig, const std::string& why) {
    int jobId = 0;
    for (const JcJob& j : jobs_) if (j.pgid == pgid) jobId = j.id;
    event("signal", "SIG" + sig + " -> process group " + std::to_string(pgid) + " (" + why + ")", jobId);
    for (JcProc& p : procs_) {
        if (p.pgid != pgid || !alive(p)) continue;
        if (sig == "TSTP" || sig == "STOP" || sig == "TTIN") {
            if (p.state != JcState::Stopped) { p.beforeStop = p.state; p.state = JcState::Stopped; }
        } else if (sig == "CONT") {
            if (p.state == JcState::Stopped) p.state = p.beforeStop;
        } else {                                     // INT, TERM, KILL: default action = terminate
            p.state = JcState::Killed;
            p.signal = sig == "INT" ? 2 : sig == "KILL" ? 9 : 15;
            if (pipe_ && pipeJob_ && p.pgid == job(pipeJob_)->pgid) {          // the kernel closes its pipe ends
                int idx = p.prog == JcProgram::Gen ? 0 : 1;
                const IpcProcState& ps = pipe_->trace().back().procs[idx];
                if (ps.writeOpen) { IpcOp c; c.kind = IpcOpKind::PipeClose; c.proc = idx; c.writeEnd = true; pipe_->apply(c); }
                if (ps.readOpen) { IpcOp c; c.kind = IpcOpKind::PipeClose; c.proc = idx; c.writeEnd = false; pipe_->apply(c); }
            }
        }
    }
    updateJobs();
}

void JobShell::updateJobs() {
    for (JcJob& j : jobs_) {
        if (j.state == JcJobState::Done || j.state == JcJobState::Terminated) continue;
        bool anyAlive = false, anyStopped = false, anyKilled = false;
        for (int pid : j.pids) {
            const JcProc* p = nullptr;
            for (const JcProc& x : procs_) if (x.pid == pid) p = &x;
            if (!p) continue;
            if (alive(*p)) anyAlive = true;
            if (p->state == JcState::Stopped) anyStopped = true;
            if (p->state == JcState::Killed) anyKilled = true;
        }
        JcJobState old = j.state;
        if (!anyAlive) j.state = anyKilled ? JcJobState::Terminated : JcJobState::Done;
        else j.state = anyStopped ? JcJobState::Stopped : JcJobState::Running;
        if (j.state == old) continue;
        std::string tag = "[" + std::to_string(j.id) + "]";
        if (j.state == JcJobState::Stopped) {
            say(tag + "+  Stopped                 " + j.cmdline);
            event("stopped", "job " + tag + " stopped (all processes of group " + std::to_string(j.pgid) + ")", j.id);
            if (fg_ == j.pgid) { fg_ = kShellPid; event("terminal", "the shell takes the terminal back (tcsetpgrp to the shell's group)", j.id); }
        } else if (j.state == JcJobState::Running && old == JcJobState::Stopped) {
            event("continued", "job " + tag + " continued", j.id);
        } else if (j.state == JcJobState::Done || j.state == JcJobState::Terminated) {
            if (pipeJob_ == j.id) pipeJob_ = 0;
            if (fg_ == j.pgid) {
                fg_ = kShellPid;
                if (j.state == JcJobState::Terminated) say("(terminated by a signal)");
                event("terminal", "foreground job " + tag + " finished: the shell has the terminal again", j.id);
            } else say(tag + "-  " + (j.state == JcJobState::Done ? "Done                    " : "Terminated              ") + j.cmdline);
            j.reported = true;                       // announced now (bash prints "Done" at the next prompt)
            event(j.state == JcJobState::Done ? "done" : "terminated", "job " + tag + " " + jcJobStateName(j.state), j.id);
        }
    }
}

// One tick: every runnable process makes one step (time sharing is abstracted - CPU scheduling
// itself is shown in the Scheduling tab).
void JobShell::step() {
    ++tick_;
    for (JcProc& p : procs_) {
        if (!alive(p) || p.state == JcState::Stopped) continue;
        switch (p.prog) {
            case JcProgram::Compute:
                p.state = JcState::Running;
                if (--p.remaining <= 0) { p.state = JcState::Exited; p.exitCode = 0; event("exit", p.cmd + " (PID " + std::to_string(p.pid) + ") exits with status 0", 0, p.pid); }
                break;
            case JcProgram::Sleep:
                p.state = JcState::Sleeping;
                p.waitingOn = "timer";
                if (--p.remaining <= 0) { p.state = JcState::Exited; p.exitCode = 0; event("exit", p.cmd + " (PID " + std::to_string(p.pid) + ") woke up and exits", 0, p.pid); }
                break;
            case JcProgram::ReadLine:
                if (p.pgid != fg_) {
                    event("tty", "PID " + std::to_string(p.pid) + " tried to read the terminal from the background: the kernel sends SIGTTIN", 0, p.pid);
                    signalGroup(p.pgid, "TTIN", "background read from the terminal");
                } else if (!input_.empty()) {
                    say("readline: got \"" + input_ + "\"");
                    event("read", "PID " + std::to_string(p.pid) + " read \"" + input_ + "\" from the terminal", 0, p.pid);
                    input_.clear();
                    p.state = JcState::Exited;
                    p.exitCode = 0;
                } else { p.state = JcState::Sleeping; p.waitingOn = "terminal input (use: type <text>)"; }
                break;
            case JcProgram::Gen:
            case JcProgram::Upper: {
                if (!pipe_) break;
                int idx = p.prog == JcProgram::Gen ? 0 : 1;
                if (pipe_->trace().back().procs[idx].blocked) break;     // still waiting inside write()/read()
                if (p.prog == JcProgram::Gen) {
                    if (p.remaining > 0) {
                        IpcOp w; w.kind = IpcOpKind::PipeWrite; w.proc = 0; w.data = std::string(1, (char)('a' + p.produced % 26));
                        pipe_->apply(w);
                        ++p.produced;
                        --p.remaining;
                        event("pipe", "gen writes item '" + w.data + "' into the pipe", 0, p.pid);
                    } else {
                        IpcOp c; c.kind = IpcOpKind::PipeClose; c.proc = 0; c.writeEnd = true;
                        pipe_->apply(c);
                        p.state = JcState::Exited;
                        p.exitCode = 0;
                        event("exit", "gen closed the write end and exits", 0, p.pid);
                    }
                } else {
                    if (tick_ % 2 != 0) break;               // upper is slower: it handles one item every 2 ticks
                    IpcOp r; r.kind = IpcOpKind::PipeRead; r.proc = 1; r.length = 1;
                    pipe_->apply(r);
                    const IpcProcState& ps = pipe_->trace().back().procs[1];
                    if (!ps.blocked && ps.lastData.empty()) {
                        p.state = JcState::Exited;
                        p.exitCode = 0;
                        event("exit", "upper read EOF (every write end closed) and exits", 0, p.pid);
                    }
                }
                break;
            }
        }
        // data that reached upper (directly or through a wake-up of its blocked read)
        if (pipe_) {
            for (JcProc& u : procs_) {
                if (u.prog != JcProgram::Upper || !pipeJob_ || u.pgid != job(pipeJob_)->pgid) continue;
                const IpcProcState& ps = pipe_->trace().back().procs[1];
                while (u.consumed < ps.bytesIn) {
                    char c = ps.lastData.empty() ? '?' : ps.lastData[0];
                    ++u.consumed;
                    say(std::string("upper: ") + (char)std::toupper((unsigned char)c));
                    event("pipe", std::string("upper read '") + c + "' and printed '" + (char)std::toupper((unsigned char)c) + "'", 0, u.pid);
                }
            }
        }
    }
    // pipeline processes sleep while their pipe operation is blocked
    if (pipe_ && pipeJob_)
        for (JcProc& p : procs_) {
            if ((p.prog != JcProgram::Gen && p.prog != JcProgram::Upper) || !alive(p) || p.state == JcState::Stopped || p.pgid != job(pipeJob_)->pgid) continue;
            const IpcProcState& ps = pipe_->trace().back().procs[p.prog == JcProgram::Gen ? 0 : 1];
            p.state = ps.blocked ? JcState::Sleeping : JcState::Running;
            p.waitingOn = ps.blocked ? (p.prog == JcProgram::Gen ? "pipe full (write blocks)" : "pipe empty (read blocks)") : "";
        }
    updateJobs();
}

JcResult JobShell::runProgram(const std::string& line, bool background) {
    std::istringstream in(line);
    std::vector<std::string> t;
    std::string w;
    while (in >> w) t.push_back(w);
    auto number = [](const std::string& s, int& out) {
        if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit) || s.size() > 3) return false;
        out = std::stoi(s);
        return out >= 1 && out <= 50;
    };
    std::vector<JcProc> ps;
    int n = 0;
    JcProc base;
    if (t.size() == 2 && (t[0] == "compute" || t[0] == "sleep") && number(t[1], n)) {
        base.prog = t[0] == "compute" ? JcProgram::Compute : JcProgram::Sleep;
        base.remaining = n;
        base.cmd = t[0] + " " + t[1];
        base.state = base.prog == JcProgram::Sleep ? JcState::Sleeping : JcState::Running;
        ps.push_back(base);
    } else if (t.size() == 1 && t[0] == "readline") {
        base.prog = JcProgram::ReadLine;
        base.cmd = "readline";
        ps.push_back(base);
    } else if (t.size() == 4 && t[0] == "gen" && number(t[1], n) && t[2] == "|" && t[3] == "upper") {
        if (pipeJob_) return {false, "only one pipeline at a time in this model (job %" + std::to_string(pipeJob_) + " is still alive)"};
        JcProc g = base, u = base;
        g.prog = JcProgram::Gen; g.remaining = n; g.cmd = "gen " + t[1];
        u.prog = JcProgram::Upper; u.cmd = "upper";
        ps = {g, u};
    } else {
        return {false, "unknown command '" + line + "'. Programs: compute N, sleep N, readline, gen N | upper (N = 1..50), optional '&'. "
                       "Arbitrary commands are never executed."};
    }
    JcJob j;
    j.id = nextJob_++;
    j.cmdline = line + (background ? " &" : "");
    j.startedInBackground = background;
    for (JcProc& p : ps) {                      // fork(): new PIDs; setpgid(): one group per job (pgid = first PID)
        p.pid = nextPid_++;
        p.ppid = kShellPid;
        p.sid = kShellPid;
        j.pids.push_back(p.pid);
    }
    for (JcProc& p : ps) p.pgid = ps.front().pid;
    j.pgid = ps.front().pid;
    if (ps.size() == 2) {
        IpcConfig c;
        c.mode = IpcMode::Pipe;
        c.procs = {"gen", "upper"};
        c.pipeCapacity = pipeCap_;
        pipe_ = std::make_unique<IpcSim>(c);
        IpcOp r; r.kind = IpcOpKind::PipeClose; r.proc = 0; r.writeEnd = false;
        IpcOp wr; wr.kind = IpcOpKind::PipeClose; wr.proc = 1; wr.writeEnd = true;
        pipe_->apply(r);                             // the shell's children close the ends they do not use
        pipe_->apply(wr);
        pipeJob_ = j.id;
        event("pipe", "pipe() created; gen keeps the write end, upper keeps the read end (unused ends closed so EOF can happen)", j.id);
    }
    for (const JcProc& p : ps) procs_.push_back(p);
    jobs_.push_back(j);
    std::string pids;
    for (int p : j.pids) pids += (pids.empty() ? "" : ", ") + std::to_string(p);
    event("start", "job [" + std::to_string(j.id) + "] = process group " + std::to_string(j.pgid) + " (PID " + pids + "), " +
                       (background ? "background" : "foreground"), j.id);
    if (background) {
        say("[" + std::to_string(j.id) + "] " + std::to_string(j.pids.back()));
        return {true, "job [" + std::to_string(j.id) + "] started in the background; the shell keeps the terminal"};
    }
    fg_ = j.pgid;
    event("terminal", "foreground process group is now " + std::to_string(j.pgid) + " (the shell waits)", j.id);
    return {true, "job [" + std::to_string(j.id) + "] runs in the foreground: advance time (tick) or press Ctrl-Z / Ctrl-C"};
}

JcResult JobShell::fg(int id) {
    JcJob* j = nullptr;
    for (JcJob& x : jobs_) if (x.id == id) j = &x;
    if (!j || j->state == JcJobState::Done || j->state == JcJobState::Terminated) return {false, "fg: job has terminated"};
    say(j->cmdline);
    fg_ = j->pgid;
    event("terminal", "fg: job [" + std::to_string(id) + "] gets the terminal (foreground group " + std::to_string(j->pgid) + ")", id);
    if (j->state == JcJobState::Stopped) signalGroup(j->pgid, "CONT", "fg continues a stopped job");
    return {true, "job [" + std::to_string(id) + "] is now in the foreground"};
}

JcResult JobShell::bg(int id) {
    const JcJob* j = job(id);
    if (!j) return {false, "bg: no such job"};
    if (j->state == JcJobState::Running) return {false, "bg: job [" + std::to_string(id) + "] is already running in the background"};
    say("[" + std::to_string(id) + "]+ " + j->cmdline + (j->cmdline.back() == '&' ? "" : " &"));
    signalGroup(j->pgid, "CONT", "bg continues the job in the background");
    return {true, "job [" + std::to_string(id) + "] continues in the background (it is running, not blocked)"};
}

JcResult JobShell::kill(int id, const std::string& sig) {
    const JcJob* j = job(id);
    if (!j) return {false, "kill: no such job"};
    bool wasStopped = j->state == JcJobState::Stopped;
    signalGroup(j->pgid, sig, "kill -" + sig + " %" + std::to_string(id));
    if (wasStopped && (sig == "TERM" || sig == "INT")) {
        // (already applied: in this model termination signals act on stopped processes too, as the shell also sends SIGCONT)
        event("signal", "the shell also sends SIGCONT so the stopped job can act on SIG" + sig, id);
    }
    return {true, "SIG" + sig + " sent to process group " + std::to_string(j->pgid) + " (job [" + std::to_string(id) + "])"};
}

JcResult JobShell::wait(int id) {
    std::vector<int> targets;
    for (const JcJob& j : jobs_)
        if ((id == 0 || j.id == id) && (j.state == JcJobState::Running || j.state == JcJobState::Stopped) && j.pgid != fg_) targets.push_back(j.id);
    if (targets.empty()) return {true, "wait: nothing to wait for"};
    for (int t : targets)
        if (job(t)->state == JcJobState::Stopped) return {false, "wait: job [" + std::to_string(t) + "] is stopped - wait would block forever; use bg or fg first"};
    for (int k = 0; k < 300; ++k) {
        bool any = false;
        for (int t : targets) if (job(t)->state == JcJobState::Running) any = true;
        if (!any) break;
        step();
        snapshot("wait (tick)");
        for (int t : targets)
            if (job(t)->state == JcJobState::Stopped) return {false, "wait: job [" + std::to_string(t) + "] stopped while waiting"};
    }
    return {true, "wait: the background job(s) finished"};
}

JcResult JobShell::exec(const std::string& raw) {
    ++command_;
    std::string line = trim(raw);
    JcResult r;
    say("$ " + line);
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::vector<std::string> args;
    std::string a;
    while (in >> a) args.push_back(a);
    bool fgJob = fg_ != kShellPid;
    std::set<std::string> duringFg = {"ctrl-z", "ctrl-c", "type", "tick", "until-prompt", "help", ""};
    if (fgJob && !duringFg.count(cmd)) {
        r = {false, "the shell is waiting for the foreground job - press Ctrl-Z or Ctrl-C, type input, or advance time"};
    } else if (cmd.empty()) {
        r = {true, ""};
    } else if (cmd == "help") {
        say("programs: compute N | sleep N | readline | gen N | upper   (N = 1..50, '&' = background)");
        say("job control: jobs, fg [%n], bg [%n], kill [-TERM|-KILL|-INT|-STOP|-CONT] %n, wait [%n], ctrl-z, ctrl-c, type <text>, tick [n], until-prompt");
    } else if (cmd == "tick") {
        int n = 1;
        if (!args.empty()) { try { n = std::stoi(args[0]); } catch (...) { n = 0; } }
        if (n < 1 || n > 100) r = {false, "tick: 1..100"};
        else {
            for (int i = 0; i < n; ++i) { step(); if (i + 1 < n) snapshot("tick"); }
            r = {true, "advanced " + std::to_string(n) + " tick(s) to t=" + std::to_string(tick_)};
        }
    } else if (cmd == "until-prompt") {
        int k = 0;
        for (; k < 200 && fg_ != kShellPid; ++k) {
            bool waitsInput = false;
            for (const JcProc& p : procs_) if (p.pgid == fg_ && p.prog == JcProgram::ReadLine && alive(p) && p.state != JcState::Stopped && input_.empty() && p.state == JcState::Sleeping) waitsInput = true;
            if (waitsInput) { r = {false, "the foreground job waits for terminal input: use 'type <text>'"}; break; }
            step();
            if (fg_ != kShellPid) snapshot("tick");
        }
        if (r.ok) r.reply = fg_ == kShellPid ? "the shell has the terminal again (" + std::to_string(k) + " tick(s))" : "still running after 200 ticks";
    } else if (cmd == "jobs") {
        int cur = currentJob();
        int listed = 0;
        for (JcJob& j : jobs_) {
            if (j.reported) continue;
            say("[" + std::to_string(j.id) + "]" + (j.id == cur ? "+" : " ") + "  " + jcJobStateName(j.state) + "   " + j.cmdline +
                "   (pgid " + std::to_string(j.pgid) + ")");
            if (j.state == JcJobState::Done || j.state == JcJobState::Terminated) j.reported = true;
            ++listed;
        }
        r = {true, std::to_string(listed) + " job(s)"};
    } else if (cmd == "fg" || cmd == "bg" || cmd == "wait") {
        std::string err;
        int id = 0;
        if (cmd == "wait" && args.empty()) id = 0;
        else {
            id = resolveJob(args.empty() ? "" : args[0], err);
            if (!id) { r = {false, cmd + ": " + err}; }
        }
        if (r.ok) r = cmd == "fg" ? fg(id) : cmd == "bg" ? bg(id) : wait(id);
    } else if (cmd == "kill") {
        std::string sig = "TERM", spec;
        for (const std::string& x : args) { if (x.size() > 1 && x[0] == '-') sig = x.substr(1); else spec = x; }
        static const std::set<std::string> ok = {"TERM", "KILL", "INT", "STOP", "CONT"};
        std::string err;
        if (!ok.count(sig)) r = {false, "kill: signal must be one of -TERM -KILL -INT -STOP -CONT"};
        else if (spec.empty()) r = {false, "kill: name a job, e.g. kill -STOP %1"};
        else {
            int id = resolveJob(spec, err);
            r = id ? kill(id, sig) : JcResult{false, "kill: " + err};
        }
    } else if (cmd == "ctrl-z" || cmd == "ctrl-c") {
        if (!fgJob) r = {false, cmd + ": no foreground job (at the prompt the shell ignores it)"};
        else {
            signalGroup(fg_, cmd == "ctrl-z" ? "TSTP" : "INT", std::string("terminal driver: ") + (cmd == "ctrl-z" ? "Ctrl-Z" : "Ctrl-C") + " to the foreground group");
            r = {true, cmd == "ctrl-z" ? "SIGTSTP stopped the foreground job; the shell has the terminal again" : "SIGINT terminated the foreground job"};
        }
    } else if (cmd == "type") {
        std::string text = line.size() > 5 ? trim(line.substr(5)) : "";
        if (!fgJob) r = {false, "type: the shell itself is in the foreground; nothing reads terminal input"};
        else if (text.empty()) r = {false, "type: give some text"};
        else { input_ = text; r = {true, "terminal input queued for the foreground job"}; }
    } else {
        bool background = !line.empty() && line.back() == '&';
        std::string prog = trim(background ? line.substr(0, line.size() - 1) : line);
        r = runProgram(prog, background);
    }
    if (!r.reply.empty()) say((r.ok ? "# " : "error: ") + r.reply);
    snapshot(line.empty() ? "(empty)" : line);
    return r;
}

std::vector<std::string> JobShell::checkInvariants() const {
    std::vector<std::string> bad;
    std::set<int> pids, ids;
    if (fg_ != kShellPid) {
        const JcJob* f = nullptr;
        for (const JcJob& j : jobs_) if (j.pgid == fg_) f = &j;
        if (!f) bad.push_back("foreground group is not a job");
        else if (f->state != JcJobState::Running) bad.push_back("a stopped or finished job owns the terminal");
    }
    for (const JcProc& p : procs_) {
        if (!pids.insert(p.pid).second) bad.push_back("duplicate pid");
        if (p.sid != kShellPid) bad.push_back("process outside the session");
        if (p.state == JcState::Running && p.exitCode >= 0) bad.push_back("exited process running");
    }
    for (const JcJob& j : jobs_) {
        if (!ids.insert(j.id).second) bad.push_back("duplicate job id");
        for (int pid : j.pids)
            for (const JcProc& p : procs_) if (p.pid == pid && p.pgid != j.pgid) bad.push_back("process not in its job's group");
        if (j.pgid != j.pids.front()) bad.push_back("pgid is not the first pid");
    }
    return bad;
}

JobShell runJobScript(const std::vector<std::string>& commands, std::vector<JcResult>* results) {
    JobShell sh;
    for (const std::string& c : commands) {
        JcResult r = sh.exec(c);
        if (results) results->push_back(r);
    }
    return sh;
}

std::vector<JcScenario> jobScenarios() {
    return {
        {"Background job: stop, resume, foreground, complete",
         "A background job is stopped with SIGSTOP (no progress while stopped), resumed with bg (running, not blocked), brought to "
         "the foreground with fg and finishes - the shell then gets the terminal back.",
         {"compute 8 &", "tick 2", "kill -STOP %1", "jobs", "tick 2", "bg %1", "tick 2", "fg %1", "until-prompt", "jobs"}},
        {"Foreground job: Ctrl-Z, bg, fg", "Ctrl-Z sends SIGTSTP to the foreground process group; the shell takes the terminal back.",
         {"compute 6", "tick 2", "ctrl-z", "jobs", "bg", "tick 1", "fg", "until-prompt"}},
        {"Background is not blocked", "A sleeping job and a computing job both run in the background while the shell keeps the terminal.",
         {"sleep 4 &", "compute 3 &", "jobs", "tick 2", "jobs", "tick 3", "jobs"}},
        {"Background read: SIGTTIN", "A background job that reads the terminal is stopped by SIGTTIN; fg gives it the terminal so it can read.",
         {"readline &", "tick 1", "jobs", "fg %1", "type hello", "tick 1"}},
        {"Two-stage pipeline", "gen | upper share one process group; items flow through a pipe of capacity 3 (blocking on full / empty); "
         "Ctrl-Z stops both stages; EOF ends upper after gen closes its write end.",
         {"gen 6 | upper", "tick 3", "ctrl-z", "jobs", "fg", "until-prompt"}},
        {"Ctrl-C, kill and wait", "SIGINT ends the foreground job; kill -TERM ends a background job; wait waits for background jobs.",
         {"compute 20", "tick 1", "ctrl-c", "compute 30 &", "sleep 3 &", "kill -TERM %2", "wait", "jobs"}},
        {"Invalid references are refused", "Unknown jobs, bare PIDs and arbitrary commands are rejected - nothing reaches the host.",
         {"fg %7", "bg", "kill -STOP 12345", "rm -rf /", "compute 99", "jobs"}},
    };
}
