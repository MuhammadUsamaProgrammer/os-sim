// sysmon_native.cpp - System Monitor raw-counter backends (see sysmon_native.h).
#include "sysmon_native.h"
#include <chrono>

static double monoSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

#if defined(__linux__)
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

static bool slurp(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

const char* sysmonBackendName() { return "Linux /proc"; }

RawSample sampleSystem() {
    RawSample s;
    s.platform = "Linux";
    s.source = "/proc (procfs)";
    s.timeSec = monoSeconds();
    s.ticksPerSec = sysconf(_SC_CLK_TCK);
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    s.logicalCpus = nproc > 0 ? (int)nproc : 1;
    long pageKb = sysconf(_SC_PAGESIZE) / 1024;
    std::string text;
    if (slurp("/proc/stat", text)) s.cpuOk = parseProcStat(text, s.cpu);
    if (slurp("/proc/meminfo", text)) s.memOk = parseMeminfo(text, s.memTotalKb, s.memAvailableKb);
    if (slurp("/proc/diskstats", text)) s.diskOk = parseDiskstats(text, s.diskReadBytes, s.diskWrittenBytes);
    if (DIR* d = opendir("/proc")) {
        s.procsOk = true;
        int unreadable = 0;
        while (dirent* e = readdir(d)) {
            const char* n = e->d_name;
            bool digits = *n != 0;
            for (const char* c = n; *c; ++c) if (!isdigit((unsigned char)*c)) { digits = false; break; }
            if (!digits) continue;
            std::string base = std::string("/proc/") + n;
            ProcRaw p;
            if (!slurp(base + "/stat", text) || !parsePidStat(text, p, pageKb > 0 ? pageKb : 4)) continue;   // vanished meanwhile
            if (slurp(base + "/status", text)) parsePidStatus(text, p);
            else { p.accessible = false; ++unreadable; }
            s.procs.push_back(p);
        }
        closedir(d);
        if (unreadable) s.notes.push_back(std::to_string(unreadable) + " process status file(s) not readable (permissions)");
    }
    return s;
}

#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

static unsigned long long ft(const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }

const char* sysmonBackendName() { return "Windows API"; }

RawSample sampleSystem() {
    RawSample s;
    s.platform = "Windows";
    s.source = "GetSystemTimes / GlobalMemoryStatusEx / Toolhelp32 / GetProcessTimes / GetProcessMemoryInfo";
    s.timeSec = monoSeconds();
    s.ticksPerSec = 10000000;                    // FILETIME units: 100 ns
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    s.logicalCpus = (int)si.dwNumberOfProcessors;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        // kernel time INCLUDES idle time on Windows
        s.cpu.total.idle = ft(idle);
        s.cpu.total.system = ft(kernel) - ft(idle);
        s.cpu.total.user = ft(user);
        s.cpuOk = true;                          // per-core times are not collected by this backend
    }
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        s.memOk = true;
        s.memTotalKb = (long long)(ms.ullTotalPhys / 1024);
        s.memAvailableKb = (long long)(ms.ullAvailPhys / 1024);
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof pe;
        s.procsOk = true;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            ProcRaw p;
            p.pid = (int)pe.th32ProcessID;
            p.ppid = (int)pe.th32ParentProcessID;
            p.threads = (int)pe.cntThreads;
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, sizeof name, nullptr, nullptr);
            p.name = name;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (!h) h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                FILETIME c, e, k, u;
                if (GetProcessTimes(h, &c, &e, &k, &u)) { p.cpuTicks = ft(k) + ft(u); p.startTime = ft(c); }
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) p.rssKb = (long long)(pmc.WorkingSetSize / 1024);   // working set ~ RSS
                CloseHandle(h);
            } else p.accessible = false;
            s.procs.push_back(p);
        }
        CloseHandle(snap);
    }
    s.notes.push_back("Windows: per-core CPU and disk rates are not collected by this backend");
    return s;
}

#else
const char* sysmonBackendName() { return "unsupported"; }
RawSample sampleSystem() {
    RawSample s;
    s.platform = "unsupported";
    s.source = "no backend for this operating system";
    s.timeSec = monoSeconds();
    s.notes.push_back("System monitoring is not implemented for this platform");
    return s;
}
#endif
