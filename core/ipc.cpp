// ipc.cpp - deterministic IPC model (shared memory, message passing, pipes).
#include "ipc.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

const char* ipcModeName(IpcMode m) {
    switch (m) {
        case IpcMode::SharedMemory: return "Shared memory";
        case IpcMode::MessagePassing: return "Message passing";
        case IpcMode::Pipe: return "Pipe";
    }
    return "?";
}

const char* ipcOpName(IpcOpKind k) {
    switch (k) {
        case IpcOpKind::ShmCreate: return "shmget (create)";
        case IpcOpKind::ShmAttach: return "shmat (attach)";
        case IpcOpKind::ShmWrite: return "write to segment";
        case IpcOpKind::ShmRead: return "read from segment";
        case IpcOpKind::ShmDetach: return "shmdt (detach)";
        case IpcOpKind::ShmRemove: return "shmctl IPC_RMID (remove)";
        case IpcOpKind::Send: return "send";
        case IpcOpKind::Receive: return "receive";
        case IpcOpKind::PipeWrite: return "write()";
        case IpcOpKind::PipeRead: return "read()";
        case IpcOpKind::PipeClose: return "close()";
    }
    return "?";
}

void validateIpcConfig(const IpcConfig& c) {
    auto need = [](bool ok, const std::string& m) { if (!ok) throw std::invalid_argument(m); };
    need(c.procs.size() >= 2 && c.procs.size() <= 4, "2 to 4 processes");
    need(c.shmSize >= 1 && c.shmSize <= 256, "segment size must be 1..256 bytes");
    need(c.queueCapacity >= 1 && c.queueCapacity <= 16, "queue capacity must be 1..16 messages");
    need(c.pipeCapacity >= 1 && c.pipeCapacity <= 256, "modelled pipe capacity must be 1..256 bytes");
}

IpcSim::IpcSim(const IpcConfig& c) : cfg_(c) {
    validateIpcConfig(c);
    for (size_t i = 0; i < c.procs.size(); ++i) {
        IpcProcState p;
        p.name = c.procs[i];
        procs_.push_back(p);
    }
    std::string start;
    if (c.mode == IpcMode::Pipe)
        start = c.namedPipe ? "A named pipe (FIFO) exists in the file system; each process opened both ends"
                            : "pipe() created one read end and one write end; after fork() parent and child hold both";
    else if (c.mode == IpcMode::MessagePassing)
        start = c.direct ? "Direct communication: each send names its receiver, each receive names its sender (capacity " +
                               std::to_string(c.queueCapacity) + " per link)"
                         : "Indirect communication through mailbox " + std::to_string(c.mailboxId) + " (capacity " +
                               std::to_string(c.queueCapacity) + ", FIFO)";
    else
        start = "No shared segment yet: the server creates it";
    record(start);
}

std::string IpcSim::pname(int p) const { return p >= 0 && p < (int)procs_.size() ? procs_[p].name : "?"; }

void IpcSim::record(const std::string& text, bool failed, bool wake, IpcTransfer t) {
    IpcSnapshot s;
    s.step = (int)trace_.size();
    s.event = text;
    s.failed = failed;
    s.wake = wake;
    s.procs = procs_;
    s.shmExists = shmExists_;
    s.shmRemoved = shmRemoved_;
    s.shmAttach = shmAttach_;
    s.shmBytes = shm_;
    s.queue = queue_;
    s.pipeBuf = pipe_;
    s.transfer = t;
    trace_.push_back(s);
}

void IpcSim::truncate(int n) {
    if (n < 0 || n + 1 >= (int)trace_.size()) return;
    // replay: rebuild from scratch with the operations that produced the first n events
    IpcConfig c = cfg_;
    std::vector<IpcOp> ops = applied_;
    IpcSim fresh(c);
    for (const IpcOp& op : ops) {
        if ((int)fresh.trace_.size() > n) break;
        fresh.apply(op);
    }
    while ((int)fresh.trace_.size() > n + 1) fresh.trace_.pop_back();
    // the replay may stop in the middle of an operation's wake-ups: keep its state consistent
    *this = fresh;
    const IpcSnapshot& s = trace_.back();
    procs_ = s.procs;
    shmExists_ = s.shmExists;
    shmRemoved_ = s.shmRemoved;
    shmAttach_ = s.shmAttach;
    shm_ = s.shmBytes;
    queue_ = s.queue;
    pipe_ = s.pipeBuf;
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const std::pair<int, IpcOp>& pr) {
        return !procs_[pr.first].blocked;
    }), pending_.end());
}

std::string IpcSim::apply(const IpcOp& op) {
    if (op.proc < 0 || op.proc >= (int)procs_.size()) {
        record("invalid process index", true);
        return "invalid process";
    }
    applied_.push_back(op);
    if (procs_[op.proc].blocked) {
        std::string e = pname(op.proc) + " is blocked (" + procs_[op.proc].blockedOn + ") and cannot issue another operation";
        record(e, true);
        return e;
    }
    std::string e = doApply(op, false);
    wakePending();
    return e;
}

// Completes pending (blocked) operations that can now proceed, in blocking order.
void IpcSim::wakePending() {
    bool progress = true;
    while (progress) {
        progress = false;
        for (size_t i = 0; i < pending_.size(); ++i) {
            auto [p, op] = pending_[i];
            std::string r = doApply(op, true);
            if (r != "blocked") {
                pending_.erase(pending_.begin() + i);
                progress = true;
                break;
            }
        }
    }
}

static std::string show(const std::string& s) {
    std::string o;
    for (char c : s) o += c == '\0' ? '.' : c;
    return o;
}

// retry = this is a pending operation being re-tried: it either completes (wake event) or returns "blocked".
std::string IpcSim::doApply(const IpcOp& op, bool retry) {
    IpcProcState& P = procs_[op.proc];
    const std::string me = P.name;
    auto fail = [&](const std::string& why) { record(me + ": " + ipcOpName(op.kind) + " failed - " + why, true); return why; };
    auto block = [&](const std::string& on) -> std::string {
        if (retry) return "blocked";
        P.blocked = true;
        P.blockedOn = on;
        pending_.push_back({op.proc, op});
        record(me + " blocks: " + on);
        return "";
    };
    auto unblock = [&]() { P.blocked = false; P.blockedOn.clear(); };

    switch (op.kind) {
        // ---------------------------------------------------------------- shared memory
        case IpcOpKind::ShmCreate:
            if (cfg_.mode != IpcMode::SharedMemory) return fail("not in shared-memory mode");
            if (shmExists_) return fail("segment " + std::to_string(cfg_.shmId) + " already exists");
            shmExists_ = true;
            shmRemoved_ = false;
            shm_.assign(cfg_.shmSize, '\0');
            record(me + " creates shared segment id " + std::to_string(cfg_.shmId) + " (" + std::to_string(cfg_.shmSize) +
                   " bytes, permissions 0600 - illustrative)");
            return "";
        case IpcOpKind::ShmAttach: {
            if (!shmExists_) return fail("no segment with id " + std::to_string(cfg_.shmId));
            if (shmRemoved_) return fail("segment is marked for removal: new attaches are refused");
            if (P.attached) return fail("already attached");
            P.attached = true;
            P.attachReadOnly = op.readOnly;
            P.attachAddr = 0x7f0000000000ULL + (uint64_t)(op.proc + 1) * 0x2a5000ULL;   // differs per process (modelled)
            ++shmAttach_;
            char buf[32];
            snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)P.attachAddr);
            record(me + " attaches the segment " + (op.readOnly ? "read-only" : "read-write") + " at virtual address " + buf +
                   " (other processes see the SAME bytes at a different address); attachments " + std::to_string(shmAttach_));
            return "";
        }
        case IpcOpKind::ShmWrite: {
            if (!P.attached) return fail("not attached to the segment");
            if (P.attachReadOnly) return fail("attached read-only: write not permitted");
            if (op.offset < 0 || op.data.empty() || op.offset + (int)op.data.size() > cfg_.shmSize)
                return fail("offset " + std::to_string(op.offset) + " + " + std::to_string(op.data.size()) + " bytes is outside the segment (0.." +
                            std::to_string(cfg_.shmSize - 1) + ")");
            shm_.replace(op.offset, op.data.size(), op.data);
            P.bytesOut += (int)op.data.size();
            record(me + " writes \"" + op.data + "\" at offset " + std::to_string(op.offset) + " (plain memory store, no system call per byte)",
                   false, false, {op.proc, -1, op.data, true, false});
            return "";
        }
        case IpcOpKind::ShmRead: {
            if (!P.attached) return fail("not attached to the segment");
            if (op.offset < 0 || op.length <= 0 || op.offset + op.length > cfg_.shmSize)
                return fail("offset " + std::to_string(op.offset) + ", length " + std::to_string(op.length) + " is outside the segment");
            std::string got = shm_.substr(op.offset, op.length);
            P.lastData = got;
            P.bytesIn += op.length;
            record(me + " reads " + std::to_string(op.length) + " bytes at offset " + std::to_string(op.offset) + ": \"" + show(got) + "\"",
                   false, false, {-1, op.proc, show(got), false, true});
            return "";
        }
        case IpcOpKind::ShmDetach:
            if (!P.attached) return fail("not attached");
            P.attached = false;
            --shmAttach_;
            record(me + " detaches; attachments " + std::to_string(shmAttach_));
            if (shmRemoved_ && shmAttach_ == 0) {
                shmExists_ = false;
                shm_.clear();
                record("last detach of a segment marked for removal: the segment is destroyed");
            }
            return "";
        case IpcOpKind::ShmRemove:
            if (!shmExists_) return fail("no segment to remove");
            shmRemoved_ = true;
            if (shmAttach_ == 0) {
                shmExists_ = false;
                shm_.clear();
                record(me + " removes the segment (IPC_RMID): no attachments, destroyed now");
            } else {
                record(me + " marks the segment for removal (IPC_RMID): destroyed after the last detach (" + std::to_string(shmAttach_) +
                       " attached)");
            }
            return "";
        // ---------------------------------------------------------------- message passing
        case IpcOpKind::Send: {
            if (cfg_.mode != IpcMode::MessagePassing) return fail("not in message-passing mode");
            if (op.data.empty()) return fail("empty message");
            if (cfg_.direct && (op.peer < 0 || op.peer >= (int)procs_.size() || op.peer == op.proc))
                return fail("direct send must name another process as receiver");
            int inLink = 0;
            for (const IpcMessage& m : queue_) if (!cfg_.direct || (m.from == op.proc && m.to == op.peer)) ++inLink;
            std::string where = cfg_.direct ? "link " + me + " -> " + pname(op.peer) : "mailbox " + std::to_string(cfg_.mailboxId);
            if (inLink >= cfg_.queueCapacity) {
                if (op.nonBlocking) return fail(where + " is full: non-blocking send returns an error immediately (not blocked)");
                return block("send on full " + where);
            }
            queue_.push_back({op.proc, cfg_.direct ? op.peer : -1, op.data});
            ++P.sent;
            if (retry) unblock();
            record(std::string(retry ? "WAKE-UP: " : "") + me + " sends \"" + op.data + "\" to " + where + " (" +
                       std::to_string(inLink + 1) + "/" + std::to_string(cfg_.queueCapacity) + ")",
                   false, retry, {op.proc, -1, op.data, true, false});
            return "";
        }
        case IpcOpKind::Receive: {
            if (cfg_.mode != IpcMode::MessagePassing) return fail("not in message-passing mode");
            if (cfg_.direct && (op.peer < 0 || op.peer >= (int)procs_.size() || op.peer == op.proc))
                return fail("direct receive must name the sender");
            auto it = std::find_if(queue_.begin(), queue_.end(), [&](const IpcMessage& m) {
                return !cfg_.direct || (m.from == op.peer && m.to == op.proc);
            });
            std::string where = cfg_.direct ? "link " + pname(op.peer) + " -> " + me : "mailbox " + std::to_string(cfg_.mailboxId);
            if (it == queue_.end()) {
                if (op.nonBlocking) return fail(where + " is empty: non-blocking receive fails at once (ENOMSG-like), not blocked");
                return block("receive on empty " + where);
            }
            IpcMessage m = *it;
            queue_.erase(it);
            P.lastData = m.text;
            ++P.received;
            if (retry) unblock();
            record(std::string(retry ? "WAKE-UP: " : "") + me + " receives \"" + m.text + "\" from " + pname(m.from) + " (FIFO: oldest first)",
                   false, retry, {-1, op.proc, m.text, false, true});
            return "";
        }
        // ---------------------------------------------------------------- pipe
        case IpcOpKind::PipeWrite: {
            if (cfg_.mode != IpcMode::Pipe) return fail("not in pipe mode");
            if (!P.writeOpen) return fail("this process closed its write end");
            bool anyReader = false;
            for (const IpcProcState& q : procs_) anyReader = anyReader || q.readOpen;
            if (!anyReader) return fail("no read end is open: EPIPE (the writer would also get SIGPIPE)");
            if (op.data.empty()) return fail("nothing to write");
            int space = cfg_.pipeCapacity - (int)pipe_.size();
            if (space <= 0) {
                if (op.nonBlocking) return fail("pipe full: non-blocking write returns EAGAIN");
                return block("write on a full pipe (" + std::to_string(op.data.size()) + " bytes waiting)");
            }
            int n = std::min<int>(space, (int)op.data.size());
            pipe_ += op.data.substr(0, n);
            P.bytesOut += n;
            if (retry) unblock();
            record(std::string(retry ? "WAKE-UP: " : "") + me + " writes " + std::to_string(n) + " bytes \"" + op.data.substr(0, n) +
                       "\" into the pipe (" + std::to_string(pipe_.size()) + "/" + std::to_string(cfg_.pipeCapacity) + " modelled bytes)",
                   false, retry, {op.proc, -1, op.data.substr(0, n), true, false});
            if (n < (int)op.data.size()) {               // the rest waits for space
                IpcOp rest = op;
                rest.data = op.data.substr(n);
                if (op.nonBlocking) { record(me + ": partial non-blocking write, " + std::to_string(rest.data.size()) + " bytes not written", true); return ""; }
                P.blocked = true;
                P.blockedOn = "write on a full pipe (" + std::to_string(rest.data.size()) + " bytes waiting)";
                pending_.push_back({op.proc, rest});
                record(me + " blocks: pipe full, " + std::to_string(rest.data.size()) + " bytes still to write");
            }
            return "";
        }
        case IpcOpKind::PipeRead: {
            if (cfg_.mode != IpcMode::Pipe) return fail("not in pipe mode");
            if (!P.readOpen) return fail("this process closed its read end");
            if (op.length <= 0) return fail("read length must be positive");
            if (pipe_.empty()) {
                bool anyWriter = false;
                for (const IpcProcState& q : procs_) anyWriter = anyWriter || q.writeOpen;
                if (!anyWriter) {
                    P.lastData.clear();
                    if (retry) unblock();
                    record(std::string(retry ? "WAKE-UP: " : "") + me + " read() returns 0 = END OF FILE: the pipe is empty and every write end is closed",
                           false, retry);
                    return "";
                }
                if (op.nonBlocking) return fail("pipe empty but a writer is open: non-blocking read returns EAGAIN");
                return block("read on an empty pipe (a write end is still open, so this is not EOF)");
            }
            int n = std::min<int>(op.length, (int)pipe_.size());
            std::string got = pipe_.substr(0, n);
            pipe_.erase(0, n);
            P.lastData = got;
            P.bytesIn += n;
            if (retry) unblock();
            record(std::string(retry ? "WAKE-UP: " : "") + me + " read(" + std::to_string(op.length) + ") returns " + std::to_string(n) +
                       " bytes \"" + got + "\" (byte stream: no message boundaries are kept)",
                   false, retry, {-1, op.proc, got, false, true});
            return "";
        }
        case IpcOpKind::PipeClose:
            if (cfg_.mode != IpcMode::Pipe) return fail("not in pipe mode");
            if (op.writeEnd) {
                if (!P.writeOpen) return fail("write end already closed");
                P.writeOpen = false;
            } else {
                if (!P.readOpen) return fail("read end already closed");
                P.readOpen = false;
            }
            record(me + " closes its " + (op.writeEnd ? "write" : "read") + " end");
            return "";
    }
    return fail("unknown operation");
}

static IpcOp mk(IpcOpKind k, int p, std::string data = "", int off = 0, int len = 0, int peer = -1, bool nb = false) {
    IpcOp o;
    o.kind = k; o.proc = p; o.data = data; o.offset = off; o.length = len; o.peer = peer; o.nonBlocking = nb;
    return o;
}

std::vector<IpcPreset> ipcPresets() {
    using K = IpcOpKind;
    std::vector<IpcPreset> v;
    IpcConfig shm;
    shm.mode = IpcMode::SharedMemory;
    IpcOp att = mk(K::ShmAttach, 1);
    att.readOnly = true;
    v.push_back({"Shared memory: server writes HELLO, client reads it",
                 "Both processes attach the SAME segment (at different virtual addresses). The server writes, then (deterministic "
                 "hand-off) the client reads. Real programs coordinate with a semaphore - see the Synchronization tab.",
                 shm,
                 {mk(K::ShmCreate, 0), mk(K::ShmAttach, 0), mk(K::ShmWrite, 0, "HELLO"), att, mk(K::ShmRead, 1, "", 0, 5),
                  mk(K::ShmDetach, 1), mk(K::ShmDetach, 0), mk(K::ShmRemove, 0)}});
    IpcConfig shmErr = shm;
    shmErr.shmSize = 16;
    v.push_back({"Shared memory errors: bad offset, read-only, removal",
                 "Writing past the end is refused, a read-only attachment cannot write, and a segment marked for removal accepts no new attach.",
                 shmErr,
                 {mk(K::ShmCreate, 0), mk(K::ShmAttach, 0), mk(K::ShmWrite, 0, "TOO-LONG-FOR-IT", 8), att, mk(K::ShmWrite, 1, "X"),
                  mk(K::ShmRemove, 0), mk(K::ShmDetach, 1), mk(K::ShmAttach, 1), mk(K::ShmDetach, 0)}});
    IpcConfig mb;
    mb.mode = IpcMode::MessagePassing;
    mb.procs = {"P1", "P2"};
    mb.queueCapacity = 2;
    v.push_back({"Mailbox, capacity 2, two messages (FIFO)",
                 "Two sends fill the mailbox; a third NON-blocking send fails at once; the receiver gets the messages in FIFO order.",
                 mb,
                 {mk(K::Send, 0, "first"), mk(K::Send, 0, "second"), mk(K::Send, 0, "third", 0, 0, -1, true), mk(K::Receive, 1),
                  mk(K::Receive, 1), mk(K::Receive, 1, "", 0, 0, -1, true)}});
    v.push_back({"Receiver blocks on an empty mailbox",
                 "The receiver blocks (it does not fail); the send delivers the message and wakes it up.",
                 mb, {mk(K::Receive, 1), mk(K::Send, 0, "wake up")}});
    IpcConfig dir = mb;
    dir.direct = true;
    dir.queueCapacity = 1;
    dir.procs = {"P1", "P2", "P3"};
    v.push_back({"Direct communication, capacity 1: sender blocks",
                 "P1 names P2 explicitly. The link holds one message, so the second blocking send blocks until P2 receives.",
                 dir,
                 {mk(K::Send, 0, "a", 0, 0, 1), mk(K::Send, 0, "b", 0, 0, 1), mk(K::Receive, 1, "", 0, 0, 0),
                  mk(K::Receive, 1, "", 0, 0, 0), mk(K::Receive, 2, "", 0, 0, 0, true)}});
    IpcConfig pp;
    pp.mode = IpcMode::Pipe;
    pp.procs = {"parent", "child"};
    IpcOp cr = mk(K::PipeClose, 1);           // child closes its read end
    IpcOp pw = mk(K::PipeClose, 0);
    pw.writeEnd = true;                       // parent closes its write end
    IpcOp cw = mk(K::PipeClose, 1);
    cw.writeEnd = true;
    v.push_back({"Anonymous pipe: short string, then EOF",
                 "The child writes, the parent reads in small pieces (no message boundaries), and gets EOF only after every write end is closed and the data is consumed.",
                 pp,
                 {cr, pw, mk(K::PipeWrite, 1, "hello pipe"), mk(K::PipeRead, 0, "", 0, 4), cw, mk(K::PipeRead, 0, "", 0, 16), mk(K::PipeRead, 0, "", 0, 16)}});
    IpcConfig full = pp;
    full.pipeCapacity = 8;
    v.push_back({"Pipe full: the writer blocks (modelled capacity 8)",
                 "10 bytes do not fit into the modelled 8-byte buffer: 8 are written, the writer blocks, and continues after the reader frees space.",
                 full, {cr, pw, mk(K::PipeWrite, 1, "ABCDEFGHIJ"), mk(K::PipeRead, 0, "", 0, 4), mk(K::PipeRead, 0, "", 0, 8)}});
    IpcConfig empty = pp;
    v.push_back({"Empty pipe with an open writer: read blocks, it is not EOF",
                 "The reader blocks because a write end is still open; closing the last write end wakes it with EOF.",
                 empty, {cr, pw, mk(K::PipeRead, 0, "", 0, 4), cw}});
    return v;
}
