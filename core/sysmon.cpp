// sysmon.cpp - System Monitor arithmetic and /proc parsers (see sysmon.h).
#include "sysmon.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

static bool parseCpuLine(const std::string& line, CpuTimes& t) {
    std::istringstream in(line);
    std::string label;
    in >> label;
    unsigned long long v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int n = 0;
    while (n < 8 && in >> v[n]) ++n;
    if (n < 4) return false;            // user nice system idle are mandatory; the rest default to 0
    t.user = v[0]; t.nice = v[1]; t.system = v[2]; t.idle = v[3];
    t.iowait = v[4]; t.irq = v[5]; t.softirq = v[6]; t.steal = v[7];
    return true;
}

bool parseProcStat(const std::string& text, CpuSample& out) {
    out = CpuSample();
    std::istringstream in(text);
    std::string line;
    bool total = false;
    while (std::getline(in, line)) {
        if (line.rfind("cpu", 0) != 0) continue;
        CpuTimes t;
        if (!parseCpuLine(line, t)) continue;
        if (line.size() > 3 && line[3] == ' ') { out.total = t; total = true; }
        else out.cores.push_back(t);
    }
    return total;
}

bool parseMeminfo(const std::string& text, long long& totalKb, long long& availableKb) {
    totalKb = availableKb = -1;
    long long freeKb = -1, buffers = 0, cached = 0;
    std::istringstream in(text);
    std::string key;
    long long val;
    std::string unit;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        if (!(l >> key >> val)) continue;
        if (key == "MemTotal:") totalKb = val;
        else if (key == "MemAvailable:") availableKb = val;
        else if (key == "MemFree:") freeKb = val;
        else if (key == "Buffers:") buffers = val;
        else if (key == "Cached:") cached = val;
    }
    if (availableKb < 0 && freeKb >= 0) availableKb = freeKb + buffers + cached;   // old kernels: estimate
    return totalKb > 0 && availableKb >= 0 && availableKb <= totalKb;
}

// /proc/<pid>/stat: "pid (comm) state ppid ..." - comm may contain spaces and ')' so the name ends
// at the LAST ')'.
bool parsePidStat(const std::string& text, ProcRaw& out, long pageSizeKb) {
    size_t open = text.find('('), close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    try { out.pid = std::stoi(text.substr(0, open)); } catch (...) { return false; }
    out.name = text.substr(open + 1, close - open - 1);
    std::istringstream in(text.substr(close + 1));
    std::vector<std::string> f;
    std::string w;
    while (in >> w) f.push_back(w);
    // fields after comm: 0 state, 1 ppid, ... 11 utime, 12 stime, 17 num_threads, 19 starttime, 21 rss (pages)
    if (f.size() < 22) return false;
    try {
        out.state = f[0].empty() ? '?' : f[0][0];
        out.ppid = std::stoi(f[1]);
        out.cpuTicks = std::stoull(f[11]) + std::stoull(f[12]);
        out.threads = std::stoi(f[17]);
        out.startTime = std::stoull(f[19]);
        long long rssPages = std::stoll(f[21]);
        out.rssKb = rssPages >= 0 ? rssPages * pageSizeKb : -1;
    } catch (...) { return false; }
    return true;
}

void parsePidStatus(const std::string& text, ProcRaw& out) {
    std::istringstream in(text);
    std::string line;
    bool haveRss = false;
    while (std::getline(in, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        v.erase(0, v.find_first_not_of(" \t"));
        try {
            if (k == "Name") out.name = v;
            else if (k == "State" && !v.empty()) out.state = v[0];
            else if (k == "PPid") out.ppid = std::stoi(v);
            else if (k == "Uid") out.uid = std::stoi(v);                   // real uid (first column)
            else if (k == "Threads") out.threads = std::stoi(v);
            else if (k == "VmRSS") { out.rssKb = std::stoll(v); haveRss = true; }
        } catch (...) {}
    }
    (void)haveRss;      // kernel threads have no VmRSS line: rssKb keeps the stat value (0) or -1
}

bool parseDiskstats(const std::string& text, unsigned long long& readBytes, unsigned long long& writtenBytes) {
    readBytes = writtenBytes = 0;
    std::istringstream in(text);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        unsigned long long major, minor, rdIos, rdMerge, rdSect, rdTicks, wrIos, wrMerge, wrSect;
        std::string dev;
        if (!(l >> major >> minor >> dev >> rdIos >> rdMerge >> rdSect >> rdTicks >> wrIos >> wrMerge >> wrSect)) continue;
        if (dev.rfind("loop", 0) == 0 || dev.rfind("ram", 0) == 0 || dev.rfind("zram", 0) == 0 || dev.rfind("dm-", 0) == 0) continue;
        bool partition;
        if (dev.rfind("nvme", 0) == 0 || dev.rfind("mmcblk", 0) == 0) partition = dev.find('p', 4) != std::string::npos;
        else partition = !dev.empty() && isdigit((unsigned char)dev.back());
        if (partition) continue;
        readBytes += rdSect * 512ULL;               // /proc/diskstats sectors are always 512 bytes
        writtenBytes += wrSect * 512ULL;
        any = true;
    }
    return any;
}

double cpuBusyPercent(const CpuTimes& a, const CpuTimes& b, bool& valid) {
    valid = false;
    if (b.total() < a.total() || b.idleAll() < a.idleAll()) return 0;      // counter reset
    unsigned long long dt = b.total() - a.total();
    if (dt == 0) return 0;
    unsigned long long di = b.idleAll() - a.idleAll();
    valid = true;
    double p = 100.0 * double(dt - std::min(di, dt)) / double(dt);
    return std::max(0.0, std::min(100.0, p));
}

double procCpuPercent(unsigned long long a, unsigned long long b, double dt, long tps, bool& valid) {
    valid = false;
    if (dt <= 0 || tps <= 0 || b < a) return 0;
    valid = true;
    return 100.0 * double(b - a) / (dt * double(tps));
}

double ratePerSec(unsigned long long a, unsigned long long b, double dt, bool& valid) {
    valid = false;
    if (dt <= 0 || b < a) return 0;
    valid = true;
    return double(b - a) / dt;
}

static std::string stateName(char s) {
    switch (s) {
        case 'R': return "running";
        case 'S': return "sleeping";
        case 'D': return "disk wait";
        case 'Z': return "zombie";
        case 'T': return "stopped";
        case 't': return "traced";
        case 'I': return "idle";
        case 'X': return "dead";
        case '?': return "n/a";
    }
    return std::string(1, s);
}

SystemSnapshot SysMonModel::update(const RawSample& s) {
    SystemSnapshot snap;
    snap.seq = ++seq_;
    snap.timeSec = s.timeSec;
    snap.platform = s.platform;
    snap.source = s.source;
    snap.logicalCpus = s.logicalCpus;
    snap.notes = s.notes;
    double dt = havePrev_ ? s.timeSec - prev_.timeSec : 0;
    snap.intervalSec = dt;
    snap.warmingUp = !havePrev_;
    if (havePrev_ && dt <= 0) snap.notes.push_back("zero or negative interval between samples: rates not computed");
    if (s.memOk && s.memTotalKb > 0) {
        snap.memValid = true;
        snap.memTotalKb = s.memTotalKb;
        snap.memAvailableKb = s.memAvailableKb;
        snap.memUsedKb = s.memTotalKb - s.memAvailableKb;
        snap.memPercent = 100.0 * double(snap.memUsedKb) / double(s.memTotalKb);
    }
    if (havePrev_ && dt > 0 && s.cpuOk && prev_.cpuOk) {
        snap.cpuPercent = cpuBusyPercent(prev_.cpu.total, s.cpu.total, snap.cpuValid);
        if (!snap.cpuValid) snap.notes.push_back("CPU counters went backwards (reset): this sample is skipped");
        if (s.cpu.cores.size() == prev_.cpu.cores.size())
            for (size_t i = 0; i < s.cpu.cores.size(); ++i) {
                bool v;
                double p = cpuBusyPercent(prev_.cpu.cores[i], s.cpu.cores[i], v);
                snap.corePercent.push_back(v ? p : -1);
            }
    }
    if (havePrev_ && dt > 0 && s.diskOk && prev_.diskOk) {
        bool v1, v2;
        snap.diskReadBps = ratePerSec(prev_.diskReadBytes, s.diskReadBytes, dt, v1);
        snap.diskWriteBps = ratePerSec(prev_.diskWrittenBytes, s.diskWrittenBytes, dt, v2);
        snap.diskValid = v1 && v2;
    }
    std::map<int, std::pair<unsigned long long, unsigned long long>> now;
    for (const ProcRaw& p : s.procs) {
        ProcView v;
        v.pid = p.pid; v.ppid = p.ppid; v.name = p.name; v.state = stateName(p.state);
        v.rssKb = p.rssKb; v.threads = p.threads; v.uid = p.uid; v.accessible = p.accessible; v.startTime = p.startTime;
        auto it = prevProc_.find(p.pid);
        // identity = pid + start time: a different start time means the PID was reused
        if (havePrev_ && it != prevProc_.end() && it->second.first == p.startTime && p.accessible)
            v.cpuPercent = procCpuPercent(it->second.second, p.cpuTicks, dt, s.ticksPerSec, v.cpuValid);
        else v.fresh = true;
        now[p.pid] = {p.startTime, p.cpuTicks};
        snap.procs.push_back(v);
    }
    snap.processCount = (int)s.procs.size();
    if (s.procsOk) prevProc_ = now;               // processes that vanished are simply dropped
    prev_ = s;
    havePrev_ = true;
    return snap;
}

void SysMonModel::reset() {
    havePrev_ = false;
    prev_ = RawSample();
    prevProc_.clear();
    seq_ = 0;
}

void MetricHistory::setCapacity(int c) {
    cap_ = c < 2 ? 2 : c;
    while ((int)v_.size() > cap_) v_.pop_front();
}
void MetricHistory::push(double v) {
    v_.push_back(v);
    while ((int)v_.size() > cap_) v_.pop_front();
}

bool AlertMonitor::feed(long seq, double value, AlertEvent& ev) {
    char buf[200];
    if (!active_) {
        above_ = value > threshold_ ? above_ + 1 : 0;
        if (above_ >= samples_) {
            active_ = true;
            below_ = 0;
            std::snprintf(buf, sizeof buf, "%s above %.0f %% for %d consecutive sample(s) (now %.1f %%)", metric_.c_str(), threshold_, samples_, value);
            ev = AlertEvent{seq, true, metric_, value, threshold_, buf};
            return true;
        }
    } else {
        below_ = value < threshold_ - hyst_ ? below_ + 1 : 0;
        if (below_ >= samples_) {
            active_ = false;
            above_ = 0;
            std::snprintf(buf, sizeof buf, "%s back below %.0f %% (now %.1f %%)", metric_.c_str(), threshold_ - hyst_, value);
            ev = AlertEvent{seq, false, metric_, value, threshold_, buf};
            return true;
        }
    }
    return false;
}

std::string formatKb(long long kb) {
    if (kb < 0) return "n/a";
    char buf[32];
    if (kb >= 1024LL * 1024) std::snprintf(buf, sizeof buf, "%.2f GiB", kb / 1048576.0);
    else if (kb >= 1024) std::snprintf(buf, sizeof buf, "%.1f MiB", kb / 1024.0);
    else std::snprintf(buf, sizeof buf, "%lld KiB", kb);
    return buf;
}
