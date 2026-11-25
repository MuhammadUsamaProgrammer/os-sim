// console_main.cpp - console demo of OS-Sim (executable ossim_console).
// Usage: ossim_console [sched [file.csv] [quantum]]
// With no arguments it runs every demo on the sample files.
#include <cstdio>
#include <iostream>
#include <string>
#include "io_formats.h"
#include "scheduler.h"

// Prints a text Gantt chart such as |P1......|P2..| with a time ruler.
static void printGantt(const SchedResult& r) {
    std::string bar = "|", ruler = "0";
    for (const Segment& s : r.timeline) {
        std::string label = s.pid < 0 ? "idle" : "P" + std::to_string(s.pid);
        int width = std::max<int>((int)label.size() + 1, s.end - s.start);
        std::string cell = label + std::string(width - label.size(), '.');
        bar += cell + "|";
        std::string t = std::to_string(s.end);
        ruler += std::string(cell.size() + 1 - t.size(), ' ') + t;
    }
    std::cout << "  " << bar << "\n  " << ruler << "\n";
}

// Prints the per-process metrics table and the averages for one result.
static void printResult(const SchedResult& r) {
    std::printf("\n== %s%s ==\n", algoName(r.algo),
                r.algo == Algo::RR ? (" (q=" + std::to_string(r.quantum) + ")").c_str() : "");
    printGantt(r);
    std::printf("  %-4s %7s %5s %10s %7s %10s %8s\n", "PID", "Arrival", "Burst", "Completion",
                "Waiting", "Turnaround", "Response");
    for (const ProcMetrics& m : r.metrics)
        std::printf("  P%-3d %7d %5d %10d %7d %10d %8d\n", m.pid, m.arrival, m.burst, m.completion,
                    m.waiting, m.turnaround, m.response);
    std::printf("  avg WT=%.4f  avg TAT=%.4f  avg RT=%.4f  context switches=%d  CPU util=%.1f%%\n",
                r.avgWaiting, r.avgTurnaround, r.avgResponse, r.contextSwitches, cpuUtilization(r));
}

// Runs all six schedulers on one process file.
static void demoScheduling(const std::string& path, int quantum) {
    std::cout << "\n######## Scheduling: " << path << " ########\n";
    for (const SchedResult& r : runAll(loadProcessesCsv(path), quantum)) printResult(r);
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "all";
    try {
        if (cmd == "sched") {
            if (argc > 2) demoScheduling(argv[2], argc > 3 ? std::stoi(argv[3]) : 4);
            else for (const char* f : {"samples/processes_fcfs.csv", "samples/processes_srtf.csv",
                                       "samples/processes_priority.csv"}) demoScheduling(f, 4);
        } else if (cmd == "all") {
            for (const char* f : {"samples/processes_fcfs.csv", "samples/processes_srtf.csv",
                                  "samples/processes_priority.csv"}) demoScheduling(f, 4);
        } else {
            std::cerr << "usage: ossim_console [all | sched [file.csv] [quantum]]\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
