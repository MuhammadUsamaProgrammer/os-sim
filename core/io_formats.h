// io_formats.h - loading and saving sample files (CSV / text).
// Every loader throws std::runtime_error with a clear message on a bad file.
#pragma once
#include <string>
#include <vector>
#include "pcb.h"
#include "deadlock.h"

// Process table CSV: header "pid,arrival,burst,priority,pages".
// Blank lines and lines starting with '#' are ignored.
std::vector<PCB> loadProcessesCsv(const std::string& path);
void saveProcessesCsv(const std::string& path, const std::vector<PCB>& procs);
// Reads a whole text file (used for reference-string samples).
std::string readTextFile(const std::string& path);

// Banker's / detection data. request is empty if the file has no "request:" section.
struct MatrixData {
    int nProcs = 0, nRes = 0;
    Vec available;
    Matrix alloc, max, request;
};
// Format: "# processes=N resources=M" (optional), "available: a b c", then the
// sections "alloc:", "max:" and optionally "request:", each followed by N rows.
MatrixData loadMatrixFile(const std::string& path);

// One scripted request: when process pid has executed runTick ticks it asks for req.
struct ScriptedRequest { int pid; int runTick; Vec req; };
// CSV: "pid,run_tick,r0,r1,..." (header optional).
std::vector<ScriptedRequest> loadRequestsCsv(const std::string& path);
