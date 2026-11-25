// io_formats.cpp - simple, strict file parsers for the sample files.
#include "io_formats.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

// Removes leading/trailing whitespace.
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Splits a line on commas and parses each field as an int.
static std::vector<int> parseCsvInts(const std::string& line, const std::string& where) {
    std::vector<int> v;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        try {
            size_t used = 0;
            int x = std::stoi(tok, &used);
            if (used != tok.size()) throw std::invalid_argument("junk");
            v.push_back(x);
        } catch (const std::exception&) {
            throw std::runtime_error(where + ": '" + tok + "' is not an integer");
        }
    }
    return v;
}

// Loads processes from CSV (pid,arrival,burst,priority,pages).
std::vector<PCB> loadProcessesCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<PCB> procs;
    std::string line;
    int lineNo = 0;
    bool headerSeen = false;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (!headerSeen) {
            headerSeen = true;
            if (line.rfind("pid", 0) == 0) continue;     // header row
        }
        std::string where = path + " line " + std::to_string(lineNo);
        std::vector<int> f = parseCsvInts(line, where);
        if (f.size() < 3 || f.size() > 5)
            throw std::runtime_error(where + ": expected pid,arrival,burst[,priority[,pages]]");
        PCB p;
        p.pid = f[0]; p.arrival = f[1]; p.burst = f[2]; p.remaining = f[2];
        if (f.size() > 3) p.priority = f[3];
        if (f.size() > 4) p.pages = f[4];
        procs.push_back(p);
    }
    if (procs.empty()) throw std::runtime_error(path + ": no processes found");
    return procs;
}

// Writes processes in the same CSV format the loader reads.
void saveProcessesCsv(const std::string& path, const std::vector<PCB>& procs) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << "pid,arrival,burst,priority,pages\n";
    for (const PCB& p : procs)
        out << p.pid << ',' << p.arrival << ',' << p.burst << ',' << p.priority << ',' << p.pages << '\n';
}

// Returns the whole content of a text file.
std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
