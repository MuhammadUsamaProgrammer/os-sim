// io_formats.cpp - simple, strict file parsers for the sample files.
#include "io_formats.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
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

// Parses whitespace-separated integers, or throws with the file position.
static Vec parseSpaceInts(const std::string& text, const std::string& where) {
    std::stringstream ss(text);
    std::string tok;
    Vec v;
    while (ss >> tok) {
        size_t used = 0;
        int x = 0;
        try { x = std::stoi(tok, &used); } catch (const std::exception&) { used = 0; }
        if (used == 0 || used != tok.size() || x < 0)
            throw std::runtime_error(where + ": '" + tok + "' is not a non-negative integer");
        v.push_back(x);
    }
    return v;
}

// Loads a Banker's / detection matrix file.
MatrixData loadMatrixFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    MatrixData d;
    int declaredN = -1, declaredM = -1;
    Matrix* section = nullptr;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string where = path + " line " + std::to_string(lineNo);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') {                                 // optional size header
            size_t p = line.find("processes="), r = line.find("resources=");
            if (p != std::string::npos) declaredN = std::atoi(line.c_str() + p + 10);
            if (r != std::string::npos) declaredM = std::atoi(line.c_str() + r + 10);
            continue;
        }
        size_t colon = line.find(':');
        if (colon != std::string::npos) {                     // section header
            std::string key = trim(line.substr(0, colon));
            std::string rest = trim(line.substr(colon + 1));
            if (key == "available") { d.available = parseSpaceInts(rest, where); section = nullptr; continue; }
            if (key == "alloc" || key == "allocation") section = &d.alloc;
            else if (key == "max") section = &d.max;
            else if (key == "request") section = &d.request;
            else throw std::runtime_error(where + ": unknown section '" + key + "'");
            if (!rest.empty()) section->push_back(parseSpaceInts(rest, where));
            continue;
        }
        if (!section) throw std::runtime_error(where + ": numbers outside a section");
        section->push_back(parseSpaceInts(line, where));
    }
    if (d.available.empty()) throw std::runtime_error(path + ": missing 'available:' line");
    if (d.alloc.empty()) throw std::runtime_error(path + ": missing 'alloc:' section");
    d.nRes = (int)d.available.size();
    d.nProcs = (int)d.alloc.size();
    if (declaredN >= 0 && declaredN != d.nProcs)
        throw std::runtime_error(path + ": header says " + std::to_string(declaredN) + " processes but alloc has " + std::to_string(d.nProcs) + " rows");
    if (declaredM >= 0 && declaredM != d.nRes)
        throw std::runtime_error(path + ": header says " + std::to_string(declaredM) + " resources but available has " + std::to_string(d.nRes));
    if (d.max.empty()) d.max = d.alloc;                       // detection-only files
    auto check = [&](const Matrix& m, const char* name) {
        if (m.empty()) return;
        if ((int)m.size() != d.nProcs)
            throw std::runtime_error(path + ": section " + name + " needs " + std::to_string(d.nProcs) + " rows");
        for (const Vec& r : m)
            if ((int)r.size() != d.nRes)
                throw std::runtime_error(path + ": every row of " + name + " needs " + std::to_string(d.nRes) + " values");
    };
    check(d.alloc, "alloc"); check(d.max, "max"); check(d.request, "request");
    return d;
}

// Loads scripted resource requests for the integrated run.
std::vector<ScriptedRequest> loadRequestsCsv(const std::string& path, Vec* total) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::vector<ScriptedRequest> out;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        std::string where = path + " line " + std::to_string(lineNo);
        size_t tp = line.find("total=");
        if (!line.empty() && line[0] == '#' && tp != std::string::npos) {
            Vec tot = parseSpaceInts(line.substr(tp + 6), where);
            if (total) *total = tot;
            continue;
        }
        if (line.empty() || line[0] == '#' || line.rfind("pid", 0) == 0) continue;
        Vec f = parseCsvInts(line, where);
        if (f.size() < 3) throw std::runtime_error(where + ": expected pid,run_tick,r0[,r1...]");
        ScriptedRequest r{f[0], f[1], Vec(f.begin() + 2, f.end())};
        if (r.runTick < 0) throw std::runtime_error(where + ": run_tick must be >= 0");
        for (int x : r.req) if (x < 0) throw std::runtime_error(where + ": negative request");
        if (!out.empty() && out[0].req.size() != r.req.size())
            throw std::runtime_error(where + ": all requests need the same number of resources");
        out.push_back(r);
    }
    if (total && !total->empty() && !out.empty() && total->size() != out[0].req.size())
        throw std::runtime_error(path + ": total= has a different number of resources than the requests");
    return out;
}
