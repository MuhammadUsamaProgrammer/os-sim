// sysmon.h - Phase 3 "System Monitor" arithmetic (pure C++17, no Qt, no threads, no I/O).
// The platform backends (engine/sysmon_native.*) only collect RAW counters; everything that is
// computed - parsing of /proc text, timestamped deltas, CPU percentages, rates, PID-reuse
// handling, alert debouncing and bounded history - lives here so it can be tested with fixtures.
//
// Definitions shown in the GUI:
//   Total CPU %   = (delta busy jiffies) / (delta all jiffies) over all logical CPUs, 0..100.
//   Process CPU % = (delta utime+stime) / (delta wall seconds * ticks per second) * 100, i.e. the
//                   share of ONE logical core (a multithreaded process can exceed 100 %); the
//                   machine share is that value divided by the number of logical CPUs.
//   RSS           = resident set size: pages of the process that are in RAM now, INCLUDING shared
//                   pages (libraries, shared memory). It is not exclusive memory, and the RSS values
//                   of all processes do not add up to the used physical memory.
//   Rates and percentages need two samples: the first sample only "warms up".
#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

struct CpuTimes {
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long idleAll() const { return idle + iowait; }
    unsigned long long total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
};
struct CpuSample { CpuTimes total; std::vector<CpuTimes> cores; };

struct ProcRaw {
    int pid = 0, ppid = 0;
    std::string name;                    // comm (from stat, or Name: from status)
    char state = '?';                    // R S D Z T t I ... ('?' = not available)
    unsigned long long cpuTicks = 0;     // utime + stime
    unsigned long long startTime = 0;    // start time (identity: pid + start time)
    long long rssKb = -1;                // -1 = not available
    int threads = -1;
    int uid = -1;
    bool accessible = true;              // false: status / stat not readable (permission)
};

struct RawSample {
    double timeSec = 0;                  // monotonic clock
    bool cpuOk = false;
    CpuSample cpu;
    bool memOk = false;
    long long memTotalKb = 0, memAvailableKb = 0;
    bool procsOk = false;
    std::vector<ProcRaw> procs;
    bool diskOk = false;
    unsigned long long diskReadBytes = 0, diskWrittenBytes = 0;
    long ticksPerSec = 100;
    int logicalCpus = 1;
    std::string platform, source;        // "Linux", "/proc" ...
    std::vector<std::string> notes;      // backend remarks (e.g. unreadable files)
};

// ---- /proc parsers (Linux text formats; return false on unusable input)
bool parseProcStat(const std::string& text, CpuSample& out);
bool parseMeminfo(const std::string& text, long long& totalKb, long long& availableKb);
bool parsePidStat(const std::string& text, ProcRaw& out, long pageSizeKb = 4);   // comm may contain spaces and ')'
void parsePidStatus(const std::string& text, ProcRaw& out);                      // Name, State, PPid, Uid, Threads, VmRSS
// Whole-disk read / write bytes from /proc/diskstats (partitions, loop and ram devices skipped).
bool parseDiskstats(const std::string& text, unsigned long long& readBytes, unsigned long long& writtenBytes);

// ---- delta arithmetic
// Percentage of busy time between two CPU samples; valid = false for a zero / negative interval or
// a counter that went backwards (reset, e.g. after suspend or a CPU going offline).
double cpuBusyPercent(const CpuTimes& prev, const CpuTimes& cur, bool& valid);
// Process CPU in percent of ONE logical core.
double procCpuPercent(unsigned long long prevTicks, unsigned long long curTicks, double dtSec, long ticksPerSec, bool& valid);
// Rate in bytes per second; invalid for dt <= 0 or a counter reset.
double ratePerSec(unsigned long long prev, unsigned long long cur, double dtSec, bool& valid);

struct ProcView {
    int pid = 0, ppid = 0;
    std::string name, state;
    bool cpuValid = false;
    double cpuPercent = 0;               // of one logical core
    long long rssKb = -1;
    int threads = -1, uid = -1;
    bool accessible = true;
    bool fresh = false;                  // first time seen (or PID reused): CPU % not yet defined
    unsigned long long startTime = 0;
};

struct SystemSnapshot {
    long seq = 0;
    double timeSec = 0, intervalSec = 0;
    bool warmingUp = true;               // no previous sample yet
    bool cpuValid = false;
    double cpuPercent = 0;
    std::vector<double> corePercent;     // empty = per-core not available
    bool memValid = false;
    long long memTotalKb = 0, memAvailableKb = 0, memUsedKb = 0;
    double memPercent = 0;               // (total - available) / total
    int processCount = 0;
    bool diskValid = false;
    double diskReadBps = 0, diskWriteBps = 0;
    int logicalCpus = 1;
    std::vector<ProcView> procs;
    std::vector<std::string> notes;
    std::string platform, source;
};

// Turns consecutive raw samples into snapshots (keeps only the previous sample).
class SysMonModel {
public:
    SystemSnapshot update(const RawSample& s);
    void reset();
    long samples() const { return seq_; }
private:
    bool havePrev_ = false;
    RawSample prev_;
    std::map<int, std::pair<unsigned long long, unsigned long long>> prevProc_;   // pid -> (startTime, cpuTicks)
    long seq_ = 0;
};

// Bounded history of one metric (oldest dropped first).
class MetricHistory {
public:
    explicit MetricHistory(int capacity = 120) : cap_(capacity < 2 ? 2 : capacity) {}
    void setCapacity(int c);
    void push(double v);
    void clear() { v_.clear(); }
    const std::deque<double>& values() const { return v_; }
    int capacity() const { return cap_; }
private:
    int cap_;
    std::deque<double> v_;
};

// Informational threshold alert with debounce: raised after `samples` consecutive values above
// the threshold, cleared after `samples` consecutive values below (threshold - hysteresis).
struct AlertEvent { long seq = 0; bool raised = false; std::string metric; double value = 0, threshold = 0; std::string text; };
class AlertMonitor {
public:
    AlertMonitor(std::string metric = "CPU", double threshold = 90, int samples = 3, double hysteresis = 5)
        : metric_(std::move(metric)), threshold_(threshold), samples_(samples < 1 ? 1 : samples), hyst_(hysteresis) {}
    void configure(double threshold, int samples) { threshold_ = threshold; samples_ = samples < 1 ? 1 : samples; }
    bool feed(long seq, double value, AlertEvent& ev);   // true if an event (raise / clear) happened
    bool active() const { return active_; }
    void reset() { active_ = false; above_ = below_ = 0; }
private:
    std::string metric_;
    double threshold_;
    int samples_;
    double hyst_;
    bool active_ = false;
    int above_ = 0, below_ = 0;
};

std::string formatKb(long long kb);      // "12.3 MiB", "n/a" for negative
