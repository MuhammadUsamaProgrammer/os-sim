// console_main.cpp - console demo of OS-Sim (executable ossim_console).
// Usage: ossim_console [all | sched [file.csv] [quantum] | memory [refs|file] [frames] | deadlock [banker.txt] [detect.txt]]
// With no arguments it runs every demo on the sample files.
#include <cstdio>
#include <iostream>
#include <string>
#include "io_formats.h"
#include "scheduler.h"
#include "memory.h"
#include "deadlock.h"

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

// Prints the frame table (one column per reference) for one algorithm.
static void printMemory(const MemResult& r) {
    std::printf("\n== %s, %d frames ==\n  ref    ", pageAlgoName(r.algo), r.numFrames);
    for (const MemStep& s : r.steps) std::printf("%3d", s.page);
    for (int f = 0; f < r.numFrames; ++f) {
        std::printf("\n  frame%d ", f + 1);
        for (const MemStep& s : r.steps)
            if (s.frames[f] < 0) std::printf("  -"); else std::printf("%3d", s.frames[f]);
    }
    std::printf("\n         ");
    for (const MemStep& s : r.steps) std::printf("%3s", s.fault ? "F" : ".");
    std::printf("\n  faults=%d hits=%d hit ratio=%.3f\n", r.faults, r.hits, r.hitRatio);
}

// Runs FIFO, LRU and Optimal on one reference string.
static void demoMemory(const std::string& refText, int frames) {
    std::vector<int> refs = parseRefString(refText);
    std::cout << "\n######## Page replacement: " << refs.size() << " references ########\n";
    for (PageAlgo a : {PageAlgo::FIFO, PageAlgo::LRU, PageAlgo::OPTIMAL})
        printMemory(runPageReplacement(a, refs, frames));
}

// Loads a reference string from a file if the argument names one, else uses it as text.
static std::string refArg(const std::string& a) {
    try { return readTextFile(a); } catch (const std::exception&) { return a; }
}

// Prints a matrix with one "P<i>" row per process.
static void printMatrix(const char* title, const Matrix& m) {
    std::printf("  %s:\n", title);
    for (size_t i = 0; i < m.size(); ++i) std::printf("    P%zu  %s\n", i, vecToString(m[i]).c_str());
}

// Formats a pid list as "P1, P3, P4".
static std::string pidList(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", P" : "P") + std::to_string(v[i]);
    return s;
}

// Banker's safety + example requests, then detection and recovery on the samples.
static void demoDeadlock(const std::string& bankerFile, const std::string& deadlockFile) {
    std::cout << "\n######## Banker's algorithm: " << bankerFile << " ########\n";
    MatrixData b = loadMatrixFile(bankerFile);
    SafetyResult s = bankersSafety(b.alloc, b.max, b.available);
    printMatrix("Need", s.need);
    for (const SafetyStep& st : s.trace)
        std::printf("    P%d runs: work %s -> %s\n", st.pid, vecToString(st.workBefore).c_str(),
                    vecToString(st.workAfter).c_str());
    std::printf("  %s %s\n", s.safe ? "SAFE:" : "UNSAFE", s.safe ? ("<" + pidList(s.sequence) + ">").c_str() : "");
    if (b.nProcs > 1 && b.nRes == 3) {
        RequestResult r = bankersRequest(1, {1, 0, 2}, b.alloc, b.max, b.available);
        std::printf("  request P1 (1 0 2): %s - %s\n", r.granted ? "GRANTED" : "DENIED", r.reason.c_str());
    }

    std::cout << "\n######## Deadlock detection: " << deadlockFile << " ########\n";
    MatrixData d = loadMatrixFile(deadlockFile);
    if (d.request.empty()) throw std::runtime_error(deadlockFile + " has no request section");
    printMatrix("Allocation", d.alloc);
    printMatrix("Request", d.request);
    DetectionResult det = detectDeadlock(d.alloc, d.request, d.available);
    if (det.deadlocked) std::printf("  DEADLOCK: %s\n", pidList(det.deadlocked_pids).c_str());
    else std::printf("  no deadlock, finish order %s\n", pidList(det.finishOrder).c_str());
    RagGraph g = buildRag(d.alloc, d.request);
    if (!g.cycleNodes.empty()) {
        std::printf("  RAG cycle:");
        for (int v : g.cycleNodes)
            std::printf(" %s%d", v < g.nProcs ? "P" : "R", v < g.nProcs ? v : v - g.nProcs);
        std::printf("\n");
    }
    for (const RecoveryStep& st : recoverByTermination(d.alloc, d.request, d.available))
        std::printf("  recovery: kill P%d, release %s -> %s\n", st.killedPid, vecToString(st.released).c_str(),
                    st.after.deadlocked ? ("still deadlocked: " + pidList(st.after.deadlocked_pids)).c_str()
                                        : "deadlock cleared");
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "all";
    try {
        if (cmd == "sched") {
            if (argc > 2) demoScheduling(argv[2], argc > 3 ? std::stoi(argv[3]) : 4);
            else for (const char* f : {"samples/processes_fcfs.csv", "samples/processes_srtf.csv",
                                       "samples/processes_priority.csv"}) demoScheduling(f, 4);
        } else if (cmd == "memory") {
            if (argc > 2) demoMemory(refArg(argv[2]), argc > 3 ? std::stoi(argv[3]) : 3);
            else {
                demoMemory(readTextFile("samples/refstring_standard.txt"), 3);
                demoMemory(readTextFile("samples/refstring_belady.txt"), 3);
                demoMemory(readTextFile("samples/refstring_belady.txt"), 4);
            }
        } else if (cmd == "deadlock") {
            demoDeadlock(argc > 2 ? argv[2] : "samples/banker_sample.txt",
                         argc > 3 ? argv[3] : "samples/deadlock_sample.txt");
        } else if (cmd == "all") {
            for (const char* f : {"samples/processes_fcfs.csv", "samples/processes_srtf.csv",
                                  "samples/processes_priority.csv"}) demoScheduling(f, 4);
            demoMemory(readTextFile("samples/refstring_standard.txt"), 3);
            demoDeadlock("samples/banker_sample.txt", "samples/deadlock_sample.txt");
        } else {
            std::cerr << "usage: ossim_console [all | sched [file.csv] [quantum] | memory [refs|file] [frames] | deadlock [banker.txt] [detect.txt]]\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
