// vfs.h - Phase 3 "Storage & Files": one educational virtual filesystem (pure C++17, no Qt,
// no threads, deterministic). Everything lives in memory inside this object: the virtual disk
// (blocks with bytes), the free-space bitmap, the inode table, directory entries, users and
// groups, per-process descriptor tables, the open-file table and the recovery metadata.
// The host disk is NEVER used as the simulation medium; virtual paths are not host paths.
//
// OS concepts: inode vs directory entry, hard and symbolic links, path resolution with search
// permission, owner/group/other permission classes, file descriptors and shared open-file
// descriptions (offsets), unlink lifetime (link count + open references), contiguous / linked /
// indexed block allocation, free-space management, deletion and recovery limits.
//
// Simplifications (documented in docs/PHASE3_MODULES.md):
//   - directory entries and inodes are kept in the inode-table region (no directory data blocks);
//   - blocks 0..3 are metadata: superblock, free-space bitmap, inode table (2 blocks);
//   - the clock is a logical operation counter, not wall time;
//   - permissions: 9 rwx bits only (no ACLs, sticky, setuid/setgid); uid 0 (root) is a simplified
//     privileged user: it bypasses read/write checks and search checks on directories;
//   - deleted blocks keep their bytes until they are reallocated (intentional, for teaching:
//     real filesystems / SSDs with TRIM may behave differently).
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class FileType { Regular, Directory, Symlink };
const char* fileTypeName(FileType);

enum class AllocStrategy { Contiguous, Linked, Indexed };
const char* allocStrategyName(AllocStrategy);

enum class BlockKind { Free, Meta, Data, Index };
const char* blockKindName(BlockKind);

// errno-like result codes (names as in POSIX so the viva can connect them).
enum class FsErr { Ok, NoEnt, Access, Exist, NotDir, IsDir, NotEmpty, NoSpc, Loop, BadF, Inval, Perm, FBig, NameTooLong };
const char* fsErrName(FsErr);

struct FsResult {
    FsErr err = FsErr::Ok;
    std::string message;                 // one-line explanation (why allowed / denied)
    int value = 0;                       // fd, inode id, byte count ... depending on the call
    std::string data;                    // bytes read
    bool ok() const { return err == FsErr::Ok; }
};

struct VUser { int uid = 0; std::string name; int gid = 0; std::vector<int> groups; };   // gid = primary
struct VGroup { int gid = 0; std::string name; };

struct Block {
    BlockKind kind = BlockKind::Free;
    int owner = -1;                      // inode id, -1 = none
    int gen = 0;                         // generation: incremented every time the block is allocated
    int next = -1;                       // linked allocation: next block of the file (-1 = end)
    std::vector<uint8_t> bytes;          // blockSize bytes (kept after free, see header comment)
};

struct Inode {
    int id = -1;
    bool used = false;
    FileType type = FileType::Regular;
    int mode = 0644;                     // 9 permission bits
    int uid = 0, gid = 0;
    int links = 0;                       // directory entries naming this inode
    int openRefs = 0;                    // open-file descriptions referring to it
    int size = 0;                        // bytes (regular files)
    std::vector<int> blocks;             // data blocks in logical order
    int indexBlock = -1;                 // indexed allocation
    std::string target;                  // symbolic link target path
    std::map<std::string, int> entries;  // directory: name -> inode id (".", ".." not stored)
    int parent = -1;                     // directory: parent directory inode
    long ctime = 0, mtime = 0, atime = 0;   // logical clock
};

// Open-file description (shared by dup()'ed descriptors and inherited ones).
struct OpenFile { int inode = -1; long offset = 0; bool rd = false, wr = false, append = false; int refs = 0; bool used = false; };
struct VProcess { int pid = 0; std::string name; int uid = 0; std::map<int, int> fds; std::string cwd = "/"; };

// Recovery metadata of one deleted inode (what a journal / deleted-inode record would keep).
struct DeletedRecord {
    int id = 0;                          // record number
    std::string path;                    // last pathname (metadata)
    int inode = -1;
    int uid = 0, gid = 0, mode = 0;
    int size = 0;
    AllocStrategy strategy = AllocStrategy::Contiguous;
    std::vector<int> blocks;             // logical order
    std::vector<int> gens;               // generation of each block when the file was deleted
    int indexBlock = -1, indexGen = 0;
    long deletedAt = 0;
    uint32_t checksum = 0;               // FNV-1a of the original content (verification only)
    bool metadataAvailable = true;       // false after "metadata loss" (only carving can help)
    bool restored = false;
};

enum class RecoveryClass { Full, Partial, Unrecoverable, Unknown };
const char* recoveryClassName(RecoveryClass);

struct BlockEvidence { int logical = 0, block = -1; bool intact = false; std::string reason; };
struct RecoveryReport {
    RecoveryClass cls = RecoveryClass::Unknown;
    int recordId = -1;
    std::string source;                  // "metadata" or "signature carving"
    std::vector<BlockEvidence> blocks;
    int recoverableBytes = 0, totalBytes = 0;
    std::string preview;                 // recovered bytes (overwritten regions as '?')
    std::vector<bool> byteIntact;        // per byte of preview
    bool verified = false;               // checksum of recovered bytes equals the original
    std::string explanation;
};

// Carving result: a synthetic "OSF1" signature found in a free block (see carve()).
struct CarveCandidate {
    int startBlock = -1;
    std::vector<int> blocks;
    int declaredLength = 0;
    bool footerOk = false;
    bool allFree = true;
    RecoveryReport report;
};

struct FsEvent { long time = 0; std::string user, op; bool ok = true; std::string text; };

struct DiskStats {
    int total = 0, meta = 0, data = 0, index = 0, free = 0;
    int largestFreeRun = 0, freeRuns = 0;
    int payloadBytes = 0;                // file bytes stored
    int capacityBytes = 0;               // bytes of the allocated data blocks
    int files = 0, dirs = 0, symlinks = 0;
    double externalFrag() const { return free == 0 ? 0.0 : 1.0 - double(largestFreeRun) / free; }
};

// One step of reading logical block k (access traversal under the strategy).
struct AccessStep { int block = -1; std::string what; };

class VirtualFs {
public:
    // blockCount 16..512, blockSize 32..1024 (multiple of 8). Throws std::invalid_argument.
    VirtualFs(int blockCount = 64, int blockSize = 64, AllocStrategy s = AllocStrategy::Indexed);
    static void validateGeometry(int blockCount, int blockSize);

    // ---- users, groups, processes
    const std::vector<VUser>& users() const { return users_; }
    const std::vector<VGroup>& groups() const { return groups_; }
    const VUser* user(int uid) const;
    const VUser* userByName(const std::string& n) const;
    std::string userName(int uid) const;
    std::string groupName(int gid) const;
    bool inGroup(int uid, int gid) const;
    int currentUser() const { return cur_; }
    bool setCurrentUser(int uid);
    int shellPid(int uid);                       // a process "sh" for that user (created on demand)
    int spawnProcess(const std::string& name, int uid);
    const std::map<int, VProcess>& processes() const { return procs_; }

    // ---- path operations (performed as the current user)
    FsResult resolve(const std::string& path, bool followLast = true, std::vector<std::string>* trace = nullptr) const;
    FsResult mkdir(const std::string& path, int mode = 0755);
    FsResult create(const std::string& path, int mode = 0644, const std::string& content = "");
    FsResult writeFile(const std::string& path, const std::string& content);      // truncate + write
    FsResult appendFile(const std::string& path, const std::string& content);
    FsResult readFile(const std::string& path);
    FsResult unlink(const std::string& path);
    FsResult rmdir(const std::string& path);
    FsResult rename(const std::string& from, const std::string& to);
    FsResult copy(const std::string& from, const std::string& to);
    FsResult link(const std::string& existing, const std::string& newPath);       // hard link
    FsResult symlink(const std::string& target, const std::string& linkPath);
    FsResult list(const std::string& path);                                        // names in data, '\n'
    FsResult chmod(const std::string& path, int mode);
    FsResult chown(const std::string& path, int uid);
    FsResult chgrp(const std::string& path, int gid);

    // ---- descriptors (per process)
    FsResult open(int pid, const std::string& path, bool rd, bool wr, bool create = false, bool trunc = false, bool append = false);
    FsResult read(int pid, int fd, int n);
    FsResult write(int pid, int fd, const std::string& bytes);
    FsResult lseek(int pid, int fd, long off);
    FsResult close(int pid, int fd);
    FsResult dup(int pid, int fd);
    const std::vector<OpenFile>& openFiles() const { return ofiles_; }

    // ---- permissions
    // Which class applies (owner / group / other) and whether access 'r','w','x' is granted.
    std::string accessClass(int uid, const Inode& n) const;
    bool may(int uid, const Inode& n, char what, std::string* why = nullptr) const;
    static std::string modeString(int mode, FileType t = FileType::Regular);    // "drwxr-x---"
    // Path resolution / operation check as ANOTHER user (does not change the current user or log).
    FsResult resolveAs(int uid, const std::string& path, bool followLast = true, std::vector<std::string>* trace = nullptr) const;
    // op: "read", "write", "execute", "list", "create" (a new entry inside the directory path),
    // "delete" (remove the entry path). Search permission on every traversed directory is included.
    FsResult checkAccess(int uid, const std::string& path, const std::string& op) const;
    static bool parseMode(const std::string& text, int oldMode, int& out);        // "750", "u+x,g-w", "rw-r-----"

    // ---- block layer
    int blockCount() const { return (int)blocks_.size(); }
    int blockSize() const { return blockSize_; }
    int payloadPerBlock() const { return strategy_ == AllocStrategy::Linked ? blockSize_ - 4 : blockSize_; }
    int pointersPerIndex() const { return blockSize_ / 4; }
    AllocStrategy strategy() const { return strategy_; }
    long maxFileBytes() const;                   // largest representable file under the strategy
    const std::vector<Block>& blocks() const { return blocks_; }
    const std::vector<Inode>& inodes() const { return inodes_; }
    const Inode* inode(int id) const { return id >= 0 && id < (int)inodes_.size() && inodes_[id].used ? &inodes_[id] : nullptr; }
    int rootInode() const { return 0; }
    std::string pathOf(int inode) const;          // one pathname of the inode ("" if none)
    DiskStats stats() const;
    std::vector<AccessStep> accessPath(int inode, int logicalBlock) const;
    std::string fileContent(int inode) const;     // bytes of a regular file (no permission check)

    // ---- recovery lab
    const std::vector<DeletedRecord>& deleted() const { return deleted_; }
    void loseMetadata(int recordId);              // simulate a lost journal / reused inode record
    RecoveryReport analyse(int recordId) const;   // metadata-based recovery analysis
    std::vector<CarveCandidate> carve() const;    // signature scan of free blocks
    // Restores the recovered bytes into a NEW file (never over an existing one); blocks still
    // holding intact evidence of any deleted record are not used. If space is insufficient the
    // call fails (ENOSPC) and nothing changes - the GUI then shows the preview only.
    FsResult restore(const RecoveryReport& r, const std::string& newPath);
    static std::string makeSignatureFile(const std::string& payload);   // "OSF1" + len + payload + "END!"
    static uint32_t checksum(const std::string& s);

    // ---- events, clock, persistence
    const std::vector<FsEvent>& events() const { return events_; }
    long clock() const { return clock_; }
    std::string saveImage() const;                // versioned text image
    static bool loadImage(const std::string& text, VirtualFs& out, std::string& err);   // validates everything
    std::vector<std::string> checkInvariants() const;   // empty = consistent (used by tests)

private:
    FsResult fail(FsErr e, const std::string& op, const std::string& why);
    FsResult okr(const std::string& op, const std::string& what, int value = 0);
    void log(const std::string& op, bool ok, const std::string& text);
    static std::vector<std::string> split(const std::string& path);
    FsResult lookupParent(const std::string& path, int& parent, std::string& name, std::vector<std::string>* trace = nullptr) const;
    FsResult walk(const std::string& path, bool followLast, int depth, std::vector<std::string>* trace) const;
    int newInode(FileType t, int mode, int uid, int gid);
    void releaseInodeIfUnused(int id);
    void freeBlocks(Inode& n);
    bool resizeTo(int id, int bytes, const std::set<int>& avoid = {});   // allocate / free blocks; false = nothing changed
    std::vector<int> findFree(int count, bool contiguous, const std::set<int>& avoid, int preferStart = -1) const;
    void takeBlock(int b, int owner, BlockKind k);
    void writeBytes(int id, long offset, const std::string& bytes);
    std::string readBytes(int id, long offset, int n) const;
    void relink(Inode& n);                        // linked strategy: rewrite next pointers
    FsResult openInternal(int pid, int inode, bool rd, bool wr, bool append);
    FsResult removeEntry(const std::string& path, bool dir);
    bool isAncestor(int dir, int maybeChild) const;

    int blockSize_;
    AllocStrategy strategy_;
    std::vector<Block> blocks_;
    std::vector<Inode> inodes_;
    std::vector<VUser> users_;
    std::vector<VGroup> groups_;
    std::map<int, VProcess> procs_;
    std::vector<OpenFile> ofiles_;
    std::vector<DeletedRecord> deleted_;
    std::vector<FsEvent> events_;
    int cur_ = 0, nextPid_ = 100;
    mutable int acting_ = -1;                     // resolveAs / checkAccess: user for the checks
    long clock_ = 0;
};

// Ready-made filesystem: users, home directories, a shared group directory, a public file,
// hard and symbolic links and synthetic signature files (used by every Storage & Files view).
void loadSampleFs(VirtualFs& fs);

// ---- Block Allocation comparison: the same workload on three isolated clones.
struct AllocWorkloadOp { enum Kind { Create, Extend, Delete } kind = Create; std::string name; int bytes = 0; };
struct AllocCompareRow {
    AllocStrategy strategy;
    std::vector<std::string> log;        // per operation: ok / failed + reason
    int failures = 0;
    DiskStats stats;
    int metadataBlocks = 0;              // index blocks / pointer bytes expressed in blocks
    int pointerBytes = 0;
    VirtualFs fs;
};
std::vector<AllocWorkloadOp> parseAllocWorkload(const std::string& text);   // "create a 300, extend a 100, delete b"
std::vector<AllocCompareRow> compareAllocation(const std::vector<AllocWorkloadOp>& ops, int blockCount, int blockSize);
struct AllocPreset { std::string name, description, workload; int blockCount, blockSize; };
std::vector<AllocPreset> allocPresets();

// ---- Recovery Lab presets: build a deterministic fixture on a fresh filesystem.
struct RecoveryPreset { std::string name, description; };
std::vector<RecoveryPreset> recoveryPresets();
void loadRecoveryPreset(VirtualFs& fs, int index);   // throws std::out_of_range
