// sysmon_native.h - platform backends of the System Monitor (Phase 3). They only COLLECT raw
// counters (core/sysmon.h computes everything). Read-only: no process is ever signalled here.
//   Linux:   /proc/stat, /proc/meminfo, /proc/diskstats, /proc/<pid>/stat and /proc/<pid>/status
//            (command lines are NOT read). Processes that vanish or are unreadable are skipped /
//            marked inaccessible.
//   Windows: GetSystemTimes, GlobalMemoryStatusEx, Toolhelp32 snapshot, GetProcessTimes,
//            GetProcessMemoryInfo (compiled only on Windows; must be verified on a Windows machine).
//   Other:   sampleSystem() returns a sample with every *Ok flag false ("not supported").
#pragma once
#include "sysmon.h"

// Collects one raw sample of the machine this program runs on.
RawSample sampleSystem();
// Name of the backend compiled into this build ("Linux /proc", "Windows API", "unsupported").
const char* sysmonBackendName();
