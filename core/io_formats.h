// io_formats.h - loading and saving sample files (CSV / text).
// Every loader throws std::runtime_error with a clear message on a bad file.
#pragma once
#include <string>
#include <vector>
#include "pcb.h"

// Process table CSV: header "pid,arrival,burst,priority,pages".
// Blank lines and lines starting with '#' are ignored.
std::vector<PCB> loadProcessesCsv(const std::string& path);
void saveProcessesCsv(const std::string& path, const std::vector<PCB>& procs);
// Reads a whole text file (used for reference-string samples).
std::string readTextFile(const std::string& path);
