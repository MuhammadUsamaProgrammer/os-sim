// ipc.h - inter-process communication (M3): shared memory, message passing (direct and
// indirect, bounded FIFO, blocking / non-blocking) and pipes (byte stream, EOF rules).
// Deterministic model: every operation is one event; a blocked operation stays pending and
// completes in a separate, labelled wake-up event. Synchronization ALGORITHMS are not repeated
// here (see the Synchronization tab); shared-memory coordination is a deterministic hand-off.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

enum class IpcMode { SharedMemory, MessagePassing, Pipe };
const char* ipcModeName(IpcMode);

enum class IpcOpKind { ShmCreate, ShmAttach, ShmWrite, ShmRead, ShmDetach, ShmRemove, Send, Receive, PipeWrite, PipeRead, PipeClose };
const char* ipcOpName(IpcOpKind);

struct IpcOp {
    IpcOpKind kind = IpcOpKind::Send;
    int proc = 0;               // process that performs the operation
    int peer = -1;              // direct message passing: receiver (Send) / sender (Receive)
    int offset = 0;             // shared memory offset
    int length = 0;             // bytes to read
    std::string data;           // bytes / message to write or send
    bool nonBlocking = false;
    bool readOnly = false;      // ShmAttach
    bool writeEnd = false;      // PipeClose: which end
};

struct IpcConfig {
    IpcMode mode = IpcMode::SharedMemory;
    std::vector<std::string> procs = {"server", "client"};
    int shmSize = 32, shmId = 4711;    // modelled segment id (the Live demo shows a real one)
    int queueCapacity = 2;             // messages per mailbox / link
    bool direct = false;               // false = mailbox (indirect), true = named sender / receiver
    int mailboxId = 7;
    int pipeCapacity = 16;             // modelled bytes (NOT the real Linux pipe capacity)
    bool namedPipe = false;            // FIFO in the file system vs anonymous pipe
};
void validateIpcConfig(const IpcConfig&);

struct IpcMessage { int from = 0, to = -1; std::string text; };

struct IpcProcState {
    std::string name;
    bool blocked = false;
    std::string blockedOn;             // e.g. "receive on empty mailbox 7"
    bool attached = false, attachReadOnly = false;
    uint64_t attachAddr = 0;           // modelled virtual address of the segment in THIS process
    bool readOpen = true, writeOpen = true;   // pipe ends held by this process
    std::string lastData;              // last bytes / message received or read
    int sent = 0, received = 0, bytesOut = 0, bytesIn = 0;
};

struct IpcTransfer { int from = -1, to = -1; std::string data; bool toObject = false, fromObject = false; };

struct IpcSnapshot {
    int step = 0;
    std::string event;                 // explanation
    bool failed = false, wake = false;
    std::vector<IpcProcState> procs;
    // shared memory
    bool shmExists = false, shmRemoved = false;
    int shmAttach = 0;
    std::string shmBytes;              // segment contents ('\0' = empty byte)
    // message passing
    std::deque<IpcMessage> queue;      // mailbox, or all direct links merged in FIFO order
    // pipe
    std::string pipeBuf;
    IpcTransfer transfer;              // what moved in this event (for the animation)
};

class IpcSim {
public:
    explicit IpcSim(const IpcConfig& c);
    // Applies one operation; returns "" or the error (the failed attempt is still recorded).
    std::string apply(const IpcOp& op);
    const std::vector<IpcSnapshot>& trace() const { return trace_; }
    const IpcConfig& config() const { return cfg_; }
    const std::vector<IpcOp>& applied() const { return applied_; }   // operations issued so far
    void truncate(int n);              // keep the first n+1 snapshots (new branch after stepping back)
private:
    std::string doApply(const IpcOp& op, bool retry);
    void wakePending();
    void record(const std::string& text, bool failed = false, bool wake = false, IpcTransfer t = {});
    std::string pname(int p) const;
    IpcConfig cfg_;
    std::vector<IpcProcState> procs_;
    bool shmExists_ = false, shmRemoved_ = false;
    int shmAttach_ = 0;
    std::string shm_;
    std::deque<IpcMessage> queue_;
    std::string pipe_;
    std::vector<std::pair<int, IpcOp>> pending_;   // blocked operations in blocking order
    std::vector<IpcSnapshot> trace_;
    std::vector<IpcOp> applied_;                   // operations in order (for truncate / replay)
};

struct IpcPreset { std::string name, description; IpcConfig cfg; std::vector<IpcOp> ops; };
std::vector<IpcPreset> ipcPresets();
