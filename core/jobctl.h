// jobctl.h - Phase 3 "Job Control": a deterministic model of shell job control (pure C++17).
//
// One terminal session (session id = shell pid 100). Every command line typed into the educational
// shell becomes a JOB with a job id (%1, %2 ...) that is separate from the PIDs; each job is one
// PROCESS GROUP (pgid = pid of its first process). The terminal has exactly one FOREGROUND process
// group at any time (the shell's own group when it shows the prompt).
//
// Limited command grammar (nothing is ever executed on the host):
//   compute N [&]      CPU-bound program, N ticks of work
//   sleep N [&]        sleeps N ticks (blocked, uses no CPU - a background job is not "blocked")
//   readline [&]       reads one line from the terminal (in the background: SIGTTIN stops it)
//   gen N | upper [&]  two-stage pipeline: gen writes N items into a pipe, upper reads and prints them
//                      (upper is slower - one item every 2 ticks - so the pipe fills and write() blocks)
//   jobs | fg [%n] | bg [%n] | kill [-TERM|-KILL|-INT|-STOP|-CONT] %n | wait [%n]
//   ctrl-z | ctrl-c    keyboard signals to the FOREGROUND process group (SIGTSTP / SIGINT)
//   type <text>        terminal input (delivered to the foreground job)
//   tick [n]           advance n ticks: every runnable process makes one step per tick
//   until-prompt       advance until the shell has the terminal again (bounded)
// Only job ids of this shell are accepted as targets: a bare PID is refused.
//
// The pipeline REUSES the IPC pipe model (core/ipc.h, IpcSim in pipe mode): bounded buffer,
// blocking write on full / read on empty, closing unused ends, EOF when every write end is closed.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ipc.h"

enum class JcState { Running, Sleeping, Stopped, Exited, Killed };
const char* jcStateName(JcState);

enum class JcProgram { Compute, Sleep, ReadLine, Gen, Upper };

struct JcProc {
    int pid = 0, ppid = 100, pgid = 0, sid = 100;
    JcProgram prog = JcProgram::Compute;
    std::string cmd;
    JcState state = JcState::Running;
    JcState beforeStop = JcState::Running;   // restored by SIGCONT
    int remaining = 0;                       // compute: work ticks; sleep: ticks; gen: items left
    int produced = 0, consumed = 0;
    int exitCode = -1, signal = 0;           // exit status or terminating signal
    std::string waitingOn;                   // why it sleeps ("pipe empty", "terminal input" ...)
};

enum class JcJobState { Running, Stopped, Done, Terminated };
const char* jcJobStateName(JcJobState);

struct JcJob {
    int id = 0, pgid = 0;
    std::string cmdline;
    std::vector<int> pids;
    bool startedInBackground = false;
    JcJobState state = JcJobState::Running;
    bool reported = false;                   // completion announced by the shell
};

struct JcEvent { int tick = 0; std::string kind; std::string text; int job = 0; int pid = 0; };

struct JcSnapshot {
    int tick = 0;
    int command = -1;                        // index of the command that produced this snapshot
    std::string label;                       // command or "tick"
    int fgPgid = 100;                        // terminal's foreground process group
    std::vector<JcProc> procs;
    std::vector<JcJob> jobs;
    std::vector<std::string> transcript;     // terminal output so far
    std::vector<JcEvent> events;             // all events so far
    std::string pipeBuffer;                  // contents of the pipeline's pipe (if any)
    int pipeCapacity = 0;
    std::string pendingInput;                // typed text not yet read
};

struct JcResult { bool ok = true; std::string reply; };

class JobShell {
public:
    static const int kShellPid = 100;
    explicit JobShell(int pipeCapacity = 3);
    JcResult exec(const std::string& line);  // records snapshots (one per tick + one per command)
    const std::vector<JcSnapshot>& snapshots() const { return snaps_; }
    const std::vector<JcProc>& procs() const { return procs_; }
    const std::vector<JcJob>& jobs() const { return jobs_; }
    int fgPgid() const { return fg_; }
    int tick() const { return tick_; }
    bool shellHasTerminal() const { return fg_ == kShellPid; }
    const JcJob* job(int id) const;
    std::vector<std::string> checkInvariants() const;    // empty = consistent
private:
    JcResult runProgram(const std::string& line, bool background);
    JcResult fg(int id), bg(int id), kill(int id, const std::string& sig), wait(int id);
    void signalGroup(int pgid, const std::string& sig, const std::string& why);
    void step();                              // one tick
    void updateJobs();
    int resolveJob(const std::string& spec, std::string& err) const;
    int currentJob() const;
    JcProc* proc(int pid);
    void event(const std::string& kind, const std::string& text, int job = 0, int pid = 0);
    void say(const std::string& line) { out_.push_back(line); }
    void snapshot(const std::string& label);
    std::vector<JcProc> procs_;
    std::vector<JcJob> jobs_;
    std::vector<std::string> out_;
    std::vector<JcEvent> events_;
    std::vector<JcSnapshot> snaps_;
    std::string input_;
    int fg_ = kShellPid, tick_ = 0, nextPid_ = 101, nextJob_ = 1, command_ = -1;
    int pipeCap_;
    std::unique_ptr<IpcSim> pipe_;            // pipeline pipe (IPC model), one pipeline at a time
    int pipeJob_ = 0, genIdx_ = 0, upperIdx_ = 1;
};

struct JcScenario { std::string name, description; std::vector<std::string> commands; };
std::vector<JcScenario> jobScenarios();
// Runs commands from a fresh shell; returns the shell (snapshots for playback).
JobShell runJobScript(const std::vector<std::string>& commands, std::vector<JcResult>* results = nullptr);
