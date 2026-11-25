// logger.h - IPC logger built on a POSIX pipe (OS concept: inter-process communication).
// Producer threads write() one '\n'-terminated line (< PIPE_BUF bytes, so each write
// is atomic) into the pipe; the dispatcher reads the other end (O_NONBLOCK) and
// collects complete lines.
#pragma once
#include <pthread.h>
#include <string>
#include <vector>

class PipeLogger {
public:
    PipeLogger();                         // creates the pipe
    ~PipeLogger();                        // closes both ends
    PipeLogger(const PipeLogger&) = delete;
    PipeLogger& operator=(const PipeLogger&) = delete;

    // Writes one line into the pipe (adds the '\n'). Safe from any thread.
    void logWrite(const std::string& line);
    // Reads everything currently in the pipe and returns the new complete lines
    // (they are also appended to the full log). Call from the dispatcher only.
    std::vector<std::string> drainLog();
    // Copy of every line drained so far (thread-safe).
    std::vector<std::string> lines() const;

private:
    int fds_[2] = {-1, -1};               // [0] = read end, [1] = write end
    std::string partial_;                 // bytes of an incomplete line
    std::vector<std::string> all_;        // full log
    mutable pthread_mutex_t mtx_;         // protects all_
};
