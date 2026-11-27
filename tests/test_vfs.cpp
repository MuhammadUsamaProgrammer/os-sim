// test_vfs.cpp - Storage & Files: virtual filesystem, permissions, links, descriptors, block
// allocation strategies, recovery fixtures and image persistence (core/vfs.h).
#include <cstdio>
#include "check.h"
#include "vfs.h"

static bool consistent(const VirtualFs& fs) {
    std::vector<std::string> v = fs.checkInvariants();
    for (const std::string& s : v) std::printf("  invariant: %s\n", s.c_str());
    return v.empty();
}

static void testBasics() {
    for (AllocStrategy s : {AllocStrategy::Contiguous, AllocStrategy::Linked, AllocStrategy::Indexed}) {
        VirtualFs fs(64, 64, s);
        fs.setCurrentUser(0);
        CHECK(fs.mkdir("/a").ok());
        CHECK(fs.mkdir("/a/b").ok());
        CHECK(fs.mkdir("/a/b").err == FsErr::Exist);
        std::string text(150, 'x');
        text[0] = 'H'; text[149] = 'Z';
        CHECK(fs.create("/a/b/f.txt", 0644, text).ok());
        FsResult r = fs.readFile("/a/b/f.txt");
        CHECK(r.ok() && r.data == text);
        CHECK(fs.appendFile("/a/b/f.txt", "++").ok() && fs.readFile("/a/b/f.txt").data == text + "++");
        CHECK(fs.writeFile("/a/b/f.txt", "short").ok() && fs.readFile("/a/b/f.txt").data == "short");
        CHECK(fs.readFile("/a/x").err == FsErr::NoEnt);
        CHECK(fs.readFile("/a/b/f.txt/y").err == FsErr::NotDir);
        CHECK(fs.readFile("/a").err == FsErr::IsDir);
        CHECK(fs.rmdir("/a/b").err == FsErr::NotEmpty);
        CHECK(fs.rename("/a/b/f.txt", "/a/g.txt").ok());
        CHECK(fs.readFile("/a/g.txt").data == "short" && fs.readFile("/a/b/f.txt").err == FsErr::NoEnt);
        CHECK(fs.rename("/a", "/a/b/c").err == FsErr::Inval);          // into its own subtree
        CHECK(fs.copy("/a/g.txt", "/a/h.txt").ok());
        CHECK(fs.resolve("/a/g.txt").value != fs.resolve("/a/h.txt").value);   // copy = new inode
        CHECK(fs.rmdir("/a/b").ok());
        CHECK(fs.unlink("/a/g.txt").ok());
        CHECK(fs.list("/a").data == "h.txt\n");
        CHECK(fs.create("/a/bad/name").err == FsErr::NoEnt);
        CHECK(fs.create("/a/" + std::string(40, 'n')).err == FsErr::NameTooLong);
        CHECK(consistent(fs));
    }
}

static void testPermissions() {
    VirtualFs fs;
    loadSampleFs(fs);
    CHECK(consistent(fs));
    const int alice = 1000, bob = 1001, carol = 1002;
    // private file: owner only
    fs.setCurrentUser(bob);
    CHECK(fs.readFile("/home/alice/notes.txt").err == FsErr::Access);   // search denied on /home/alice (700)
    FsResult r = fs.list("/home/alice");
    CHECK(r.err == FsErr::Access);
    fs.setCurrentUser(alice);
    CHECK(fs.readFile("/home/alice/notes.txt").ok());
    // shared-group directory 770 root:staff: alice and carol in staff, bob not
    CHECK(fs.readFile("/shared/plan.txt").ok());
    fs.setCurrentUser(carol);
    CHECK(fs.appendFile("/shared/plan.txt", "carol was here\n").ok());
    fs.setCurrentUser(bob);
    CHECK(fs.readFile("/shared/plan.txt").err == FsErr::Access);
    // public readable
    CHECK(fs.readFile("/pub/readme.txt").ok());
    CHECK(fs.writeFile("/pub/readme.txt", "x").err == FsErr::Access);
    // precedence: owner bits apply to the owner even when group/other would allow more
    fs.setCurrentUser(alice);
    CHECK(fs.create("/tmp/p.txt", 0644, "data").ok());
    CHECK(fs.chmod("/tmp/p.txt", 0077).ok());
    CHECK(fs.readFile("/tmp/p.txt").err == FsErr::Access);              // owner class: ---
    fs.setCurrentUser(carol);                                            // carol: group users -> rwx
    CHECK(fs.readFile("/tmp/p.txt").ok());
    std::string why;
    CHECK(fs.accessClass(alice, *fs.inode(fs.resolve("/tmp/p.txt").value)) == "owner");
    // chmod: only owner or root; chown: only root; chgrp: owner to own group
    CHECK(fs.chmod("/tmp/p.txt", 0600).err == FsErr::Perm);
    fs.setCurrentUser(alice);
    CHECK(fs.chmod("/tmp/p.txt", 0600).ok());
    CHECK(fs.chown("/tmp/p.txt", bob).err == FsErr::Perm);
    CHECK(fs.chgrp("/tmp/p.txt", 101).ok());                             // alice is in staff
    CHECK(fs.chgrp("/tmp/p.txt", 0).err == FsErr::Perm);                 // not in root
    fs.setCurrentUser(0);
    CHECK(fs.chown("/tmp/p.txt", bob).ok());
    // deleting needs w+x on the PARENT, not on the file
    fs.setCurrentUser(alice);
    CHECK(fs.create("/home/alice/ro.txt", 0400, "read only").ok());
    CHECK(fs.writeFile("/home/alice/ro.txt", "y").err == FsErr::Access);
    CHECK(fs.unlink("/home/alice/ro.txt").ok());                         // file mode is irrelevant
    fs.setCurrentUser(bob);
    CHECK(fs.unlink("/pub/readme.txt").err == FsErr::Access);            // /pub 755 root
    // directory: r lists names, x traverses
    fs.setCurrentUser(0);
    CHECK(fs.mkdir("/xonly", 0711).ok() && fs.create("/xonly/f", 0644, "visible by path").ok());
    CHECK(fs.mkdir("/ronly", 0744).ok() && fs.create("/ronly/f", 0644, "hidden").ok());
    fs.setCurrentUser(bob);
    CHECK(fs.list("/xonly").err == FsErr::Access && fs.readFile("/xonly/f").ok());
    CHECK(fs.list("/ronly").ok() && fs.readFile("/ronly/f").err == FsErr::Access);
    // effective-access checker for other users (does not switch user)
    CHECK(fs.checkAccess(bob, "/xonly", "list").err == FsErr::Access && fs.checkAccess(bob, "/xonly", "execute").ok());
    CHECK(fs.checkAccess(1000, "/shared/plan.txt", "write").ok() && fs.checkAccess(bob, "/shared/plan.txt", "read").err == FsErr::Access);
    CHECK(fs.checkAccess(bob, "/pub/readme.txt", "delete").err == FsErr::Access && fs.checkAccess(1000, "/home/alice/notes.txt", "delete").ok());
    CHECK(fs.checkAccess(bob, "/tmp", "create").ok() && fs.currentUser() == bob);
    // root bypass (simplified privilege policy)
    fs.setCurrentUser(0);
    CHECK(fs.readFile("/home/alice/notes.txt").ok());
    // mode parsing
    int m = 0;
    CHECK(VirtualFs::parseMode("750", 0, m) && m == 0750);
    CHECK(VirtualFs::parseMode("rw-r-----", 0, m) && m == 0640);
    CHECK(VirtualFs::parseMode("u+x,g-w,o=r", 0664, m) && m == 0744);
    CHECK(VirtualFs::parseMode("a+r", 0, m) && m == 0444);
    CHECK(!VirtualFs::parseMode("9z", 0, m) && !VirtualFs::parseMode("q+r", 0, m));
    CHECK(VirtualFs::modeString(0750, FileType::Directory) == "drwxr-x---");
    CHECK(consistent(fs));
}

static void testLinks() {
    VirtualFs fs;
    loadSampleFs(fs);
    fs.setCurrentUser(1002);
    int a = fs.resolve("/pub/readme.txt").value, b = fs.resolve("/home/carol/readme-link").value;
    CHECK(a == b && fs.inode(a)->links == 2);                            // hard link: same inode
    CHECK(fs.readFile("/home/carol/pub/readme.txt").ok());               // through a symlink
    CHECK(fs.readFile("/home/carol/old").err == FsErr::NoEnt);           // dangling
    CHECK(fs.resolve("/home/carol/old", false).ok());                    // the link itself exists
    CHECK(fs.readFile("/tmp/loopA").err == FsErr::Loop);                 // loop detection
    CHECK(fs.link("/home/carol", "/tmp/dirlink").err == FsErr::Perm);    // no directory hard links
    fs.setCurrentUser(0);
    CHECK(fs.unlink("/pub/readme.txt").ok());
    CHECK(fs.inode(a) && fs.inode(a)->links == 1 && fs.readFile("/home/carol/readme-link").ok());
    CHECK(fs.deleted().empty());                                         // not freed: one name left
    CHECK(fs.readFile("/home/carol/pub/readme.txt").err == FsErr::NoEnt);
    CHECK(fs.unlink("/home/carol/readme-link").ok() && !fs.inode(a) && fs.deleted().size() == 1);
    CHECK(consistent(fs));
}

static void testDescriptors() {
    VirtualFs fs;
    fs.setCurrentUser(0);
    fs.mkdir("/tmp", 0777);
    fs.setCurrentUser(1000);
    CHECK(fs.create("/tmp/d.txt", 0644, "0123456789").ok());
    int p = fs.spawnProcess("reader", 1000);
    int q = fs.spawnProcess("other", 1001);
    FsResult o = fs.open(p, "/tmp/d.txt", true, true);
    CHECK(o.ok() && o.value == 3);
    int fd = o.value;
    CHECK(fs.read(p, fd, 4).data == "0123");
    int fd2 = fs.dup(p, fd).value;
    CHECK(fd2 == 4 && fs.read(p, fd2, 3).data == "456");                 // shared offset
    int fd3 = fs.open(p, "/tmp/d.txt", true, false).value;               // separate description
    CHECK(fs.read(p, fd3, 2).data == "01");
    CHECK(fs.lseek(p, fd, 8).ok() && fs.read(p, fd2, 10).data == "89");
    CHECK(fs.read(p, fd2, 10).data.empty());                             // end of file
    CHECK(fs.write(p, fd3, "x").err == FsErr::BadF);                     // read-only descriptor
    CHECK(fs.read(q, fd, 1).err == FsErr::BadF);                         // fd tables are per process
    CHECK(fs.open(q, "/tmp/d.txt", false, true).err == FsErr::Access);   // bob: other r--
    CHECK(fs.lseek(p, fd, 2).ok() && fs.write(p, fd, "AB").ok() && fs.readFile("/tmp/d.txt").data == "01AB456789");
    int fa = fs.open(p, "/tmp/d.txt", false, true, false, false, true).value;
    CHECK(fs.write(p, fa, "!").ok() && fs.readFile("/tmp/d.txt").data == "01AB456789!");
    // unlink while open: data remains readable through the descriptor until the last close
    int id = fs.resolve("/tmp/d.txt").value;
    CHECK(fs.unlink("/tmp/d.txt").ok());
    CHECK(fs.inode(id) && fs.inode(id)->links == 0 && fs.deleted().empty());
    CHECK(fs.lseek(p, fd3, 0).ok() && fs.read(p, fd3, 3).data == "01A");
    CHECK(consistent(fs));
    CHECK(fs.close(p, fd).ok() && fs.inode(id));                         // fd2 still shares it
    CHECK(fs.close(p, fd2).ok() && fs.close(p, fd3).ok() && fs.inode(id));
    CHECK(fs.close(p, fa).ok() && !fs.inode(id) && fs.deleted().size() == 1);
    CHECK(fs.close(p, fa).err == FsErr::BadF);
    CHECK(consistent(fs));
}

static void testAllocation() {
    // full disk rollback: nothing changes when an allocation fails
    for (AllocStrategy s : {AllocStrategy::Contiguous, AllocStrategy::Linked, AllocStrategy::Indexed}) {
        VirtualFs fs(16, 32, s);
        fs.setCurrentUser(0);
        DiskStats before = fs.stats();
        CHECK(before.free == 12 && before.meta == 4);
        CHECK(fs.create("/a", 0644, std::string(200, 'a')).ok());
        std::string img = fs.saveImage();
        FsResult r = fs.create("/b", 0644, std::string(400, 'b'));
        CHECK((r.err == FsErr::NoSpc || r.err == FsErr::FBig) && !fs.resolve("/b").ok());
        FsErr ae = fs.appendFile("/a", std::string(400, 'c')).err;
        CHECK((ae == FsErr::NoSpc || ae == FsErr::FBig) && fs.readFile("/a").data == std::string(200, 'a'));
        CHECK(consistent(fs));
        DiskStats st = fs.stats();
        CHECK(st.free + st.data + st.index + st.meta == st.total);
        int payload = fs.payloadPerBlock();
        CHECK(st.data == (200 + payload - 1) / payload);
        CHECK(st.index == (s == AllocStrategy::Indexed ? 1 : 0));
        (void)img;
    }
    // access traversal
    VirtualFs lf(32, 64, AllocStrategy::Linked);
    lf.setCurrentUser(0);
    lf.create("/f", 0644, std::string(300, 'z'));
    int id = lf.resolve("/f").value;
    CHECK(lf.accessPath(id, 4).size() == 5);
    VirtualFs xf(32, 64, AllocStrategy::Indexed);
    xf.setCurrentUser(0);
    xf.create("/f", 0644, std::string(300, 'z'));
    CHECK(xf.accessPath(xf.resolve("/f").value, 4).size() == 2);
    CHECK(xf.create("/huge", 0644, std::string(17 * 64, 'h')).err == FsErr::FBig);    // 16 pointers per index block
    // preset: contiguous fails despite enough free blocks, linked and indexed succeed
    AllocPreset p = allocPresets()[0];
    std::vector<AllocCompareRow> rows = compareAllocation(parseAllocWorkload(p.workload), p.blockCount, p.blockSize);
    CHECK(rows.size() == 3);
    CHECK(rows[0].failures == 1 && rows[0].log.back().find("ENOSPC") != std::string::npos);
    CHECK(rows[0].log.back().find("largest run is 12") != std::string::npos);
    // the "enough free space" premise holds for the contiguous disk
    CHECK(rows[0].stats.free >= 8);
    CHECK(rows[1].failures == 0 && rows[2].failures == 0);
    for (const AllocCompareRow& r : rows) CHECK(r.fs.checkInvariants().empty());
    // contiguous relocation keeps contents
    AllocPreset g = allocPresets()[1];
    std::vector<AllocCompareRow> gr = compareAllocation(parseAllocWorkload(g.workload), g.blockCount, g.blockSize);
    for (AllocCompareRow& r : gr) {
        r.fs.setCurrentUser(0);
        CHECK(r.fs.readFile("/a").data.size() == 600 && r.fs.checkInvariants().empty());
    }
    CHECK_THROWS(parseAllocWorkload("grow a 3"), std::invalid_argument);
    CHECK_THROWS(VirtualFs(8, 64), std::invalid_argument);
}

static void testRecovery() {
    VirtualFs fs;
    // intact
    loadRecoveryPreset(fs, 0);
    CHECK(fs.deleted().size() == 1);
    RecoveryReport r = fs.analyse(0);
    std::string original = "Alice's private notes: inode = metadata, a directory entry = a name. Unlink removes a name; "
                           "the data is freed when no name and no descriptor is left.\n";
    CHECK(r.cls == RecoveryClass::Full && r.verified && r.preview == original && r.recoverableBytes == (int)original.size());
    CHECK(fs.restore(r, "/recovered/notes.txt").ok());
    CHECK(fs.readFile("/recovered/notes.txt").data == original);
    CHECK(fs.restore(r, "/recovered/notes.txt").err == FsErr::Exist);       // never overwrite
    CHECK(fs.analyse(0).cls == RecoveryClass::Full);                      // evidence blocks untouched
    CHECK(consistent(fs));
    // partially overwritten: the first block was reused
    loadRecoveryPreset(fs, 1);
    r = fs.analyse(0);
    int payload = fs.payloadPerBlock();
    CHECK(r.cls == RecoveryClass::Partial && !r.blocks[0].intact && r.blocks[1].intact);
    CHECK(r.recoverableBytes == (int)original.size() - payload);
    CHECK(r.preview.substr(payload) == original.substr(payload) && r.preview.substr(0, payload) == std::string(payload, '?'));
    CHECK(!r.verified);
    // fully overwritten
    loadRecoveryPreset(fs, 2);
    r = fs.analyse(0);
    CHECK(r.cls == RecoveryClass::Unrecoverable && r.recoverableBytes == 0);
    // a reused-and-freed-again block is not reported as intact
    loadRecoveryPreset(fs, 1);
    fs.unlink("/home/alice/new.txt");
    r = fs.analyse(0);
    CHECK(r.cls == RecoveryClass::Partial && !r.blocks[0].intact);
    // metadata lost: carving finds the signature, but cannot prove integrity
    loadRecoveryPreset(fs, 3);
    CHECK(fs.analyse(0).cls == RecoveryClass::Unknown);
    std::vector<CarveCandidate> c = fs.carve();
    CHECK(c.size() == 1 && c[0].footerOk && c[0].allFree && c[0].report.cls == RecoveryClass::Unknown);
    CHECK(c[0].report.preview == VirtualFs::makeSignatureFile("Bob's synthetic report: carving finds files by their OSF1 header and END! footer."));
    // with the metadata record available carving is confirmed by generations
    VirtualFs g;
    loadSampleFs(g);
    g.setCurrentUser(1001);
    g.unlink("/home/bob/report.osf");
    std::vector<CarveCandidate> c2 = g.carve();
    CHECK(c2.size() == 1 && c2[0].report.cls == RecoveryClass::Full && c2[0].report.verified);
    // partially overwritten signature file
    g.setCurrentUser(0);
    g.create("/tmp/fill", 0644, std::string(70, 'F'));
    c2 = g.carve();
    CHECK(c2.empty() || c2[0].report.cls != RecoveryClass::Full);
    // hard link + open descriptor lifetime
    loadRecoveryPreset(fs, 4);
    CHECK(fs.deleted().empty());
    int pid = -1;
    for (auto& kv : fs.processes()) if (kv.second.name == "logger") pid = kv.first;
    CHECK(pid > 0 && fs.read(pid, 3, 5).data == "A log");
    CHECK(fs.close(pid, 3).ok() && fs.deleted().size() == 1 && fs.analyse(0).cls == RecoveryClass::Full);
    // restore into insufficient space -> preview only, nothing changed
    VirtualFs tiny(16, 32, AllocStrategy::Contiguous);
    tiny.setCurrentUser(0);
    tiny.create("/g", 0644, std::string(32 * 7, 'g'));
    tiny.create("/f", 0644, std::string(150, 'q'));
    tiny.unlink("/f");                         // its 5 blocks are the only free ones: all evidence
    RecoveryReport tr = tiny.analyse(0);
    CHECK(tr.cls == RecoveryClass::Full);
    DiskStats sb = tiny.stats();
    CHECK(tiny.restore(tr, "/f.rec").err == FsErr::NoSpc && !tiny.resolve("/f.rec").ok());
    CHECK(tiny.stats().free == sb.free && tiny.analyse(0).cls == RecoveryClass::Full && tiny.checkInvariants().empty());
}

static void testPersistence() {
    VirtualFs fs;
    loadSampleFs(fs);
    fs.unlink("/home/alice/todo.txt");
    std::string img = fs.saveImage();
    VirtualFs back;
    std::string err;
    CHECK(VirtualFs::loadImage(img, back, err));
    CHECK(back.saveImage() == img);
    back.setCurrentUser(1000);
    CHECK(back.readFile("/home/alice/notes.txt").ok() && back.deleted().size() == 1);
    CHECK(!VirtualFs::loadImage("garbage", back, err) && !err.empty());
    CHECK(!VirtualFs::loadImage(img.substr(0, img.size() / 2), back, err));
    std::string broken = img;
    size_t pos = broken.find("geometry 64");
    broken.replace(pos, 11, "geometry 65");
    CHECK(!VirtualFs::loadImage(broken, back, err));
    // a corrupted block owner is caught by the invariant check
    std::string evil = img;
    size_t b5 = evil.find("\nblock 5 ");
    size_t sp = evil.find(' ', b5 + 9);
    evil.replace(b5 + 9, sp - b5 - 9, "0");     // kind field -> free while a file still uses it
    CHECK(!VirtualFs::loadImage(evil, back, err));
}

int main() {
    testBasics();
    testPermissions();
    testLinks();
    testDescriptors();
    testAllocation();
    testRecovery();
    testPersistence();
    return report("test_vfs");
}
