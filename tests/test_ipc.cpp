// test_ipc.cpp - M3 IPC model (core/ipc.*): every preset, FIFO order, exact payloads,
// bounded capacity, blocking vs non-blocking, endpoint closure / EOF, detach / removal,
// invalid offsets and processes, replay after stepping back.
#include "check.h"
#include "ipc.h"

static IpcOp op(IpcOpKind k, int proc) {
    IpcOp o;
    o.kind = k;
    o.proc = proc;
    return o;
}

static IpcSim runPreset(const IpcPreset& p) {
    IpcSim s(p.cfg);
    for (const IpcOp& op : p.ops) s.apply(op);
    return s;
}
static int countFailed(const IpcSim& s) { int n = 0; for (const IpcSnapshot& x : s.trace()) n += x.failed; return n; }
static int countWake(const IpcSim& s) { int n = 0; for (const IpcSnapshot& x : s.trace()) n += x.wake; return n; }

int main() {
    std::vector<IpcPreset> pr = ipcPresets();
    CHECK(pr.size() == 8);

    // 1. shared memory: HELLO preserved, different virtual addresses, destroyed at the end
    IpcSim shm = runPreset(pr[0]);
    CHECK(countFailed(shm) == 0);
    std::string clientRead;
    unsigned long long a0 = 0, a1 = 0;
    for (const IpcSnapshot& s : shm.trace()) {
        if (s.procs[1].lastData.size()) clientRead = s.procs[1].lastData;
        if (s.procs[0].attached) a0 = s.procs[0].attachAddr;
        if (s.procs[1].attached) a1 = s.procs[1].attachAddr;
    }
    CHECK(clientRead == "HELLO");
    CHECK(a0 && a1 && a0 != a1);
    CHECK(!shm.trace().back().shmExists && shm.trace().back().shmAttach == 0);
    bool sawBoth = false;
    for (const IpcSnapshot& s : shm.trace()) sawBoth = sawBoth || s.shmAttach == 2;
    CHECK(sawBoth);

    // 2. shared-memory errors: bad offset, read-only write, attach after removal; destroyed after last detach
    IpcSim err = runPreset(pr[1]);
    CHECK(countFailed(err) == 3);
    CHECK(!err.trace().back().shmExists);
    {
        IpcConfig c;
        IpcSim s(c);
        CHECK(s.apply(op(IpcOpKind::ShmAttach, 0)) != "");            // no segment yet
        s.apply(op(IpcOpKind::ShmCreate, 0));
        CHECK(s.apply(op(IpcOpKind::ShmCreate, 0)) != "");            // duplicate id
        s.apply(op(IpcOpKind::ShmAttach, 0));
        IpcOp neg = op(IpcOpKind::ShmWrite, 0);
        neg.offset = -1;
        neg.data = "x";
        CHECK(s.apply(neg) != "");
        IpcOp rd = op(IpcOpKind::ShmRead, 1);
        rd.length = 1;
        CHECK(s.apply(rd) != "");                                    // client not attached
        IpcOp bad;
        bad.proc = 9;
        CHECK(s.apply(bad) != "");                                   // invalid process
    }

    // 3. mailbox capacity 2: non-blocking send on full fails, FIFO order, non-blocking receive on empty fails
    IpcSim mb = runPreset(pr[2]);
    CHECK(countFailed(mb) == 2 && countWake(mb) == 0);
    std::vector<std::string> got;
    std::string last;
    for (const IpcSnapshot& s : mb.trace())
        if (s.procs[1].lastData != last) { last = s.procs[1].lastData; got.push_back(last); }
    CHECK((got == std::vector<std::string>{"first", "second"}));
    int maxQ = 0;
    for (const IpcSnapshot& s : mb.trace()) maxQ = std::max(maxQ, (int)s.queue.size());
    CHECK(maxQ == 2);                                                // never above capacity

    // 4. receiver blocks on an empty mailbox, then wakes with the message
    IpcSim blk = runPreset(pr[3]);
    CHECK(countFailed(blk) == 0 && countWake(blk) == 1);
    CHECK(blk.trace()[1].procs[1].blocked && !blk.trace().back().procs[1].blocked);
    CHECK(blk.trace().back().procs[1].lastData == "wake up" && blk.trace().back().queue.empty());

    // 5. direct link, capacity 1: second send blocks, receive wakes it, order a then b
    IpcSim dir = runPreset(pr[4]);
    CHECK(countWake(dir) == 1 && countFailed(dir) == 1);             // P3's non-blocking receive from P1 fails
    std::vector<std::string> p2;
    last.clear();
    for (const IpcSnapshot& s : dir.trace())
        if (s.procs[1].lastData != last) { last = s.procs[1].lastData; p2.push_back(last); }
    CHECK((p2 == std::vector<std::string>{"a", "b"}));
    for (const IpcSnapshot& s : dir.trace()) CHECK(s.queue.size() <= 1);

    // 6. anonymous pipe: "hell" + "o pipe", then EOF
    IpcSim pipe = runPreset(pr[5]);
    CHECK(countFailed(pipe) == 0);
    std::string all;
    bool eof = false;
    for (const IpcSnapshot& s : pipe.trace()) {
        if (s.transfer.to == 0) all += s.transfer.data;
        if (s.event.find("END OF FILE") != std::string::npos) eof = true;
    }
    CHECK(all == "hello pipe" && eof);
    CHECK(pipe.trace().back().procs[0].bytesIn == 10 && pipe.trace().back().procs[1].bytesOut == 10);

    // 7. pipe full: 8 bytes written, writer blocks, wakes after the read, nothing lost or duplicated
    IpcSim full = runPreset(pr[6]);
    CHECK(countWake(full) == 1);
    all.clear();
    for (const IpcSnapshot& s : full.trace()) {
        if (s.transfer.to == 0) all += s.transfer.data;
        CHECK((int)s.pipeBuf.size() <= 8);
    }
    CHECK(all == "ABCDEFGHIJ");
    CHECK(!full.trace().back().procs[1].blocked);

    // 8. empty pipe with an open writer: read blocks; closing the last writer gives EOF (wake)
    IpcSim emp = runPreset(pr[7]);
    CHECK(emp.trace()[3].procs[0].blocked && countWake(emp) == 1);
    CHECK(emp.trace().back().event.find("END OF FILE") != std::string::npos);
    {   // writing with no reader -> EPIPE; non-blocking read on empty pipe with a writer -> EAGAIN
        IpcConfig c;
        c.mode = IpcMode::Pipe;
        c.procs = {"a", "b"};
        IpcSim s(c);
        IpcOp rd = op(IpcOpKind::PipeRead, 0);
        rd.length = 3;
        rd.nonBlocking = true;
        CHECK(s.apply(rd) != "");
        IpcOp ca = op(IpcOpKind::PipeClose, 0), cb = op(IpcOpKind::PipeClose, 1);
        s.apply(ca);
        s.apply(cb);
        IpcOp w = op(IpcOpKind::PipeWrite, 0);
        w.data = "x";
        CHECK(s.apply(w).find("EPIPE") != std::string::npos);
    }

    // a blocked process cannot issue another operation
    {
        IpcSim s(pr[3].cfg);
        s.apply(op(IpcOpKind::Receive, 1));
        IpcOp snd = op(IpcOpKind::Send, 1);
        snd.data = "x";
        CHECK(s.apply(snd) != "");
    }

    // stepping back and branching: replay reproduces the earlier state exactly
    IpcSim br(pr[2].cfg);
    for (const IpcOp& op : pr[2].ops) br.apply(op);
    int n = 2;
    br.truncate(n);
    CHECK((int)br.trace().size() == n + 1);
    CHECK(br.trace().back().queue.size() == 2);
    IpcOp r = op(IpcOpKind::Receive, 1);
    CHECK(br.apply(r) == "" && br.trace().back().procs[1].lastData == "first");

    // determinism
    IpcSim d1 = runPreset(pr[6]), d2 = runPreset(pr[6]);
    bool same = d1.trace().size() == d2.trace().size();
    for (size_t i = 0; same && i < d1.trace().size(); ++i) same = d1.trace()[i].event == d2.trace()[i].event;
    CHECK(same);
    IpcConfig bad;
    bad.queueCapacity = 0;
    CHECK_THROWS(IpcSim{bad}, std::invalid_argument);
    return report("test_ipc");
}
