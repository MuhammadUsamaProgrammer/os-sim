// deadlock.h - deadlock avoidance, detection and recovery (OS concept: deadlocks).
// Banker's safety + resource-request algorithm (avoidance), the multi-instance
// detection algorithm, recovery by process termination, and a resource-allocation graph.
#pragma once
#include <string>
#include <vector>

using Matrix = std::vector<std::vector<int>>;
using Vec = std::vector<int>;

struct SafetyStep { int pid; Vec workBefore; Vec workAfter; };
struct SafetyResult { bool safe = false; std::vector<int> sequence; std::vector<SafetyStep> trace; Matrix need; };
// Banker's safety algorithm. Pass-scan order: repeat passes over pids 0..n-1, take a
// process as soon as need <= work and continue the same pass; stop when a pass makes no progress.
SafetyResult bankersSafety(const Matrix& alloc, const Matrix& max, const Vec& avail);

struct RequestResult { bool granted = false; std::string reason; SafetyResult safetyAfter; };
// Resource-request algorithm. Check: request <= need, request <= available, pretend-allocate, run safety, roll back if unsafe.
RequestResult bankersRequest(int pid, const Vec& request, Matrix alloc, const Matrix& max, Vec avail);

struct DetectionResult { bool deadlocked = false; std::vector<int> deadlocked_pids; std::vector<int> finishOrder; };
// Detection algorithm for multi-instance resources (same pass-scan as the safety algorithm,
// but with the current Request matrix instead of Need).
DetectionResult detectDeadlock(const Matrix& alloc, const Matrix& request, const Vec& avail);

struct RecoveryStep { int killedPid; Vec released; DetectionResult after; };
// Terminate deadlocked processes one at a time (lowest total allocation first, tie -> lowest pid) and re-detect.
std::vector<RecoveryStep> recoverByTermination(Matrix alloc, Matrix request, Vec avail);

struct RagEdge { int from; int to; bool isRequest; };  // process nodes 0..n-1; resource nodes n..n+m-1
struct RagGraph { int nProcs = 0, nRes = 0; std::vector<RagEdge> edges; std::vector<int> cycleNodes; };
// Builds the resource-allocation graph and finds one cycle (DFS); cycleNodes lists it in order.
RagGraph buildRag(const Matrix& alloc, const Matrix& request);

// Need = Max - Allocation (throws if a value would be negative).
Matrix computeNeed(const Matrix& alloc, const Matrix& max);
// Formats a vector as "3 3 2".
std::string vecToString(const Vec& v);
