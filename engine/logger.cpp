// logger.cpp - pipe() based log channel.
#include "logger.h"
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

// Creates the pipe and makes the read end non-blocking.
PipeLogger::PipeLogger() {
    if (pipe(fds_) != 0) throw std::runtime_error("pipe() failed");
    int flags = fcntl(fds_[0], F_GETFL);
    fcntl(fds_[0], F_SETFL, flags | O_NONBLOCK);
    pthread_mutex_init(&mtx_, nullptr);
}

PipeLogger::~PipeLogger() {
    close(fds_[0]);
    close(fds_[1]);
    pthread_mutex_destroy(&mtx_);
}

// One write() per line: lines shorter than PIPE_BUF are written atomically,
// so lines from different threads can never be interleaved.
void PipeLogger::logWrite(const std::string& line) {
    std::string msg = line.substr(0, PIPE_BUF - 2) + "\n";
    size_t off = 0;
    while (off < msg.size()) {
        ssize_t n = write(fds_[1], msg.data() + off, msg.size() - off);
        if (n < 0) { if (errno == EINTR) continue; return; }
        off += (size_t)n;
    }
}

// Non-blocking read of the pipe; splits the bytes into complete lines.
std::vector<std::string> PipeLogger::drainLog() {
    char buf[4096];
    for (;;) {
        ssize_t n = read(fds_[0], buf, sizeof buf);
        if (n > 0) { partial_.append(buf, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        break;                                     // EAGAIN: pipe is empty
    }
    std::vector<std::string> fresh;
    size_t pos;
    while ((pos = partial_.find('\n')) != std::string::npos) {
        fresh.push_back(partial_.substr(0, pos));
        partial_.erase(0, pos + 1);
    }
    pthread_mutex_lock(&mtx_);
    all_.insert(all_.end(), fresh.begin(), fresh.end());
    pthread_mutex_unlock(&mtx_);
    return fresh;
}

std::vector<std::string> PipeLogger::lines() const {
    pthread_mutex_lock(&mtx_);
    std::vector<std::string> copy = all_;
    pthread_mutex_unlock(&mtx_);
    return copy;
}
