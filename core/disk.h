// disk.h - disk (HDD) scheduling: FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK.
// OS concept: servicing a queue of cylinder requests; algorithm choice changes the
// total head movement. Educational cylinder-distance model (not an SSD or a measured HDD).
//
// Conventions (finite batch, documented in README/REPORT):
//  * SSTF ties (same distance) -> earlier input order (lower request id).
//  * Directional algorithms service requests at the current head position first (distance 0).
//  * SCAN / C-SCAN travel to the physical boundary only when outstanding requests remain
//    behind the head; LOOK / C-LOOK turn (or jump) at the furthest outstanding request.
//  * A boundary visited only for movement is NOT a serviced request.
//  * C-SCAN / C-LOOK circular return (wrap) distance IS counted in the total head movement.
//  * Duplicate cylinders are distinct requests; equal cylinders are served in input order.
#pragma once
#include <string>
#include <vector>

enum class DiskAlgo { FCFS, SSTF, SCAN, CSCAN, LOOK, CLOOK };
const char* diskAlgoName(DiskAlgo);
const std::vector<DiskAlgo>& allDiskAlgos();

enum class DiskDir { Higher, Lower };          // initial direction (toward higher / lower cylinders)
const char* diskDirName(DiskDir);

// Service = arrive at a request and service it; Boundary = transit to the end of the disk;
// Wrap = circular return (C-SCAN: end -> other end; C-LOOK: jump to the furthest request, serviced).
enum class MoveKind { Service, Boundary, Wrap };
const char* moveKindName(MoveKind);

struct DiskConfig {
    int minCyl = 0, maxCyl = 199;
    int head = 53;
    DiskDir dir = DiskDir::Higher;
    std::vector<int> requests;                 // request id = index in this vector
};
// Throws std::invalid_argument with a clear message.
void validateDiskConfig(const DiskConfig&);

struct DiskMove {
    int from = 0, to = 0, distance = 0, cumulative = 0;
    MoveKind kind = MoveKind::Service;
    int requestId = -1;                        // serviced request, -1 = transit only
    bool reversed = false;                     // the head changed direction for this move
    bool movingUp = true;                      // direction of this move (to >= from)
    std::string reason;                        // why this move was chosen
    int pseudoLine = 0;                        // pseudocode line that explains it
};

struct DiskResult {
    DiskAlgo algo = DiskAlgo::FCFS;
    DiskConfig cfg;
    std::vector<DiskMove> moves;
    std::vector<int> order;                    // request ids in service order
    int total = 0;                             // total head movement (cylinders)
    int serviced = 0;
    double average = 0;                        // total / serviced (0 when there are no requests)
};

DiskResult runDisk(DiskAlgo algo, const DiskConfig& cfg);
std::vector<DiskResult> runAllDisk(const DiskConfig& cfg);
// Reproducible random requests (std::mt19937 with the given seed).
std::vector<int> randomDiskRequests(int count, int minCyl, int maxCyl, unsigned seed);
// Pseudocode of each algorithm (the moves' pseudoLine indexes into it).
std::vector<std::string> diskPseudocode(DiskAlgo algo);
// Sample file: lines "range 0 199", "head 53", "direction higher|lower", "requests 98 183 ...",
// '#' comments. Throws std::runtime_error on a bad file.
DiskConfig loadDiskFile(const std::string& path);
