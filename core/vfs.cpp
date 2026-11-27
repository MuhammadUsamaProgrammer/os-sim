// vfs.cpp - educational virtual filesystem (see vfs.h for the model and its simplifications).
#include "vfs.h"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>

static const int kMetaBlocks = 4;      // superblock, free bitmap, inode table (2)
static const int kMaxName = 28;
static const int kMaxSymlinkHops = 8;

const char* fileTypeName(FileType t) {
    switch (t) {
        case FileType::Regular: return "regular file";
        case FileType::Directory: return "directory";
        case FileType::Symlink: return "symbolic link";
    }
    return "?";
}
const char* allocStrategyName(AllocStrategy s) {
    switch (s) {
        case AllocStrategy::Contiguous: return "Contiguous";
        case AllocStrategy::Linked: return "Linked";
        case AllocStrategy::Indexed: return "Indexed";
    }
    return "?";
}
const char* blockKindName(BlockKind k) {
    switch (k) {
        case BlockKind::Free: return "free";
        case BlockKind::Meta: return "metadata";
        case BlockKind::Data: return "data";
        case BlockKind::Index: return "index";
    }
    return "?";
}
const char* fsErrName(FsErr e) {
    switch (e) {
        case FsErr::Ok: return "OK";
        case FsErr::NoEnt: return "ENOENT";
        case FsErr::Access: return "EACCES";
        case FsErr::Exist: return "EEXIST";
        case FsErr::NotDir: return "ENOTDIR";
        case FsErr::IsDir: return "EISDIR";
        case FsErr::NotEmpty: return "ENOTEMPTY";
        case FsErr::NoSpc: return "ENOSPC";
        case FsErr::Loop: return "ELOOP";
        case FsErr::BadF: return "EBADF";
        case FsErr::Inval: return "EINVAL";
        case FsErr::Perm: return "EPERM";
        case FsErr::FBig: return "EFBIG";
        case FsErr::NameTooLong: return "ENAMETOOLONG";
    }
    return "?";
}
const char* recoveryClassName(RecoveryClass c) {
    switch (c) {
        case RecoveryClass::Full: return "Fully recoverable";
        case RecoveryClass::Partial: return "Partially recoverable";
        case RecoveryClass::Unrecoverable: return "Unrecoverable";
        case RecoveryClass::Unknown: return "Unknown (insufficient evidence)";
    }
    return "?";
}

// ------------------------------------------------------------------ construction

void VirtualFs::validateGeometry(int blockCount, int blockSize) {
    if (blockCount < 16 || blockCount > 512) throw std::invalid_argument("block count must be 16..512");
    if (blockSize < 32 || blockSize > 1024 || blockSize % 8) throw std::invalid_argument("block size must be 32..1024 bytes and a multiple of 8");
}

// Creates an empty filesystem: metadata blocks, users / groups and the root directory.
VirtualFs::VirtualFs(int blockCount, int blockSize, AllocStrategy s) : blockSize_(blockSize), strategy_(s) {
    validateGeometry(blockCount, blockSize);
    blocks_.resize(blockCount);
    for (Block& b : blocks_) b.bytes.assign(blockSize, 0);
    for (int i = 0; i < kMetaBlocks; ++i) blocks_[i].kind = BlockKind::Meta;
    groups_ = {{0, "root"}, {100, "users"}, {101, "staff"}, {102, "students"}};
    users_ = {{0, "root", 0, {0}},
              {1000, "alice", 100, {100, 101, 102}},
              {1001, "bob", 100, {100, 102}},
              {1002, "carol", 100, {100, 101}}};
    int r = newInode(FileType::Directory, 0755, 0, 0);
    inodes_[r].links = 2;
    inodes_[r].parent = r;
}

const VUser* VirtualFs::user(int uid) const {
    for (const VUser& u : users_) if (u.uid == uid) return &u;
    return nullptr;
}
const VUser* VirtualFs::userByName(const std::string& n) const {
    for (const VUser& u : users_) if (u.name == n) return &u;
    return nullptr;
}
std::string VirtualFs::userName(int uid) const { const VUser* u = user(uid); return u ? u->name : std::to_string(uid); }
std::string VirtualFs::groupName(int gid) const {
    for (const VGroup& g : groups_) if (g.gid == gid) return g.name;
    return std::to_string(gid);
}
bool VirtualFs::inGroup(int uid, int gid) const {
    const VUser* u = user(uid);
    if (!u) return false;
    if (u->gid == gid) return true;
    return std::find(u->groups.begin(), u->groups.end(), gid) != u->groups.end();
}
bool VirtualFs::setCurrentUser(int uid) {
    if (!user(uid)) return false;
    cur_ = uid;
    log("su", true, "current user is now " + userName(uid));
    return true;
}
int VirtualFs::spawnProcess(const std::string& name, int uid) {
    VProcess p;
    p.pid = nextPid_++;
    p.name = name;
    p.uid = uid;
    procs_[p.pid] = p;
    return p.pid;
}
int VirtualFs::shellPid(int uid) {
    for (auto& kv : procs_) if (kv.second.uid == uid && kv.second.name == "sh") return kv.first;
    return spawnProcess("sh", uid);
}

// ------------------------------------------------------------------ helpers

void VirtualFs::log(const std::string& op, bool ok, const std::string& text) {
    FsEvent e;
    e.time = clock_;
    e.user = userName(cur_);
    e.op = op;
    e.ok = ok;
    e.text = text;
    events_.push_back(e);
    if (events_.size() > 2000) events_.erase(events_.begin(), events_.begin() + 500);
}
FsResult VirtualFs::fail(FsErr e, const std::string& op, const std::string& why) {
    FsResult r;
    r.err = e;
    r.message = std::string(fsErrName(e)) + ": " + why;
    log(op, false, r.message);
    return r;
}
FsResult VirtualFs::okr(const std::string& op, const std::string& what, int value) {
    FsResult r;
    r.message = what;
    r.value = value;
    log(op, true, what);
    return r;
}

std::vector<std::string> VirtualFs::split(const std::string& path) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : path) {
        if (c == '/') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

static bool validName(const std::string& n, std::string& why) {
    if (n.empty() || n == "." || n == "..") { why = "invalid name '" + n + "'"; return false; }
    for (char c : n) if (c == '/' || (unsigned char)c < 33 || (unsigned char)c > 126) { why = "names may contain printable characters except '/' and spaces"; return false; }
    return true;
}

int VirtualFs::newInode(FileType t, int mode, int uid, int gid) {
    int id = -1;
    for (int i = 0; i < (int)inodes_.size(); ++i) if (!inodes_[i].used) { id = i; break; }
    if (id < 0) {
        if ((int)inodes_.size() >= std::max(32, blockCount())) return -1;   // inode table full
        id = (int)inodes_.size();
        inodes_.emplace_back();
    }
    Inode n;
    n.id = id;
    n.used = true;
    n.type = t;
    n.mode = mode & 0777;
    n.uid = uid;
    n.gid = gid;
    n.ctime = n.mtime = n.atime = clock_;
    inodes_[id] = n;
    return id;
}

// ------------------------------------------------------------------ permissions

std::string VirtualFs::accessClass(int uid, const Inode& n) const {
    if (uid == n.uid) return "owner";
    if (inGroup(uid, n.gid)) return "group";
    return "other";
}

// Normal precedence: exactly ONE class applies (owner, else group, else other); classes are not OR-ed.
bool VirtualFs::may(int uid, const Inode& n, char what, std::string* why) const {
    int bit = what == 'r' ? 4 : what == 'w' ? 2 : 1;
    if (uid == 0) {
        bool ok = !(what == 'x' && n.type == FileType::Regular && (n.mode & 0111) == 0);
        if (why) *why = ok ? "root (uid 0, simplified privileged user) bypasses the permission bits"
                           : "even root needs at least one x bit to execute a regular file";
        return ok;
    }
    std::string cls = accessClass(uid, n);
    int shift = cls == "owner" ? 6 : cls == "group" ? 3 : 0;
    int bits = (n.mode >> shift) & 7;
    bool ok = (bits & bit) != 0;
    if (why) {
        std::string rwx = std::string(bits & 4 ? "r" : "-") + (bits & 2 ? "w" : "-") + (bits & 1 ? "x" : "-");
        std::string who = cls == "owner" ? userName(uid) + " owns it" : cls == "group" ? userName(uid) + " is in group " + groupName(n.gid)
                                                                                       : userName(uid) + " is neither owner nor in group " + groupName(n.gid);
        *why = who + " -> " + cls + " bits " + rwx + " -> '" + std::string(1, what) + "' " + (ok ? "granted" : "denied");
    }
    return ok;
}

std::string VirtualFs::modeString(int mode, FileType t) {
    std::string s = t == FileType::Directory ? "d" : t == FileType::Symlink ? "l" : "-";
    for (int shift = 6; shift >= 0; shift -= 3) {
        int b = (mode >> shift) & 7;
        s += b & 4 ? 'r' : '-';
        s += b & 2 ? 'w' : '-';
        s += b & 1 ? 'x' : '-';
    }
    return s;
}

// Accepts octal ("750"), a 9-character string ("rwxr-x---") or chmod clauses ("u+x,g-w,o=r,a+r").
bool VirtualFs::parseMode(const std::string& text, int oldMode, int& out) {
    std::string t;
    for (char c : text) if (c != ' ') t += c;
    if (t.empty()) return false;
    bool octal = t.size() <= 4 && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '7'; });
    if (octal) { out = std::stoi(t, nullptr, 8) & 0777; return t.size() == 3 || (t.size() == 4 && t[0] == '0'); }
    if (t.size() == 9 && t.find_first_not_of("rwx-") == std::string::npos) {
        static const char* pat = "rwxrwxrwx";
        int m = 0;
        for (int i = 0; i < 9; ++i) {
            if (t[i] == pat[i]) m |= 1 << (8 - i);
            else if (t[i] != '-') return false;
        }
        out = m;
        return true;
    }
    int m = oldMode & 0777;
    std::stringstream ss(t);
    std::string clause;
    while (std::getline(ss, clause, ',')) {
        size_t opPos = clause.find_first_of("+-=");
        if (opPos == std::string::npos) return false;
        std::string who = clause.substr(0, opPos), perms = clause.substr(opPos + 1);
        char op = clause[opPos];
        if (who.empty()) who = "a";
        int mask = 0;
        for (char w : who) {
            if (w == 'u') mask |= 0700; else if (w == 'g') mask |= 0070; else if (w == 'o') mask |= 0007;
            else if (w == 'a') mask |= 0777; else return false;
        }
        int bits = 0;
        for (char p : perms) {
            if (p == 'r') bits |= 0444; else if (p == 'w') bits |= 0222; else if (p == 'x') bits |= 0111; else return false;
        }
        bits &= mask;
        if (op == '+') m |= bits;
        else if (op == '-') m &= ~bits;
        else m = (m & ~mask) | bits;
    }
    out = m;
    return true;
}

// ------------------------------------------------------------------ path resolution

// Walks the components from the root. Search ('x') permission is needed on every directory
// that is traversed (including the parent of the last component). Symbolic links are followed
// with a hop limit (ELOOP); relative targets are resolved from the link's directory.
FsResult VirtualFs::walk(const std::string& path, bool followLast, int depth, std::vector<std::string>* trace) const {
    FsResult r;
    std::vector<std::string> comps = split(path);
    int cur = 0;
    const int uid = acting_ >= 0 ? acting_ : cur_;
    std::string sofar = "/";
    if (trace && depth == 0) trace->push_back("start at / (inode 0)");
    for (size_t i = 0; i < comps.size(); ++i) {
        const Inode& d = inodes_[cur];
        if (d.type != FileType::Directory) { r.err = FsErr::NotDir; r.message = "ENOTDIR: " + sofar + " is not a directory"; return r; }
        std::string why;
        if (!may(uid, d, 'x', &why)) {
            r.err = FsErr::Access;
            r.message = "EACCES: search permission on directory " + sofar + " is needed to look up '" + comps[i] + "' (" + why + ")";
            if (trace) trace->push_back("x on " + sofar + ": denied");
            return r;
        }
        if (trace) trace->push_back("x on " + sofar + ": granted (" + why + ")");
        const std::string& c = comps[i];
        if (c == ".") continue;
        if (c == "..") { cur = d.parent; sofar = pathOf(cur); continue; }
        auto it = d.entries.find(c);
        if (it == d.entries.end()) { r.err = FsErr::NoEnt; r.message = "ENOENT: no entry '" + c + "' in " + sofar; return r; }
        int child = it->second;
        sofar = (sofar == "/" ? "/" : sofar + "/") + c;
        bool last = i + 1 == comps.size();
        if (inodes_[child].type == FileType::Symlink && (!last || followLast)) {
            if (depth + 1 > kMaxSymlinkHops) { r.err = FsErr::Loop; r.message = "ELOOP: more than " + std::to_string(kMaxSymlinkHops) + " symbolic links (loop?)"; return r; }
            std::string tgt = inodes_[child].target;
            std::string base = pathOf(cur);
            std::string full = !tgt.empty() && tgt[0] == '/' ? tgt : (base == "/" ? "/" : base + "/") + tgt;
            for (size_t k = i + 1; k < comps.size(); ++k) full += "/" + comps[k];
            if (trace) trace->push_back(sofar + " is a symbolic link -> " + tgt + " (hop " + std::to_string(depth + 1) + ")");
            return walk(full, followLast, depth + 1, trace);
        }
        cur = child;
        if (trace) trace->push_back("'" + c + "' -> inode " + std::to_string(child) + " (" + fileTypeName(inodes_[child].type) + ")");
    }
    r.value = cur;
    r.message = "resolved to inode " + std::to_string(cur);
    return r;
}

FsResult VirtualFs::resolve(const std::string& path, bool followLast, std::vector<std::string>* trace) const {
    return walk(path, followLast, 0, trace);
}

FsResult VirtualFs::resolveAs(int uid, const std::string& path, bool followLast, std::vector<std::string>* trace) const {
    int old = acting_;
    acting_ = uid;
    FsResult r = walk(path, followLast, 0, trace);
    acting_ = old;
    return r;
}

FsResult VirtualFs::checkAccess(int uid, const std::string& path, const std::string& op) const {
    int old = acting_;
    acting_ = uid;
    FsResult r;
    std::string why;
    if (op == "delete") {
        int par; std::string name;
        r = lookupParent(path, par, name);
        if (r.ok()) {
            if (!inodes_[par].entries.count(name)) { r.err = FsErr::NoEnt; r.message = "ENOENT: no entry '" + name + "'"; }
            else if (!may(uid, inodes_[par], 'w', &why)) { r.err = FsErr::Access; r.message = "EACCES: removing a name needs w on the parent directory: " + why; }
            else if (!may(uid, inodes_[par], 'x', &why)) { r.err = FsErr::Access; r.message = "EACCES: removing a name needs x on the parent directory: " + why; }
            else r.message = "allowed: w and x on the parent directory " + pathOf(par) + " (the file's own bits do not matter)";
        }
        acting_ = old;
        return r;
    }
    r = walk(path, true, 0, nullptr);
    if (r.ok()) {
        const Inode& n = inodes_[r.value];
        bool dir = n.type == FileType::Directory;
        char bit = 'r';
        std::string need;
        if (op == "read") { bit = 'r'; need = dir ? "r (list the names)" : "r"; }
        else if (op == "write") { bit = 'w'; need = dir ? "w (change entries; also needs x)" : "w"; }
        else if (op == "execute") { bit = 'x'; need = dir ? "x (search / traverse)" : "x"; }
        else if (op == "list") { bit = 'r'; need = "r on the directory"; if (!dir) { r.err = FsErr::NotDir; r.message = "ENOTDIR: not a directory"; } }
        else if (op == "create") { bit = 'w'; need = "w and x on the directory"; if (!dir) { r.err = FsErr::NotDir; r.message = "ENOTDIR: not a directory"; } }
        else { r.err = FsErr::Inval; r.message = "EINVAL: unknown operation " + op; }
        if (r.ok()) {
            bool ok = may(uid, n, bit, &why);
            if (ok && (op == "create" || (op == "write" && dir))) ok = may(uid, n, 'x', &why);
            if (!ok) { r.err = FsErr::Access; r.message = "EACCES: needs " + need + ": " + why; }
            else r.message = "allowed: " + why;
        }
    }
    acting_ = old;
    return r;
}

// Splits path into the parent directory (resolved, following links) and the final name.
FsResult VirtualFs::lookupParent(const std::string& path, int& parent, std::string& name, std::vector<std::string>* trace) const {
    std::vector<std::string> comps = split(path);
    FsResult r;
    if (comps.empty()) { r.err = FsErr::Inval; r.message = "EINVAL: the root directory has no parent entry"; return r; }
    name = comps.back();
    std::string why;
    if (!validName(name, why)) { r.err = FsErr::Inval; r.message = "EINVAL: " + why; return r; }
    if ((int)name.size() > kMaxName) { r.err = FsErr::NameTooLong; r.message = "ENAMETOOLONG: names are limited to 28 characters"; return r; }
    std::string dir = "/";
    for (size_t i = 0; i + 1 < comps.size(); ++i) dir += comps[i] + "/";
    r = walk(dir, true, 0, trace);
    if (!r.ok()) return r;
    parent = r.value;
    if (inodes_[parent].type != FileType::Directory) { r.err = FsErr::NotDir; r.message = "ENOTDIR: " + dir + " is not a directory"; return r; }
    return r;
}

std::string VirtualFs::pathOf(int id) const {
    if (id == 0) return "/";
    if (id < 0 || id >= (int)inodes_.size() || !inodes_[id].used) return "";
    if (inodes_[id].type == FileType::Directory) {
        std::string p;
        int cur = id;
        for (int guard = 0; cur != 0 && guard < 512; ++guard) {
            int par = inodes_[cur].parent;
            std::string nm;
            for (auto& kv : inodes_[par].entries) if (kv.second == cur) { nm = kv.first; break; }
            p = "/" + nm + p;
            cur = par;
        }
        return p;
    }
    // breadth-first search for a directory entry naming the inode (first in name order)
    std::vector<int> q = {0};
    for (size_t i = 0; i < q.size(); ++i) {
        const Inode& d = inodes_[q[i]];
        for (auto& kv : d.entries) {
            if (kv.second == id) { std::string base = pathOf(q[i]); return (base == "/" ? "/" : base + "/") + kv.first; }
            if (inodes_[kv.second].type == FileType::Directory) q.push_back(kv.second);
        }
    }
    return "";
}

bool VirtualFs::isAncestor(int dir, int maybeChild) const {
    for (int cur = maybeChild, guard = 0; guard < 512; ++guard) {
        if (cur == dir) return true;
        if (cur == 0) return false;
        cur = inodes_[cur].parent;
    }
    return false;
}

// ------------------------------------------------------------------ block layer

long VirtualFs::maxFileBytes() const {
    long data = blockCount() - kMetaBlocks;
    switch (strategy_) {
        case AllocStrategy::Contiguous: return data * blockSize_;
        case AllocStrategy::Linked: return data * (blockSize_ - 4);
        case AllocStrategy::Indexed: return std::min<long>(pointersPerIndex(), data - 1) * blockSize_;
    }
    return 0;
}

std::vector<int> VirtualFs::findFree(int count, bool contiguous, const std::set<int>& avoid, int preferStart) const {
    std::vector<int> v;
    if (count <= 0) return v;
    auto usable = [&](int b) { return b >= kMetaBlocks && b < blockCount() && blocks_[b].kind == BlockKind::Free && !avoid.count(b); };
    if (!contiguous) {
        for (int b = kMetaBlocks; b < blockCount() && (int)v.size() < count; ++b) if (usable(b)) v.push_back(b);
        if ((int)v.size() < count) v.clear();
        return v;
    }
    if (preferStart >= 0) {
        bool okRun = true;
        for (int k = 0; k < count; ++k) if (!usable(preferStart + k)) { okRun = false; break; }
        if (okRun) { for (int k = 0; k < count; ++k) v.push_back(preferStart + k); return v; }
        return v;
    }
    for (int b = kMetaBlocks; b + count <= blockCount(); ++b) {           // first fit
        int k = 0;
        while (k < count && usable(b + k)) ++k;
        if (k == count) { for (int j = 0; j < count; ++j) v.push_back(b + j); return v; }
        b += k;
    }
    return v;
}

void VirtualFs::takeBlock(int b, int owner, BlockKind k) {
    Block& bl = blocks_[b];
    bl.kind = k;
    bl.owner = owner;
    bl.gen += 1;                       // a new generation: older evidence in this block is gone
    bl.next = -1;
    std::fill(bl.bytes.begin(), bl.bytes.end(), 0);
}

static void putU32(std::vector<uint8_t>& v, int pos, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[pos + i] = (uint8_t)(x >> (8 * i));
}

// Rewrites the strategy's on-disk pointers: next pointers (last 4 bytes of each linked block)
// or the pointer array of the index block.
void VirtualFs::relink(Inode& n) {
    if (strategy_ == AllocStrategy::Linked) {
        for (size_t i = 0; i < n.blocks.size(); ++i) {
            int nx = i + 1 < n.blocks.size() ? n.blocks[i + 1] : -1;
            blocks_[n.blocks[i]].next = nx;
            putU32(blocks_[n.blocks[i]].bytes, blockSize_ - 4, (uint32_t)nx);
        }
    } else if (strategy_ == AllocStrategy::Indexed && n.indexBlock >= 0) {
        std::vector<uint8_t>& ib = blocks_[n.indexBlock].bytes;
        std::fill(ib.begin(), ib.end(), 0xff);
        for (size_t i = 0; i < n.blocks.size(); ++i) putU32(ib, (int)i * 4, (uint32_t)n.blocks[i]);
    }
}

void VirtualFs::freeBlocks(Inode& n) {
    for (int b : n.blocks) { blocks_[b].kind = BlockKind::Free; blocks_[b].owner = -1; }
    if (n.indexBlock >= 0) { blocks_[n.indexBlock].kind = BlockKind::Free; blocks_[n.indexBlock].owner = -1; }
    n.blocks.clear();
    n.indexBlock = -1;
}

static std::string g_lastResizeWhy;

// Grows or shrinks the block list of a regular file to hold `bytes`. On failure NOTHING changes
// (the allocation is all-or-nothing) and g_lastResizeWhy explains why.
bool VirtualFs::resizeTo(int id, int bytes, const std::set<int>& avoid) {
    Inode& n = inodes_[id];
    int payload = payloadPerBlock();
    int need = (bytes + payload - 1) / payload;
    int cur = (int)n.blocks.size();
    g_lastResizeWhy.clear();
    if (strategy_ == AllocStrategy::Indexed && need > pointersPerIndex()) {
        g_lastResizeWhy = "EFBIG: one index block holds " + std::to_string(pointersPerIndex()) + " pointers, so the largest file is " +
                          std::to_string(pointersPerIndex() * blockSize_) + " bytes";
        return false;
    }
    if (need < cur) {
        for (int i = need; i < cur; ++i) { blocks_[n.blocks[i]].kind = BlockKind::Free; blocks_[n.blocks[i]].owner = -1; }
        n.blocks.resize(need);
        if (need == 0 && n.indexBlock >= 0) { blocks_[n.indexBlock].kind = BlockKind::Free; blocks_[n.indexBlock].owner = -1; n.indexBlock = -1; }
        relink(n);
    } else if (need > cur) {
        int extra = need - cur;
        int freeTotal = 0;
        for (int b = kMetaBlocks; b < blockCount(); ++b) if (blocks_[b].kind == BlockKind::Free && !avoid.count(b)) ++freeTotal;
        if (strategy_ == AllocStrategy::Contiguous) {
            std::vector<int> run;
            if (cur > 0) run = findFree(extra, true, avoid, n.blocks.back() + 1);        // extend in place
            if (!run.empty()) {
                for (int b : run) { takeBlock(b, id, BlockKind::Data); n.blocks.push_back(b); }
            } else {
                std::vector<int> all = findFree(need, true, avoid);                        // relocate the whole file
                if (all.empty()) {
                    int largest = 0, runLen = 0;
                    for (int b = kMetaBlocks; b < blockCount(); ++b) {
                        if (blocks_[b].kind == BlockKind::Free && !avoid.count(b)) largest = std::max(largest, ++runLen); else runLen = 0;
                    }
                    g_lastResizeWhy = "ENOSPC: contiguous allocation needs a run of " + std::to_string(need) + " free blocks; " +
                                      std::to_string(freeTotal) + " blocks are free in total but the largest run is " + std::to_string(largest);
                    return false;
                }
                std::vector<std::vector<uint8_t>> old;
                for (int b : n.blocks) old.push_back(blocks_[b].bytes);
                for (int b : n.blocks) { blocks_[b].kind = BlockKind::Free; blocks_[b].owner = -1; }
                n.blocks.clear();
                for (size_t i = 0; i < all.size(); ++i) {
                    takeBlock(all[i], id, BlockKind::Data);
                    if (i < old.size()) blocks_[all[i]].bytes = old[i];
                    n.blocks.push_back(all[i]);
                }
            }
        } else {
            int want = extra + (strategy_ == AllocStrategy::Indexed && n.indexBlock < 0 ? 1 : 0);
            std::vector<int> got = findFree(want, false, avoid);
            if (got.empty()) {
                g_lastResizeWhy = "ENOSPC: " + std::to_string(want) + " free blocks needed, only " + std::to_string(freeTotal) + " free";
                return false;
            }
            size_t k = 0;
            if (strategy_ == AllocStrategy::Indexed && n.indexBlock < 0) { n.indexBlock = got[k++]; takeBlock(n.indexBlock, id, BlockKind::Index); }
            for (; k < got.size(); ++k) { takeBlock(got[k], id, BlockKind::Data); n.blocks.push_back(got[k]); }
        }
        relink(n);
    }
    n.size = bytes;
    return true;
}

void VirtualFs::writeBytes(int id, long offset, const std::string& bytes) {
    Inode& n = inodes_[id];
    int payload = payloadPerBlock();
    for (size_t i = 0; i < bytes.size(); ++i) {
        long pos = offset + (long)i;
        blocks_[n.blocks[pos / payload]].bytes[pos % payload] = (uint8_t)bytes[i];
    }
    n.size = std::max<long>(n.size, offset + (long)bytes.size());
    n.mtime = clock_;
}

std::string VirtualFs::readBytes(int id, long offset, int count) const {
    const Inode& n = inodes_[id];
    std::string s;
    int payload = payloadPerBlock();
    for (long pos = offset; pos < offset + count && pos < n.size; ++pos) s += (char)blocks_[n.blocks[pos / payload]].bytes[pos % payload];
    return s;
}

std::string VirtualFs::fileContent(int id) const {
    const Inode* n = inode(id);
    if (!n || n->type != FileType::Regular) return "";
    return readBytes(id, 0, n->size);
}

DiskStats VirtualFs::stats() const {
    DiskStats s;
    s.total = blockCount();
    int run = 0;
    for (int b = 0; b < blockCount(); ++b) {
        switch (blocks_[b].kind) {
            case BlockKind::Meta: ++s.meta; break;
            case BlockKind::Data: ++s.data; break;
            case BlockKind::Index: ++s.index; break;
            case BlockKind::Free: ++s.free; break;
        }
        if (blocks_[b].kind == BlockKind::Free) { if (run++ == 0) ++s.freeRuns; s.largestFreeRun = std::max(s.largestFreeRun, run); }
        else run = 0;
    }
    for (const Inode& n : inodes_) {
        if (!n.used) continue;
        if (n.type == FileType::Regular) { ++s.files; s.payloadBytes += n.size; }
        else if (n.type == FileType::Directory) ++s.dirs;
        else ++s.symlinks;
    }
    s.capacityBytes = s.data * payloadPerBlock();
    return s;
}

std::vector<AccessStep> VirtualFs::accessPath(int id, int k) const {
    std::vector<AccessStep> v;
    const Inode* n = inode(id);
    if (!n || k < 0 || k >= (int)n->blocks.size()) return v;
    switch (strategy_) {
        case AllocStrategy::Contiguous:
            v.push_back({n->blocks[k], "start block " + std::to_string(n->blocks[0]) + " + " + std::to_string(k) + " = block " + std::to_string(n->blocks[k]) + " (1 disk read, direct access)"});
            break;
        case AllocStrategy::Linked:
            for (int i = 0; i <= k; ++i)
                v.push_back({n->blocks[i], "read block " + std::to_string(n->blocks[i]) + (i < k ? ", follow its next pointer -> " + std::to_string(n->blocks[i + 1]) : " = logical block " + std::to_string(k))});
            break;
        case AllocStrategy::Indexed:
            v.push_back({n->indexBlock, "read index block " + std::to_string(n->indexBlock) + ", entry " + std::to_string(k) + " -> " + std::to_string(n->blocks[k])});
            v.push_back({n->blocks[k], "read data block " + std::to_string(n->blocks[k])});
            break;
    }
    return v;
}

// ------------------------------------------------------------------ deletion lifetime

// An inode is released only when no directory entry names it AND no open-file description
// refers to it (unlink of an open file leaves an orphan inode until the last close).
void VirtualFs::releaseInodeIfUnused(int id) {
    Inode& n = inodes_[id];
    if (!n.used || n.links > 0 || n.openRefs > 0) return;
    if (n.type == FileType::Regular) {
        DeletedRecord d;
        d.id = (int)deleted_.size();
        d.path = n.target.empty() ? "(inode " + std::to_string(id) + ")" : n.target;   // last name kept in target (see removeEntry)
        d.inode = id;
        d.uid = n.uid; d.gid = n.gid; d.mode = n.mode;
        d.size = n.size;
        d.strategy = strategy_;
        d.blocks = n.blocks;
        for (int b : n.blocks) d.gens.push_back(blocks_[b].gen);
        d.indexBlock = n.indexBlock;
        d.indexGen = n.indexBlock >= 0 ? blocks_[n.indexBlock].gen : 0;
        d.deletedAt = clock_;
        d.checksum = checksum(fileContent(id));
        deleted_.push_back(d);
        freeBlocks(n);
        log("release", true, "inode " + std::to_string(id) + " released (link count 0, no open descriptors); " +
                             std::to_string(d.blocks.size()) + " data blocks marked free, contents still on the virtual disk");
    }
    n.used = false;
    n.entries.clear();
}

// ------------------------------------------------------------------ path operations

FsResult VirtualFs::mkdir(const std::string& path, int mode) {
    ++clock_;
    int par; std::string name, why;
    FsResult r = lookupParent(path, par, name);
    if (!r.ok()) return fail(r.err, "mkdir", r.message.substr(r.message.find(':') + 2));
    if (!may(cur_, inodes_[par], 'w', &why) || !may(cur_, inodes_[par], 'x')) return fail(FsErr::Access, "mkdir", "creating an entry needs w and x on the parent directory (" + why + ")");
    if (inodes_[par].entries.count(name)) return fail(FsErr::Exist, "mkdir", path + " already exists");
    const VUser* u = user(cur_);
    int id = newInode(FileType::Directory, mode, cur_, u ? u->gid : 0);
    if (id < 0) return fail(FsErr::NoSpc, "mkdir", "the inode table is full");
    inodes_[id].links = 2;
    inodes_[id].parent = par;
    inodes_[par].entries[name] = id;
    inodes_[par].links += 1;
    inodes_[par].mtime = clock_;
    return okr("mkdir", "created directory " + path + " (inode " + std::to_string(id) + ", " + modeString(mode, FileType::Directory) + ")", id);
}

FsResult VirtualFs::create(const std::string& path, int mode, const std::string& content) {
    ++clock_;
    int par; std::string name, why;
    FsResult r = lookupParent(path, par, name);
    if (!r.ok()) return fail(r.err, "create", r.message.substr(r.message.find(':') + 2));
    if (!may(cur_, inodes_[par], 'w', &why) || !may(cur_, inodes_[par], 'x')) return fail(FsErr::Access, "create", "creating an entry needs w and x on the parent directory (" + why + ")");
    if (inodes_[par].entries.count(name)) return fail(FsErr::Exist, "create", path + " already exists");
    const VUser* u = user(cur_);
    int id = newInode(FileType::Regular, mode, cur_, u ? u->gid : 0);
    if (id < 0) return fail(FsErr::NoSpc, "create", "the inode table is full");
    if (!content.empty() && !resizeTo(id, (int)content.size())) {
        inodes_[id].used = false;                       // roll back: no entry, no inode, no blocks
        std::string w = g_lastResizeWhy;
        return fail(w.rfind("EFBIG", 0) == 0 ? FsErr::FBig : FsErr::NoSpc, "create", w.substr(w.find(':') + 2) + " (nothing was created)");
    }
    writeBytes(id, 0, content);
    inodes_[id].links = 1;
    inodes_[par].entries[name] = id;
    inodes_[par].mtime = clock_;
    return okr("create", "created " + path + " (inode " + std::to_string(id) + ", " + std::to_string(content.size()) + " bytes in " +
                         std::to_string(inodes_[id].blocks.size()) + " blocks)", id);
}

FsResult VirtualFs::writeFile(const std::string& path, const std::string& content) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "write", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    std::string why;
    if (n.type == FileType::Directory) return fail(FsErr::IsDir, "write", path + " is a directory");
    if (!may(cur_, n, 'w', &why)) return fail(FsErr::Access, "write", why);
    std::string old = fileContent(r.value);
    if (!resizeTo(r.value, (int)content.size())) {
        std::string w = g_lastResizeWhy;
        return fail(w.rfind("EFBIG", 0) == 0 ? FsErr::FBig : FsErr::NoSpc, "write", w.substr(w.find(':') + 2) + " (the old content is unchanged)");
    }
    writeBytes(r.value, 0, content);
    n.size = (int)content.size();
    return okr("write", "wrote " + std::to_string(content.size()) + " bytes to " + path + " (" + std::to_string(n.blocks.size()) + " blocks)", (int)content.size());
}

FsResult VirtualFs::appendFile(const std::string& path, const std::string& content) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "append", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    std::string why;
    if (n.type == FileType::Directory) return fail(FsErr::IsDir, "append", path + " is a directory");
    if (!may(cur_, n, 'w', &why)) return fail(FsErr::Access, "append", why);
    int old = n.size;
    if (!resizeTo(r.value, old + (int)content.size())) {
        std::string w = g_lastResizeWhy;
        inodes_[r.value].size = old;
        return fail(w.rfind("EFBIG", 0) == 0 ? FsErr::FBig : FsErr::NoSpc, "append", w.substr(w.find(':') + 2) + " (nothing appended)");
    }
    writeBytes(r.value, old, content);
    return okr("append", "appended " + std::to_string(content.size()) + " bytes to " + path, (int)content.size());
}

FsResult VirtualFs::readFile(const std::string& path) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "read", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    std::string why;
    if (n.type == FileType::Directory) return fail(FsErr::IsDir, "read", path + " is a directory (use list)");
    if (!may(cur_, n, 'r', &why)) return fail(FsErr::Access, "read", why);
    n.atime = clock_;
    FsResult ok = okr("read", "read " + std::to_string(n.size) + " bytes from " + path + " (" + why + ")", n.size);
    ok.data = fileContent(r.value);
    return ok;
}

FsResult VirtualFs::list(const std::string& path) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "list", r.message.substr(r.message.find(':') + 2));
    const Inode& d = inodes_[r.value];
    std::string why;
    if (d.type != FileType::Directory) return fail(FsErr::NotDir, "list", path + " is not a directory");
    if (!may(cur_, d, 'r', &why)) return fail(FsErr::Access, "list", "listing names needs r on the directory (" + why + ")");
    FsResult ok = okr("list", "listed " + std::to_string(d.entries.size()) + " entries of " + path + " (" + why + ")", (int)d.entries.size());
    for (auto& kv : d.entries) ok.data += kv.first + (inodes_[kv.second].type == FileType::Directory ? "/" : "") + "\n";
    return ok;
}

// Removes a directory entry. For a regular file the inode survives while other links or open
// descriptors exist; for a directory it must be empty.
FsResult VirtualFs::removeEntry(const std::string& path, bool dir) {
    const char* op = dir ? "rmdir" : "unlink";
    int par; std::string name, why;
    FsResult r = lookupParent(path, par, name);
    if (!r.ok()) return fail(r.err, op, r.message.substr(r.message.find(':') + 2));
    Inode& p = inodes_[par];
    auto it = p.entries.find(name);
    if (it == p.entries.end()) return fail(FsErr::NoEnt, op, "no entry '" + name + "'");
    int id = it->second;
    Inode& n = inodes_[id];
    if (!dir && n.type == FileType::Directory) return fail(FsErr::IsDir, op, path + " is a directory (use rmdir)");
    if (dir && n.type != FileType::Directory) return fail(FsErr::NotDir, op, path + " is not a directory");
    if (!may(cur_, p, 'w', &why) || !may(cur_, p, 'x'))
        return fail(FsErr::Access, op, "removing a name needs w and x on the PARENT directory, not on the file (" + why + ")");
    if (dir && !n.entries.empty()) return fail(FsErr::NotEmpty, op, path + " is not empty");
    p.entries.erase(it);
    p.mtime = clock_;
    if (dir) {
        p.links -= 1;
        n.used = false;
        return okr(op, "removed directory " + path);
    }
    n.links -= 1;
    n.ctime = clock_;
    n.target = n.type == FileType::Symlink ? n.target : path;   // remember the last name for the deleted record
    std::string what = "removed the name " + path + " (inode " + std::to_string(id) + " link count now " + std::to_string(n.links) + ")";
    if (n.links > 0) what += "; the inode and its data stay (other hard links)";
    else if (n.openRefs > 0) what += "; the inode stays until the last open descriptor is closed (" + std::to_string(n.openRefs) + " open)";
    FsResult ok = okr(op, what, id);
    if (n.type == FileType::Symlink && n.links == 0) { n.used = false; return ok; }
    releaseInodeIfUnused(id);
    return ok;
}

FsResult VirtualFs::unlink(const std::string& path) { ++clock_; return removeEntry(path, false); }
FsResult VirtualFs::rmdir(const std::string& path) {
    ++clock_;
    if (split(path).empty()) return fail(FsErr::Inval, "rmdir", "the root directory cannot be removed");
    return removeEntry(path, true);
}

FsResult VirtualFs::rename(const std::string& from, const std::string& to) {
    ++clock_;
    int fp, tp; std::string fn, tn, why;
    FsResult r = lookupParent(from, fp, fn);
    if (!r.ok()) return fail(r.err, "rename", r.message.substr(r.message.find(':') + 2));
    r = lookupParent(to, tp, tn);
    if (!r.ok()) return fail(r.err, "rename", r.message.substr(r.message.find(':') + 2));
    auto it = inodes_[fp].entries.find(fn);
    if (it == inodes_[fp].entries.end()) return fail(FsErr::NoEnt, "rename", "no entry " + from);
    int id = it->second;
    for (int d : {fp, tp})
        if (!may(cur_, inodes_[d], 'w', &why) || !may(cur_, inodes_[d], 'x'))
            return fail(FsErr::Access, "rename", "rename needs w and x on both parent directories (" + pathOf(d) + ": " + why + ")");
    bool isDir = inodes_[id].type == FileType::Directory;
    if (isDir && isAncestor(id, tp)) return fail(FsErr::Inval, "rename", "a directory cannot be moved into itself");
    auto ex = inodes_[tp].entries.find(tn);
    if (ex != inodes_[tp].entries.end()) {
        int old = ex->second;
        if (old == id) return okr("rename", "source and destination are the same file");
        Inode& o = inodes_[old];
        if (o.type == FileType::Directory && !isDir) return fail(FsErr::IsDir, "rename", to + " is a directory");
        if (o.type != FileType::Directory && isDir) return fail(FsErr::NotDir, "rename", to + " is not a directory");
        if (o.type == FileType::Directory && !o.entries.empty()) return fail(FsErr::NotEmpty, "rename", to + " is not empty");
        inodes_[tp].entries.erase(ex);
        if (o.type == FileType::Directory) { inodes_[tp].links -= 1; o.used = false; }
        else { o.links -= 1; o.target = o.type == FileType::Symlink ? o.target : to; if (o.type == FileType::Symlink && o.links == 0) o.used = false; else releaseInodeIfUnused(old); }
    }
    inodes_[fp].entries.erase(fn);
    inodes_[tp].entries[tn] = id;
    if (isDir && fp != tp) { inodes_[fp].links -= 1; inodes_[tp].links += 1; inodes_[id].parent = tp; }
    inodes_[id].ctime = clock_;
    return okr("rename", "renamed " + from + " -> " + to + " (same inode " + std::to_string(id) + ": only directory entries changed)", id);
}

FsResult VirtualFs::copy(const std::string& from, const std::string& to) {
    FsResult r = readFile(from);
    if (!r.ok()) return r;
    FsResult src = resolve(from);
    if (inodes_[src.value].type == FileType::Directory) return fail(FsErr::IsDir, "copy", "copying directories is not supported in this model");
    FsResult c = create(to, inodes_[src.value].mode, r.data);
    if (c.ok()) c.message = "copied " + from + " -> " + to + " (a NEW inode " + std::to_string(c.value) + " with its own blocks)";
    return c;
}

FsResult VirtualFs::link(const std::string& existing, const std::string& newPath) {
    ++clock_;
    FsResult r = resolve(existing, false);
    if (!r.ok()) return fail(r.err, "link", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    if (n.type == FileType::Directory) return fail(FsErr::Perm, "link", "hard links to directories are not allowed (they could create cycles)");
    int par; std::string name, why;
    FsResult p = lookupParent(newPath, par, name);
    if (!p.ok()) return fail(p.err, "link", p.message.substr(p.message.find(':') + 2));
    if (!may(cur_, inodes_[par], 'w', &why) || !may(cur_, inodes_[par], 'x')) return fail(FsErr::Access, "link", "a new name needs w and x on its parent directory (" + why + ")");
    if (inodes_[par].entries.count(name)) return fail(FsErr::Exist, "link", newPath + " already exists");
    inodes_[par].entries[name] = r.value;
    n.links += 1;
    n.ctime = clock_;
    return okr("link", "hard link " + newPath + " -> inode " + std::to_string(r.value) + " (link count " + std::to_string(n.links) + ")", r.value);
}

FsResult VirtualFs::symlink(const std::string& target, const std::string& linkPath) {
    ++clock_;
    if (target.empty()) return fail(FsErr::Inval, "symlink", "empty target");
    int par; std::string name, why;
    FsResult p = lookupParent(linkPath, par, name);
    if (!p.ok()) return fail(p.err, "symlink", p.message.substr(p.message.find(':') + 2));
    if (!may(cur_, inodes_[par], 'w', &why) || !may(cur_, inodes_[par], 'x')) return fail(FsErr::Access, "symlink", "a new name needs w and x on its parent directory (" + why + ")");
    if (inodes_[par].entries.count(name)) return fail(FsErr::Exist, "symlink", linkPath + " already exists");
    const VUser* u = user(cur_);
    int id = newInode(FileType::Symlink, 0777, cur_, u ? u->gid : 0);
    if (id < 0) return fail(FsErr::NoSpc, "symlink", "the inode table is full");
    inodes_[id].target = target;
    inodes_[id].size = (int)target.size();
    inodes_[id].links = 1;
    inodes_[par].entries[name] = id;
    bool dangling = !resolve(linkPath).ok();
    return okr("symlink", "symbolic link " + linkPath + " -> " + target + " (inode " + std::to_string(id) + ")" + (dangling ? "; the target does not resolve (dangling link)" : ""), id);
}

FsResult VirtualFs::chmod(const std::string& path, int mode) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "chmod", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    if (cur_ != 0 && cur_ != n.uid) return fail(FsErr::Perm, "chmod", "only the owner (" + userName(n.uid) + ") or root may change the mode");
    int old = n.mode;
    n.mode = mode & 0777;
    n.ctime = clock_;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%03o -> %03o", old, n.mode);
    return okr("chmod", "chmod " + path + ": " + buf + " (" + modeString(n.mode, n.type) + ")", n.mode);
}

FsResult VirtualFs::chown(const std::string& path, int uid) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "chown", r.message.substr(r.message.find(':') + 2));
    if (!user(uid)) return fail(FsErr::Inval, "chown", "unknown user " + std::to_string(uid));
    if (cur_ != 0) return fail(FsErr::Perm, "chown", "only root may change the owner (Linux rule: users cannot give files away)");
    inodes_[r.value].uid = uid;
    inodes_[r.value].ctime = clock_;
    return okr("chown", "owner of " + path + " is now " + userName(uid));
}

FsResult VirtualFs::chgrp(const std::string& path, int gid) {
    ++clock_;
    FsResult r = resolve(path);
    if (!r.ok()) return fail(r.err, "chgrp", r.message.substr(r.message.find(':') + 2));
    bool known = false;
    for (const VGroup& g : groups_) if (g.gid == gid) known = true;
    if (!known) return fail(FsErr::Inval, "chgrp", "unknown group " + std::to_string(gid));
    Inode& n = inodes_[r.value];
    if (cur_ != 0 && (cur_ != n.uid || !inGroup(cur_, gid)))
        return fail(FsErr::Perm, "chgrp", "the owner may only choose a group they belong to (or be root)");
    n.gid = gid;
    n.ctime = clock_;
    return okr("chgrp", "group of " + path + " is now " + groupName(gid));
}

// ------------------------------------------------------------------ descriptors

namespace {
struct ActAs {          // performs a call with the permissions of a process's user
    int& cur; int old;
    ActAs(int& c, int uid) : cur(c), old(c) { cur = uid; }
    ~ActAs() { cur = old; }
};
}

FsResult VirtualFs::openInternal(int pid, int id, bool rd, bool wr, bool append) {
    int slot = -1;
    for (int i = 0; i < (int)ofiles_.size(); ++i) if (!ofiles_[i].used) { slot = i; break; }
    if (slot < 0) { slot = (int)ofiles_.size(); ofiles_.emplace_back(); }
    OpenFile& o = ofiles_[slot];
    o = OpenFile();
    o.used = true; o.inode = id; o.rd = rd; o.wr = wr; o.append = append; o.refs = 1;
    inodes_[id].openRefs += 1;
    VProcess& p = procs_[pid];
    int fd = 3;
    while (p.fds.count(fd)) ++fd;
    p.fds[fd] = slot;
    return okr("open", p.name + "(" + std::to_string(pid) + ") fd " + std::to_string(fd) + " -> open-file entry " + std::to_string(slot) +
                       " -> inode " + std::to_string(id) + " (offset 0" + (append ? ", O_APPEND" : "") + ")", fd);
}

FsResult VirtualFs::open(int pid, const std::string& path, bool rd, bool wr, bool create_, bool trunc, bool append) {
    if (!procs_.count(pid)) return fail(FsErr::Inval, "open", "no process " + std::to_string(pid));
    ActAs as(cur_, procs_[pid].uid);
    if (!rd && !wr) return fail(FsErr::Inval, "open", "open needs read and/or write access");
    FsResult r = resolve(path);
    if (!r.ok() && r.err == FsErr::NoEnt && create_) {
        FsResult c = create(path, 0644);
        if (!c.ok()) return c;
        r = resolve(path);
    }
    ++clock_;
    if (!r.ok()) return fail(r.err, "open", r.message.substr(r.message.find(':') + 2));
    Inode& n = inodes_[r.value];
    std::string why;
    if (n.type == FileType::Directory && wr) return fail(FsErr::IsDir, "open", path + " is a directory");
    if (rd && !may(cur_, n, 'r', &why)) return fail(FsErr::Access, "open", "O_RDONLY/O_RDWR: " + why);
    if (wr && !may(cur_, n, 'w', &why)) return fail(FsErr::Access, "open", "O_WRONLY/O_RDWR: " + why);
    if (trunc && wr) resizeTo(r.value, 0);
    return openInternal(pid, r.value, rd, wr, append);
}

FsResult VirtualFs::read(int pid, int fd, int count) {
    ++clock_;
    auto pit = procs_.find(pid);
    if (pit == procs_.end() || !pit->second.fds.count(fd)) return fail(FsErr::BadF, "read", "fd " + std::to_string(fd) + " is not open in process " + std::to_string(pid));
    OpenFile& o = ofiles_[pit->second.fds[fd]];
    if (!o.rd) return fail(FsErr::BadF, "read", "fd " + std::to_string(fd) + " was not opened for reading");
    if (count < 0) return fail(FsErr::Inval, "read", "negative count");
    std::string s = readBytes(o.inode, o.offset, count);
    long before = o.offset;
    o.offset += (long)s.size();
    inodes_[o.inode].atime = clock_;
    FsResult ok = okr("read", "read(fd " + std::to_string(fd) + ", " + std::to_string(count) + ") -> " + std::to_string(s.size()) + " bytes, offset " +
                                  std::to_string(before) + " -> " + std::to_string(o.offset) + (s.empty() ? " (end of file)" : ""), (int)s.size());
    ok.data = s;
    return ok;
}

FsResult VirtualFs::write(int pid, int fd, const std::string& bytes) {
    ++clock_;
    auto pit = procs_.find(pid);
    if (pit == procs_.end() || !pit->second.fds.count(fd)) return fail(FsErr::BadF, "write", "fd " + std::to_string(fd) + " is not open in process " + std::to_string(pid));
    OpenFile& o = ofiles_[pit->second.fds[fd]];
    if (!o.wr) return fail(FsErr::BadF, "write", "fd " + std::to_string(fd) + " was not opened for writing");
    Inode& n = inodes_[o.inode];
    if (o.append) o.offset = n.size;
    int oldSize = n.size;
    int newSize = std::max<long>(n.size, o.offset + (long)bytes.size());
    if (!resizeTo(o.inode, newSize)) {
        n.size = oldSize;
        std::string w = g_lastResizeWhy;
        return fail(w.rfind("EFBIG", 0) == 0 ? FsErr::FBig : FsErr::NoSpc, "write", w.substr(w.find(':') + 2) + " (nothing written)");
    }
    // bytes between the old end and the write offset (a "hole") read as zeros
    writeBytes(o.inode, o.offset, bytes);
    long before = o.offset;
    o.offset += (long)bytes.size();
    return okr("write", "write(fd " + std::to_string(fd) + ", " + std::to_string(bytes.size()) + " bytes) at offset " + std::to_string(before) +
                        " -> offset " + std::to_string(o.offset) + ", file size " + std::to_string(n.size), (int)bytes.size());
}

FsResult VirtualFs::lseek(int pid, int fd, long off) {
    ++clock_;
    auto pit = procs_.find(pid);
    if (pit == procs_.end() || !pit->second.fds.count(fd)) return fail(FsErr::BadF, "lseek", "fd " + std::to_string(fd) + " is not open");
    if (off < 0) return fail(FsErr::Inval, "lseek", "negative offset");
    OpenFile& o = ofiles_[pit->second.fds[fd]];
    o.offset = off;
    return okr("lseek", "lseek(fd " + std::to_string(fd) + ", " + std::to_string(off) + "): the offset lives in the shared open-file entry " +
                        std::to_string(pit->second.fds[fd]) + " (" + std::to_string(o.refs) + " descriptor(s) see it)", (int)off);
}

FsResult VirtualFs::close(int pid, int fd) {
    ++clock_;
    auto pit = procs_.find(pid);
    if (pit == procs_.end() || !pit->second.fds.count(fd)) return fail(FsErr::BadF, "close", "fd " + std::to_string(fd) + " is not open");
    int slot = pit->second.fds[fd];
    pit->second.fds.erase(fd);
    OpenFile& o = ofiles_[slot];
    o.refs -= 1;
    std::string what = "close(fd " + std::to_string(fd) + ")";
    if (o.refs > 0) { what += ": open-file entry " + std::to_string(slot) + " still used by " + std::to_string(o.refs) + " descriptor(s)"; return okr("close", what); }
    int id = o.inode;
    o.used = false;
    inodes_[id].openRefs -= 1;
    what += ": open-file entry " + std::to_string(slot) + " freed";
    if (inodes_[id].links == 0 && inodes_[id].openRefs == 0) what += "; the inode had no names left, so it is released now";
    FsResult ok = okr("close", what);
    releaseInodeIfUnused(id);
    return ok;
}

FsResult VirtualFs::dup(int pid, int fd) {
    ++clock_;
    auto pit = procs_.find(pid);
    if (pit == procs_.end() || !pit->second.fds.count(fd)) return fail(FsErr::BadF, "dup", "fd " + std::to_string(fd) + " is not open");
    int slot = pit->second.fds[fd];
    int nfd = 3;
    while (pit->second.fds.count(nfd)) ++nfd;
    pit->second.fds[nfd] = slot;
    ofiles_[slot].refs += 1;
    return okr("dup", "dup(fd " + std::to_string(fd) + ") -> fd " + std::to_string(nfd) + ": both share open-file entry " + std::to_string(slot) + " (one offset)", nfd);
}

// ------------------------------------------------------------------ recovery lab

uint32_t VirtualFs::checksum(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

std::string VirtualFs::makeSignatureFile(const std::string& payload) {
    std::string s = "OSF1";
    int n = (int)std::min<size_t>(payload.size(), 65535);
    s += (char)(n & 0xff);
    s += (char)((n >> 8) & 0xff);
    s += payload.substr(0, n);
    s += "END!";
    return s;
}

void VirtualFs::loseMetadata(int recordId) {
    if (recordId < 0 || recordId >= (int)deleted_.size()) return;
    deleted_[recordId].metadataAvailable = false;
    log("metadata-loss", true, "deleted record " + std::to_string(recordId) + " (" + deleted_[recordId].path +
                               ") lost: name, size and block list are no longer known");
}

// Metadata-based analysis: the record says which blocks held the file. A block is intact only
// if its generation is still the one recorded at deletion (it was never reallocated since).
RecoveryReport VirtualFs::analyse(int recordId) const {
    RecoveryReport rep;
    rep.recordId = recordId;
    rep.source = "metadata";
    if (recordId < 0 || recordId >= (int)deleted_.size()) { rep.explanation = "no such deleted record"; return rep; }
    const DeletedRecord& d = deleted_[recordId];
    rep.totalBytes = d.size;
    if (!d.metadataAvailable) {
        rep.cls = RecoveryClass::Unknown;
        rep.explanation = "The metadata record was lost: the block list, size and name are unknown. Try signature carving.";
        return rep;
    }
    int payload = d.strategy == AllocStrategy::Linked ? blockSize_ - 4 : blockSize_;
    int intactBlocks = 0;
    for (size_t i = 0; i < d.blocks.size(); ++i) {
        BlockEvidence e;
        e.logical = (int)i;
        e.block = d.blocks[i];
        const Block& b = blocks_[e.block];
        e.intact = b.gen == d.gens[i];
        if (e.intact) { ++intactBlocks; e.reason = std::string("generation ") + std::to_string(b.gen) + " unchanged since deletion (" + blockKindName(b.kind) + ")"; }
        else e.reason = "reallocated after deletion (generation " + std::to_string(d.gens[i]) + " -> " + std::to_string(b.gen) +
                        (b.kind != BlockKind::Free ? ", now " + std::string(blockKindName(b.kind)) + " of inode " + std::to_string(b.owner) : ", free again") + ")";
        rep.blocks.push_back(e);
    }
    for (int pos = 0; pos < d.size; ++pos) {
        int lb = pos / payload;
        bool ok = rep.blocks[lb].intact;
        rep.byteIntact.push_back(ok);
        rep.preview += ok ? (char)blocks_[d.blocks[lb]].bytes[pos % payload] : '?';
        if (ok) ++rep.recoverableBytes;
    }
    if (d.blocks.empty()) { rep.cls = RecoveryClass::Full; rep.verified = d.checksum == checksum(""); }
    else if (intactBlocks == (int)d.blocks.size()) {
        rep.verified = checksum(rep.preview) == d.checksum;
        rep.cls = rep.verified ? RecoveryClass::Full : RecoveryClass::Unknown;
    } else rep.cls = intactBlocks > 0 ? RecoveryClass::Partial : RecoveryClass::Unrecoverable;
    std::string idx = d.indexBlock >= 0 ? (blocks_[d.indexBlock].gen == d.indexGen ? " The index block is also intact." :
                                           " The index block was reused, but the deleted record still lists the data blocks.") : "";
    rep.explanation = std::to_string(intactBlocks) + " of " + std::to_string(d.blocks.size()) + " blocks intact; " +
                      std::to_string(rep.recoverableBytes) + " of " + std::to_string(d.size) + " bytes recoverable." + idx +
                      (rep.verified ? " The recovered bytes match the original checksum." : "");
    return rep;
}

// Signature carving: looks for the synthetic "OSF1" header at the start of free blocks and reads
// the following physical blocks (carving assumes contiguity - it knows no block list).
std::vector<CarveCandidate> VirtualFs::carve() const {
    std::vector<CarveCandidate> out;
    int payload = payloadPerBlock();
    for (int b = kMetaBlocks; b < blockCount(); ++b) {
        const Block& bl = blocks_[b];
        if (bl.kind != BlockKind::Free || payload < 6) continue;
        if (!(bl.bytes[0] == 'O' && bl.bytes[1] == 'S' && bl.bytes[2] == 'F' && bl.bytes[3] == '1')) continue;
        CarveCandidate c;
        c.startBlock = b;
        c.declaredLength = bl.bytes[4] | (bl.bytes[5] << 8);
        int total = 6 + c.declaredLength + 4;
        int nb = (total + payload - 1) / payload;
        std::string raw;
        for (int k = 0; k < nb && b + k < blockCount(); ++k) {
            const Block& x = blocks_[b + k];
            c.blocks.push_back(b + k);
            if (x.kind != BlockKind::Free) c.allFree = false;
            for (int i = 0; i < payload; ++i) raw += (char)x.bytes[i];
        }
        raw = raw.substr(0, std::min<size_t>(raw.size(), (size_t)total));
        c.footerOk = (int)raw.size() == total && raw.compare(total - 4, 4, "END!") == 0;
        RecoveryReport& r = c.report;
        r.source = "signature carving";
        r.totalBytes = total;
        r.preview = raw;
        r.byteIntact.assign(raw.size(), c.allFree);
        r.recoverableBytes = c.allFree ? (int)raw.size() : 0;
        for (size_t k = 0; k < c.blocks.size(); ++k) {
            const Block& x = blocks_[c.blocks[k]];
            r.blocks.push_back({(int)k, c.blocks[k], x.kind == BlockKind::Free, x.kind == BlockKind::Free ? "free block, contents read" : std::string("in use (") + blockKindName(x.kind) + ")"});
        }
        // Proof needs more than a signature: a surviving metadata record with matching generations.
        for (const DeletedRecord& d : deleted_) {
            if (!d.metadataAvailable || d.blocks != c.blocks) continue;
            bool gensOk = true;
            for (size_t k = 0; k < d.blocks.size(); ++k) if (blocks_[d.blocks[k]].gen != d.gens[k]) gensOk = false;
            if (gensOk && checksum(raw) == d.checksum) { r.recordId = d.id; r.verified = true; }
        }
        if (r.verified) { r.cls = RecoveryClass::Full; r.explanation = "Signature complete and confirmed by a deleted record (generations and checksum match)."; }
        else if (c.footerOk && c.allFree) { r.cls = RecoveryClass::Unknown; r.explanation = "Header and footer found and the declared length fits, but without metadata the integrity and the original name cannot be proven."; }
        else { r.cls = RecoveryClass::Partial; r.explanation = c.footerOk ? "Some carved blocks are in use by other files." : "Header found but the footer is missing or wrong: the following blocks were overwritten or the file was not contiguous."; }
        out.push_back(c);
    }
    return out;
}

FsResult VirtualFs::restore(const RecoveryReport& rep, const std::string& newPath) {
    ++clock_;
    if (rep.preview.empty() && rep.totalBytes > 0) return fail(FsErr::Inval, "restore", "nothing recoverable to restore");
    if (resolve(newPath).ok()) return fail(FsErr::Exist, "restore", newPath + " exists: restore never overwrites a file");
    // keep every block that still holds intact evidence of a deleted file
    std::set<int> avoid;
    for (const DeletedRecord& d : deleted_)
        for (size_t k = 0; k < d.blocks.size(); ++k) if (blocks_[d.blocks[k]].gen == d.gens[k] && blocks_[d.blocks[k]].kind == BlockKind::Free) avoid.insert(d.blocks[k]);
    for (const BlockEvidence& e : rep.blocks) if (e.block >= 0) avoid.insert(e.block);
    int par; std::string name, why;
    FsResult p = lookupParent(newPath, par, name);
    if (!p.ok()) return fail(p.err, "restore", p.message.substr(p.message.find(':') + 2));
    if (!may(cur_, inodes_[par], 'w', &why) || !may(cur_, inodes_[par], 'x')) return fail(FsErr::Access, "restore", why);
    const VUser* u = user(cur_);
    int id = newInode(FileType::Regular, 0600, cur_, u ? u->gid : 0);
    if (id < 0) return fail(FsErr::NoSpc, "restore", "the inode table is full");
    if (!rep.preview.empty() && !resizeTo(id, (int)rep.preview.size(), avoid)) {
        inodes_[id].used = false;
        return fail(FsErr::NoSpc, "restore", "not enough free blocks outside the evidence: preview only, the image was not changed");
    }
    writeBytes(id, 0, rep.preview);
    inodes_[id].links = 1;
    inodes_[par].entries[name] = id;
    if (rep.recordId >= 0 && rep.recordId < (int)deleted_.size()) deleted_[rep.recordId].restored = true;
    int unknown = (int)std::count(rep.byteIntact.begin(), rep.byteIntact.end(), false);
    return okr("restore", "restored " + std::to_string(rep.preview.size()) + " bytes into NEW file " + newPath + " (inode " + std::to_string(id) + ")" +
                          (unknown ? "; " + std::to_string(unknown) + " overwritten bytes are marked '?'" : "") + "; evidence blocks were not used", id);
}

// ------------------------------------------------------------------ invariants

std::vector<std::string> VirtualFs::checkInvariants() const {
    std::vector<std::string> bad;
    auto B = [&](const std::string& s) { bad.push_back(s); };
    std::vector<int> owner(blockCount(), -1);
    for (int i = 0; i < kMetaBlocks; ++i) if (blocks_[i].kind != BlockKind::Meta) B("block " + std::to_string(i) + " must be metadata");
    if (!inode(0) || inodes_[0].type != FileType::Directory) B("root is not a directory");
    std::map<int, int> names;                        // inode -> number of entries
    std::map<int, int> subdirs;
    for (const Inode& n : inodes_) {
        if (!n.used) continue;
        for (auto& kv : n.entries) {
            if (!inode(kv.second)) { B("entry " + kv.first + " points to a free inode"); continue; }
            names[kv.second] += 1;
            if (inodes_[kv.second].type == FileType::Directory) {
                subdirs[n.id] += 1;
                if (inodes_[kv.second].parent != n.id) B("directory " + kv.first + " has a wrong parent");
            }
        }
    }
    for (const Inode& n : inodes_) {
        if (!n.used) continue;
        std::string nm = "inode " + std::to_string(n.id);
        if (n.type == FileType::Directory) {
            if (n.links != 2 + subdirs[n.id]) B(nm + ": directory link count " + std::to_string(n.links) + " != 2 + subdirectories");
            if (n.id != 0 && names[n.id] != 1) B(nm + ": a directory must have exactly one name");
        } else if (n.links != names[n.id]) B(nm + ": link count " + std::to_string(n.links) + " != names " + std::to_string(names[n.id]));
        if (n.links == 0 && n.openRefs == 0 && n.id != 0) B(nm + ": unreferenced inode still in use");
        if (n.type != FileType::Regular) { if (!n.blocks.empty()) B(nm + ": non-regular file with data blocks"); continue; }
        int payload = payloadPerBlock();
        if ((int)n.blocks.size() != (n.size + payload - 1) / payload) B(nm + ": block count does not match the size");
        for (size_t k = 0; k < n.blocks.size(); ++k) {
            int b = n.blocks[k];
            if (b < kMetaBlocks || b >= blockCount()) { B(nm + ": block out of range"); continue; }
            if (owner[b] >= 0) B("block " + std::to_string(b) + " used by two files");
            owner[b] = n.id;
            if (blocks_[b].kind != BlockKind::Data || blocks_[b].owner != n.id) B("block " + std::to_string(b) + " not marked as data of " + nm);
            if (strategy_ == AllocStrategy::Contiguous && k > 0 && b != n.blocks[k - 1] + 1) B(nm + ": contiguous file is not contiguous");
            if (strategy_ == AllocStrategy::Linked && blocks_[b].next != (k + 1 < n.blocks.size() ? n.blocks[k + 1] : -1)) B(nm + ": broken next pointer");
        }
        if (strategy_ == AllocStrategy::Indexed) {
            if (n.blocks.empty() != (n.indexBlock < 0)) B(nm + ": index block present iff data blocks present");
            if (n.indexBlock >= 0) {
                if (owner[n.indexBlock] >= 0) B("index block shared");
                owner[n.indexBlock] = n.id;
                if (blocks_[n.indexBlock].kind != BlockKind::Index) B(nm + ": index block not marked");
            }
        } else if (n.indexBlock >= 0) B(nm + ": index block outside indexed allocation");
    }
    for (int b = kMetaBlocks; b < blockCount(); ++b)
        if ((blocks_[b].kind == BlockKind::Free) != (owner[b] < 0)) B("block " + std::to_string(b) + ": free-space bitmap disagrees with the files");
    std::map<int, int> refs;
    for (const OpenFile& o : ofiles_) if (o.used) refs[o.inode] += 1;
    for (const Inode& n : inodes_) if (n.used && n.openRefs != refs[n.id]) B("inode " + std::to_string(n.id) + ": open reference count mismatch");
    return bad;
}

// ------------------------------------------------------------------ persistence

static std::string hex(const std::string& s) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s) { o += h[c >> 4]; o += h[c & 15]; }
    return o.empty() ? "-" : o;
}
static bool unhex(const std::string& s, std::string& out) {
    out.clear();
    if (s == "-") return true;
    if (s.size() % 2) return false;
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out += (char)(a * 16 + b);
    }
    return true;
}

// Version 1 text image: geometry, every block, every used inode and the deleted records.
// Processes and open files are runtime state and are not saved.
std::string VirtualFs::saveImage() const {
    std::ostringstream o;
    o << "OSVFS 1\n";
    o << "geometry " << blockCount() << " " << blockSize_ << " " << (int)strategy_ << "\n";
    o << "clock " << clock_ << " " << cur_ << "\n";
    for (int b = 0; b < blockCount(); ++b) {
        const Block& x = blocks_[b];
        o << "block " << b << " " << (int)x.kind << " " << x.owner << " " << x.gen << " " << x.next << " "
          << hex(std::string(x.bytes.begin(), x.bytes.end())) << "\n";
    }
    for (const Inode& n : inodes_) {
        if (!n.used || (n.links == 0 && n.id != 0)) continue;
        o << "inode " << n.id << " " << (int)n.type << " " << n.mode << " " << n.uid << " " << n.gid << " " << n.links << " " << n.size << " "
          << n.indexBlock << " " << n.parent << " " << n.ctime << " " << n.mtime << " " << n.atime << " " << hex(n.target) << " " << n.blocks.size();
        for (int b : n.blocks) o << " " << b;
        o << " " << n.entries.size();
        for (auto& kv : n.entries) o << " " << hex(kv.first) << " " << kv.second;
        o << "\n";
    }
    for (const DeletedRecord& d : deleted_) {
        o << "deleted " << d.inode << " " << d.uid << " " << d.gid << " " << d.mode << " " << d.size << " " << (int)d.strategy << " " << d.indexBlock
          << " " << d.indexGen << " " << d.deletedAt << " " << d.checksum << " " << (int)d.metadataAvailable << " " << (int)d.restored << " "
          << hex(d.path) << " " << d.blocks.size();
        for (size_t k = 0; k < d.blocks.size(); ++k) o << " " << d.blocks[k] << " " << d.gens[k];
        o << "\n";
    }
    o << "end\n";
    return o.str();
}

bool VirtualFs::loadImage(const std::string& text, VirtualFs& out, std::string& err) {
    std::istringstream in(text);
    std::string line, word;
    auto bad = [&](const std::string& why) { err = why; return false; };
    if (!std::getline(in, line) || line != "OSVFS 1") return bad("not an OS-Sim virtual disk image (version 1)");
    int bc = 0, bs = 0, st = 0;
    if (!std::getline(in, line)) return bad("missing geometry");
    { std::istringstream l(line); if (!(l >> word >> bc >> bs >> st) || word != "geometry") return bad("bad geometry line"); }
    if (st < 0 || st > 2) return bad("unknown allocation strategy");
    try { validateGeometry(bc, bs); } catch (const std::exception& e) { return bad(e.what()); }
    VirtualFs fs(bc, bs, (AllocStrategy)st);
    fs.inodes_.clear();
    long clk = 0; int cu = 0;
    if (!std::getline(in, line)) return bad("missing clock");
    { std::istringstream l(line); if (!(l >> word >> clk >> cu) || word != "clock" || !fs.user(cu)) return bad("bad clock line"); }
    fs.clock_ = clk;
    fs.cur_ = cu;
    int seenBlocks = 0;
    bool ended = false;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        if (!(l >> word)) continue;
        if (word == "end") { ended = true; break; }
        if (word == "block") {
            int i, k, ow, gen, nx; std::string hx, bytes;
            if (!(l >> i >> k >> ow >> gen >> nx >> hx) || i != seenBlocks || i >= bc || k < 0 || k > 3 || !unhex(hx, bytes) || (int)bytes.size() != bs)
                return bad("bad block line " + std::to_string(seenBlocks));
            Block& b = fs.blocks_[i];
            b.kind = (BlockKind)k; b.owner = ow; b.gen = gen; b.next = nx;
            b.bytes.assign(bytes.begin(), bytes.end());
            if (nx < -1 || nx >= bc) return bad("block next pointer out of range");
            ++seenBlocks;
        } else if (word == "inode") {
            Inode n;
            int t; std::string tg; size_t nb, ne;
            if (!(l >> n.id >> t >> n.mode >> n.uid >> n.gid >> n.links >> n.size >> n.indexBlock >> n.parent >> n.ctime >> n.mtime >> n.atime >> tg >> nb))
                return bad("bad inode line");
            if (n.id < 0 || n.id >= std::max(32, bc) || t < 0 || t > 2 || n.mode < 0 || n.mode > 0777 || n.size < 0 || nb > (size_t)bc || !unhex(tg, n.target))
                return bad("inode " + std::to_string(n.id) + ": value out of range");
            n.type = (FileType)t;
            for (size_t k = 0; k < nb; ++k) { int b; if (!(l >> b) || b < 0 || b >= bc) return bad("inode block out of range"); n.blocks.push_back(b); }
            if (!(l >> ne) || ne > 4096) return bad("bad entry count");
            for (size_t k = 0; k < ne; ++k) {
                std::string hn, nm; int id; std::string why;
                if (!(l >> hn >> id) || !unhex(hn, nm) || !validName(nm, why) || id < 0) return bad("bad directory entry");
                n.entries[nm] = id;
            }
            if (n.indexBlock < -1 || n.indexBlock >= bc || n.parent < -1) return bad("inode index/parent out of range");
            n.used = true;
            if ((int)fs.inodes_.size() <= n.id) fs.inodes_.resize(n.id + 1);
            if (fs.inodes_[n.id].used) return bad("duplicate inode " + std::to_string(n.id));
            fs.inodes_[n.id] = n;
        } else if (word == "deleted") {
            DeletedRecord d;
            int s, ma, rs; std::string hp; size_t nb;
            if (!(l >> d.inode >> d.uid >> d.gid >> d.mode >> d.size >> s >> d.indexBlock >> d.indexGen >> d.deletedAt >> d.checksum >> ma >> rs >> hp >> nb) ||
                s < 0 || s > 2 || nb > (size_t)bc || !unhex(hp, d.path) || d.size < 0)
                return bad("bad deleted record");
            d.strategy = (AllocStrategy)s;
            d.metadataAvailable = ma != 0;
            d.restored = rs != 0;
            for (size_t k = 0; k < nb; ++k) { int b, g; if (!(l >> b >> g) || b < 0 || b >= bc) return bad("deleted record block out of range"); d.blocks.push_back(b); d.gens.push_back(g); }
            d.id = (int)fs.deleted_.size();
            fs.deleted_.push_back(d);
        } else return bad("unknown line '" + word + "'");
    }
    if (!ended) return bad("image truncated (no 'end')");
    if (seenBlocks != bc) return bad("image has " + std::to_string(seenBlocks) + " blocks, geometry says " + std::to_string(bc));
    if (fs.inodes_.empty() || !fs.inodes_[0].used) return bad("no root directory");
    for (Inode& n : fs.inodes_) {
        if (!n.used) continue;
        for (auto& kv : n.entries) if (kv.second >= (int)fs.inodes_.size() || !fs.inodes_[kv.second].used) return bad("entry '" + kv.first + "' points to a missing inode");
        if (n.parent >= (int)fs.inodes_.size()) return bad("parent out of range");
        if (n.type == FileType::Directory && (n.parent < 0 || !fs.inodes_[n.parent].used || fs.inodes_[n.parent].type != FileType::Directory))
            return bad("directory with an invalid parent");
    }
    std::vector<std::string> inv = fs.checkInvariants();
    if (!inv.empty()) return bad("inconsistent image: " + inv[0]);
    // cycle check: every directory must reach the root through its parents
    for (const Inode& n : fs.inodes_)
        if (n.used && n.type == FileType::Directory && !fs.isAncestor(0, n.id)) return bad("directory cycle detected");
    out = fs;
    out.log("load", true, "virtual disk image loaded and validated");
    return true;
}

// ------------------------------------------------------------------ samples

void loadSampleFs(VirtualFs& fs) {
    int keep = fs.currentUser();
    fs.setCurrentUser(0);
    fs.mkdir("/home", 0755);
    fs.mkdir("/home/alice", 0700);
    fs.mkdir("/home/bob", 0750);
    fs.mkdir("/home/carol", 0755);
    fs.mkdir("/shared", 0770);
    fs.mkdir("/pub", 0755);
    fs.mkdir("/tmp", 0777);
    fs.mkdir("/recovered", 0777);
    fs.chown("/home/alice", 1000);
    fs.chown("/home/bob", 1001);
    fs.chown("/home/carol", 1002);
    fs.chgrp("/shared", 101);
    fs.create("/pub/readme.txt", 0644, "Welcome to the OS-Sim virtual disk.\nEverything here lives in memory only.\n");
    fs.setCurrentUser(1000);
    fs.create("/home/alice/notes.txt", 0600,
              "Alice's private notes: inode = metadata, a directory entry = a name. Unlink removes a name; "
              "the data is freed when no name and no descriptor is left.\n");
    fs.create("/home/alice/todo.txt", 0640, "1. study block allocation\n2. try the recovery lab\n");
    fs.create("/shared/plan.txt", 0660, "Staff plan: group members may read and write this file.\n");
    fs.chgrp("/shared/plan.txt", 101);
    fs.setCurrentUser(1001);
    fs.create("/home/bob/report.osf", 0644, VirtualFs::makeSignatureFile("Bob's synthetic report: carving finds files by their OSF1 header and END! footer."));
    fs.setCurrentUser(1002);
    fs.link("/pub/readme.txt", "/home/carol/readme-link");
    fs.symlink("/pub", "/home/carol/pub");
    fs.symlink("missing.txt", "/home/carol/old");
    fs.symlink("/tmp/loopB", "/tmp/loopA");
    fs.symlink("/tmp/loopA", "/tmp/loopB");
    fs.setCurrentUser(keep == 0 ? 1000 : keep);
}

// ------------------------------------------------------------------ block allocation comparison

std::vector<AllocWorkloadOp> parseAllocWorkload(const std::string& text) {
    std::vector<AllocWorkloadOp> ops;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::istringstream l(item);
        std::string verb;
        if (!(l >> verb)) continue;
        AllocWorkloadOp op;
        if (verb == "create" || verb == "extend") {
            op.kind = verb == "create" ? AllocWorkloadOp::Create : AllocWorkloadOp::Extend;
            if (!(l >> op.name >> op.bytes) || op.bytes < 0) throw std::invalid_argument("'" + item + "': expected '" + verb + " <name> <bytes>'");
        } else if (verb == "delete") {
            op.kind = AllocWorkloadOp::Delete;
            if (!(l >> op.name)) throw std::invalid_argument("'" + item + "': expected 'delete <name>'");
        } else throw std::invalid_argument("unknown operation '" + verb + "' (use create, extend, delete)");
        std::string why;
        if (!validName(op.name, why)) throw std::invalid_argument(why);
        ops.push_back(op);
    }
    if (ops.empty()) throw std::invalid_argument("the workload is empty");
    return ops;
}

static std::string patternBytes(const std::string& name, int n, int salt) {
    std::string s;
    for (int i = 0; i < n; ++i) s += (char)('a' + (name[0] + i + salt) % 26);
    return s;
}

std::vector<AllocCompareRow> compareAllocation(const std::vector<AllocWorkloadOp>& ops, int blockCount, int blockSize) {
    std::vector<AllocCompareRow> rows;
    for (AllocStrategy s : {AllocStrategy::Contiguous, AllocStrategy::Linked, AllocStrategy::Indexed}) {
        AllocCompareRow row{s, {}, 0, {}, 0, 0, VirtualFs(blockCount, blockSize, s)};
        row.fs.setCurrentUser(0);
        int salt = 0;
        for (const AllocWorkloadOp& op : ops) {
            FsResult r;
            std::string p = "/" + op.name;
            if (op.kind == AllocWorkloadOp::Create) r = row.fs.create(p, 0644, patternBytes(op.name, op.bytes, salt++));
            else if (op.kind == AllocWorkloadOp::Extend) r = row.fs.appendFile(p, patternBytes(op.name, op.bytes, salt++));
            else r = row.fs.unlink(p);
            if (!r.ok()) ++row.failures;
            std::string verb = op.kind == AllocWorkloadOp::Create ? "create" : op.kind == AllocWorkloadOp::Extend ? "extend" : "delete";
            row.log.push_back(verb + " " + op.name + (op.kind != AllocWorkloadOp::Delete ? " " + std::to_string(op.bytes) : "") + ": " +
                              (r.ok() ? "ok" : r.message));
        }
        row.stats = row.fs.stats();
        row.metadataBlocks = row.stats.index;
        row.pointerBytes = s == AllocStrategy::Linked ? row.stats.data * 4 : s == AllocStrategy::Indexed ? row.stats.data * 4 : 0;
        rows.push_back(row);
    }
    return rows;
}

std::vector<AllocPreset> allocPresets() {
    return {
        {"Contiguous fails despite enough free space",
         "Five 3-block files alternate with small files; deleting the five leaves 15 free blocks in holes of 3 plus 12 at the end "
         "(27 free). A 13-block file: contiguous allocation fails (largest run 12), linked and indexed allocation succeed.",
         "create h1 360, create k1 100, create h2 360, create k2 100, create h3 360, create k3 100, create h4 360, create k4 100, "
         "create h5 360, create k5 100, delete h1, delete h2, delete h3, delete h4, delete h5, create big 1600",
         36, 128},
        {"Growing files (extend in place or relocate)",
         "Two files grow alternately: contiguous must relocate a file when its neighbour blocks are taken; linked and indexed just add blocks.",
         "create a 100, create b 100, extend a 200, extend b 200, extend a 300, create c 64", 48, 64},
        {"Many small files (metadata overhead)",
         "Ten 20-byte files: indexed allocation spends one index block per file; linked spends 4 bytes per block on pointers.",
         "create f0 20, create f1 20, create f2 20, create f3 20, create f4 20, create f5 20, create f6 20, create f7 20, create f8 20, create f9 20",
         32, 64},
        {"Largest file (index limit)",
         "One index block of 64 bytes holds 16 pointers: indexed allocation cannot represent a file of more than 16 blocks.",
         "create huge 1200", 48, 64},
    };
}

// ------------------------------------------------------------------ recovery presets

std::vector<RecoveryPreset> recoveryPresets() {
    return {
        {"Intact deleted file", "alice deletes notes.txt; no block was reused, so metadata recovery is complete and verified."},
        {"Partially overwritten", "notes.txt is deleted, then a new 1-block file reuses the first freed block."},
        {"Fully overwritten", "notes.txt is deleted, then a new file of the same block count reuses every freed block."},
        {"Metadata lost: signature carving", "bob's report.osf is deleted and its metadata record is lost; carving finds the OSF1 signature but cannot prove integrity."},
        {"Hard link and open descriptor", "a file has two names and is open in a process; unlinking both names leaves an orphan inode until the descriptor is closed."},
    };
}

void loadRecoveryPreset(VirtualFs& fs, int index) {
    if (index < 0 || index >= (int)recoveryPresets().size()) throw std::out_of_range("recovery preset");
    fs = VirtualFs(64, 64, AllocStrategy::Contiguous);
    loadSampleFs(fs);
    fs.setCurrentUser(1000);
    switch (index) {
        case 0: fs.unlink("/home/alice/notes.txt"); break;
        case 1:
            fs.unlink("/home/alice/notes.txt");
            fs.create("/home/alice/new.txt", 0600, std::string(40, 'N'));
            break;
        case 2: {
            fs.unlink("/home/alice/notes.txt");
            const DeletedRecord& d = fs.deleted().back();
            fs.create("/home/alice/big.txt", 0600, std::string((size_t)d.blocks.size() * fs.payloadPerBlock(), 'B'));
            break;
        }
        case 3:
            fs.setCurrentUser(1001);
            fs.unlink("/home/bob/report.osf");
            fs.loseMetadata(fs.deleted().back().id);
            break;
        case 4: {
            fs.create("/tmp/log.txt", 0644, "A log file with two names. The inode survives the first unlink.\n");
            fs.link("/tmp/log.txt", "/home/alice/log-link");
            int pid = fs.spawnProcess("logger", 1000);
            fs.open(pid, "/tmp/log.txt", true, false);
            fs.unlink("/tmp/log.txt");
            fs.unlink("/home/alice/log-link");
            break;
        }
    }
}
