// StorageTab.cpp - Storage & Files (Phase 3): File Explorer, Access Control, Block Allocation,
// Recovery Lab over one shared VirtualFs (see StorageTab.h).
#include "StorageTab.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <functional>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "ModuleKit.h"
#include "NativeRunner.h"
#include "StorageViews.h"
#include "Style.h"

namespace {
QSpinBox* spin(int lo, int hi, int v, const QString& tip = QString()) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(v);
    if (!tip.isEmpty()) s->setToolTip(tip);
    return s;
}
QTableWidget* table(const QStringList& headers) {
    auto* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}
void setRow(QTableWidget* t, int r, const QStringList& cells) {
    for (int k = 0; k < cells.size(); ++k) t->setItem(r, k, ui::roItem(cells[k]));
}
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.toStdString(); }
QPushButton* button(const QString& text, const QString& tip = QString()) {
    auto* b = new QPushButton(text);
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}
// Rows of buttons that wrap into a grid (two per row) so narrow panels stay usable.
QWidget* buttonGrid(const QList<QPushButton*>& bs, int perRow = 2) {
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setSpacing(4);
    for (int i = 0; i < bs.size(); ++i) g->addWidget(bs[i], i / perRow, i % perRow);
    return w;
}
QString joinPath(const QString& dir, const QString& name) {
    if (name.startsWith('/')) return name;
    return dir == "/" ? "/" + name : dir + "/" + name;
}
QString parentOf(const QString& p) {
    int i = p.lastIndexOf('/');
    return i <= 0 ? "/" : p.left(i);
}
void fillUsers(QComboBox* c, const VirtualFs& fs) {
    c->clear();
    for (const VUser& u : fs.users()) c->addItem(QString("%1 (uid %2)").arg(q(u.name)).arg(u.uid), u.uid);
    c->setCurrentIndex(c->findData(fs.currentUser()));
}
// Every path of the filesystem (directories first by tree order).
QStringList allPaths(const VirtualFs& fs) {
    QStringList out;
    std::vector<std::pair<int, std::string>> q = {{0, "/"}};
    for (size_t i = 0; i < q.size(); ++i) {
        out << QString::fromStdString(q[i].second);
        const Inode* d = fs.inode(q[i].first);
        if (!d || d->type != FileType::Directory) continue;
        for (auto& kv : d->entries) q.push_back({kv.second, (q[i].second == "/" ? "/" : q[i].second + "/") + kv.first});
    }
    return out;
}
}

// ======================================================================= DiskControls

DiskControls::DiskControls(StorageArea* area, QWidget* parent) : QWidget(parent), area_(area) {
    blocks_ = spin(16, 512, 64, "Number of blocks of the new virtual disk (blocks 0-3 are metadata)");
    size_ = spin(32, 1024, 64, "Block size in bytes (multiple of 8)");
    size_->setSingleStep(32);
    strategy_ = new QComboBox;
    for (AllocStrategy s : {AllocStrategy::Contiguous, AllocStrategy::Linked, AllocStrategy::Indexed}) strategy_->addItem(allocStrategyName(s));
    strategy_->setToolTip("The allocation strategy is fixed per virtual disk: changing it needs a new disk (existing files are never reinterpreted)");
    auto* mk = button("New virtual disk", "Creates an empty in-memory disk with this geometry and strategy");
    auto* sample = button("Load sample", "New disk with users' home directories, a shared group directory, links and signature files");
    auto* save = button("Save image...", "Writes the virtual disk to a versioned text image (only to a file you choose)");
    auto* load = button("Load image...", "Reads a saved image; size, structure, indices and paths are validated first");
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    auto* f = new QFormLayout(this);
    f->setContentsMargins(0, 0, 0, 0);
    f->addRow("Blocks:", blocks_);
    f->addRow("Block size:", size_);
    f->addRow("Strategy:", strategy_);
    f->addRow(buttonGrid({mk, sample, save, load}));
    f->addRow(summary_);
    auto strat = [this] { return (AllocStrategy)strategy_->currentIndex(); };
    connect(mk, &QPushButton::clicked, this, [this, strat] { area_->newDisk(blocks_->value(), size_->value() / 8 * 8, strat(), false); });
    connect(sample, &QPushButton::clicked, this, [this, strat] { area_->newDisk(blocks_->value(), size_->value() / 8 * 8, strat(), true); });
    connect(save, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getSaveFileName(this, "Save virtual disk image", "virtual_disk.osvfs", "OS-Sim virtual disk (*.osvfs)");
        if (p.isEmpty()) return;
        QString err;
        if (!area_->saveImage(p, &err)) ui::warn(this, err);
    });
    connect(load, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getOpenFileName(this, "Load virtual disk image", QString(), "OS-Sim virtual disk (*.osvfs);;All files (*)");
        if (p.isEmpty()) return;
        QString err;
        if (!area_->loadImage(p, &err)) ui::warn(this, err);
    });
    connect(area_, &StorageArea::changed, this, &DiskControls::sync);
    sync();
}

void DiskControls::sync() {
    const VirtualFs& fs = area_->fs();
    DiskStats st = fs.stats();
    blocks_->setValue(fs.blockCount());           // the controls show the CURRENT disk (edit them for a new one)
    size_->setValue(fs.blockSize());
    strategy_->setCurrentIndex((int)fs.strategy());
    summary_->setText(QString("Current disk: %1 blocks x %2 B, %3 allocation. %4 free, %5 data, %6 index, %7 metadata blocks.")
                          .arg(fs.blockCount()).arg(fs.blockSize()).arg(allocStrategyName(fs.strategy()))
                          .arg(st.free).arg(st.data).arg(st.index).arg(st.meta));
}

// ======================================================================= StorageArea

StorageArea::StorageArea(QWidget* parent) : QTabWidget(parent), fs_(64, 64, AllocStrategy::Indexed) {
    loadSampleFs(fs_);
    explorer_ = new ExplorerPage(this);
    access_ = new AccessPage(this);
    blocks_ = new BlockPage(this);
    recovery_ = new RecoveryPage(this);
    addTab(explorer_, "File Explorer");
    addTab(access_, "Access Control");
    addTab(blocks_, "Block Allocation");
    addTab(recovery_, "Recovery Lab");
    notifyChanged();
}

void StorageArea::notifyChanged() { emit changed(); }

void StorageArea::report(const FsResult& r, const QString& what) {
    emit logMessage(QString("Storage: %1 -> %2").arg(what, q(r.message)));
    emit changed();
}

bool StorageArea::newDisk(int blocks, int blockSize, AllocStrategy s, bool sample) {
    try {
        VirtualFs fresh(blocks, blockSize, s);
        if (sample) loadSampleFs(fresh);
        replaceFs(fresh, QString("new %1 virtual disk %2 x %3 B%4").arg(allocStrategyName(s)).arg(blocks).arg(blockSize).arg(sample ? " with sample files" : ""));
        return true;
    } catch (const std::exception& e) {
        ui::warn(this, e.what());
        return false;
    }
}

void StorageArea::replaceFs(const VirtualFs& fs, const QString& why) {
    fs_ = fs;
    explorer_->setCwd("/");
    emit logMessage("Storage: " + why);
    emit changed();
}

bool StorageArea::saveImage(const QString& path, QString* err) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) { if (err) *err = "Cannot write " + path + ": " + f.errorString(); return false; }
    f.write(QByteArray::fromStdString(fs_.saveImage()));
    return true;
}

bool StorageArea::loadImage(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { if (err) *err = "Cannot read " + path + ": " + f.errorString(); return false; }
    if (f.size() > 4 * 1024 * 1024) { if (err) *err = "The image is larger than 4 MB: refused"; return false; }
    VirtualFs fs;
    std::string e;
    if (!VirtualFs::loadImage(f.readAll().toStdString(), fs, e)) { if (err) *err = "Invalid virtual disk image: " + q(e); return false; }
    replaceFs(fs, "loaded image " + path);
    return true;
}

QString StorageArea::reportText() const {
    DiskStats st = fs_.stats();
    QString t = QString("Virtual disk: %1 blocks x %2 bytes, %3 allocation\n").arg(fs_.blockCount()).arg(fs_.blockSize()).arg(allocStrategyName(fs_.strategy()));
    t += QString("Blocks: %1 free, %2 data, %3 index, %4 metadata; largest free run %5; files %6, directories %7, symbolic links %8\n")
             .arg(st.free).arg(st.data).arg(st.index).arg(st.meta).arg(st.largestFreeRun).arg(st.files).arg(st.dirs).arg(st.symlinks);
    for (const QString& p : allPaths(fs_)) {
        FsResult r = fs_.resolve(s(p), false);
        if (!r.ok()) continue;
        const Inode* n = fs_.inode(r.value);
        t += QString("  %1 %2 %3:%4 %5 B inode %6 links %7\n").arg(q(VirtualFs::modeString(n->mode, n->type)), -10).arg(p)
                 .arg(q(fs_.userName(n->uid)), q(fs_.groupName(n->gid))).arg(n->size).arg(n->id).arg(n->links);
    }
    t += QString("Deleted records: %1\n").arg(fs_.deleted().size());
    for (const DeletedRecord& d : fs_.deleted()) {
        RecoveryReport r = fs_.analyse(d.id);
        t += QString("  #%1 %2 (%3 B): %4, %5 bytes recoverable\n").arg(d.id).arg(d.metadataAvailable ? q(d.path) : "(metadata lost)")
                 .arg(d.size).arg(recoveryClassName(r.cls)).arg(r.recoverableBytes);
    }
    if (!blocks_->comparison().empty()) {
        t += "Block allocation comparison:\n";
        for (const AllocCompareRow& r : blocks_->comparison())
            t += QString("  %1: %2 failure(s), %3 data + %4 index blocks, %5 free, largest run %6\n").arg(allocStrategyName(r.strategy))
                     .arg(r.failures).arg(r.stats.data).arg(r.stats.index).arg(r.stats.free).arg(r.stats.largestFreeRun);
    }
    return t;
}

QString StorageArea::csvSection() const {
    QString c = "## Storage and files\nstrategy,blocks,block_size,free,data,index,meta,largest_free_run,files,dirs,deleted_records\n";
    DiskStats st = fs_.stats();
    c += QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11\n").arg(allocStrategyName(fs_.strategy())).arg(fs_.blockCount()).arg(fs_.blockSize())
             .arg(st.free).arg(st.data).arg(st.index).arg(st.meta).arg(st.largestFreeRun).arg(st.files).arg(st.dirs).arg(fs_.deleted().size());
    return c;
}

// ======================================================================= File Explorer

ExplorerPage::ExplorerPage(StorageArea* area) : area_(area) {
    header_ = new ModuleHeader("File Explorer",
        "An isolated in-memory filesystem: directory entries name inodes, inodes own blocks. Create, edit, copy, move, link and "
        "delete files; open descriptors per process. Nothing here touches the host disk.");
    // ---- configuration
    auto* disk = new DiskControls(area);
    user_ = new QComboBox;
    user_->setToolTip("Operations run with this simulated user's permissions");
    path_ = new QLineEdit;
    path_->setPlaceholderText("name or /absolute/path");
    path_->setToolTip("Name (relative to the current directory) or absolute virtual path");
    dest_ = new QLineEdit;
    dest_->setPlaceholderText("destination / link target");
    auto* bNew = button("New file", "Create an empty regular file (needs w+x on the directory)");
    auto* bDir = button("New directory");
    auto* bDel = button("Delete", "Unlink a file or remove an empty directory (needs w+x on the PARENT directory)");
    auto* bRen = button("Rename / move", "Renames to the destination: only directory entries change, the inode stays");
    auto* bCopy = button("Copy", "Copy to the destination: a new inode with its own blocks");
    auto* bLink = button("Hard link", "Destination becomes a second name for the same inode (not for directories)");
    auto* bSym = button("Symbolic link", "Creates the path as a link whose content is the destination text");
    auto* bUp = button("Parent directory");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow(titledBox("Virtual disk", disk));
    cf->addRow("Current user:", user_);
    cf->addRow("Path:", path_);
    cf->addRow("Destination:", dest_);
    cf->addRow(buttonGrid({bNew, bDir, bDel, bRen, bCopy, bLink, bSym, bUp}));

    // ---- centre: tree | (breadcrumb, list, editor)
    tree_ = new QTreeWidget;
    tree_->setHeaderLabel("Directories");
    tree_->setMinimumWidth(120);
    crumb_ = new QLabel;
    crumb_->setWordWrap(true);
    list_ = table({"Name", "Type", "Inode", "Size", "Mode", "Owner", "Group", "Links", "Blocks"});
    editor_ = new QPlainTextEdit;
    editor_->setPlaceholderText("Select a regular file to view or edit its content");
    auto* bSave = button("Write (replace)", "write(): replaces the whole content (all-or-nothing: on a full disk nothing changes)");
    auto* bAppend = button("Append", "Appends the editor text to the file");
    auto* bReload = button("Reload");
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(crumb_);
    auto* vs = new QSplitter(Qt::Vertical);
    vs->addWidget(list_);
    auto* edW = new QWidget;
    auto* el = new QVBoxLayout(edW);
    el->setContentsMargins(0, 0, 0, 0);
    el->addWidget(editor_, 1);
    el->addWidget(buttonGrid({bSave, bAppend, bReload}, 3));
    vs->addWidget(edW);
    vs->setStretchFactor(0, 3);
    vs->setStretchFactor(1, 2);
    rl->addWidget(vs, 1);
    auto* center = new QSplitter(Qt::Horizontal);
    center->addWidget(tree_);
    center->addWidget(right);
    center->setStretchFactor(0, 1);
    center->setStretchFactor(1, 3);

    // ---- inspector
    meta_ = new QLabel;
    meta_->setWordWrap(true);
    meta_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    trace_ = new QListWidget;
    trace_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Inode (metadata)</b>"));
    il->addWidget(meta_);
    il->addWidget(new QLabel("<b>Path resolution</b>"));
    il->addWidget(trace_, 1);

    // ---- bottom: events, descriptors, native demo
    auto* bottom = new QTabWidget;
    auto* events = table({"Time", "User", "Operation", "Result", "Explanation"});
    events->setObjectName("fsEvents");
    bottom->addTab(events, "Event log");
    proc_ = new QComboBox;
    proc_->setToolTip("Simulated process whose descriptor table is used");
    fd_ = spin(0, 64, 3, "Descriptor number");
    fdArg_ = new QLineEdit;
    fdArg_->setPlaceholderText("bytes to write / count / offset");
    auto* bNewProc = button("New process", "Adds a process for the current user (its own descriptor table)");
    auto* oR = button("open O_RDONLY");
    auto* oRW = button("open O_RDWR");
    auto* oA = button("open O_APPEND");
    auto* fRead = button("read(fd, n)");
    auto* fWrite = button("write(fd, text)");
    auto* fSeek = button("lseek(fd, off)");
    auto* fDup = button("dup(fd)");
    auto* fClose = button("close(fd)");
    fdTable_ = table({"Process", "User", "fd", "Open-file entry", "Inode", "Offset"});
    oft_ = table({"Entry", "Inode", "Path", "Offset", "Access", "Descriptors"});
    auto* fdW = new QWidget;
    auto* fl = new QGridLayout(fdW);
    auto* ctl = new QWidget;
    auto* cl = new QFormLayout(ctl);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addRow("Process:", proc_);
    cl->addRow("fd:", fd_);
    cl->addRow("Argument:", fdArg_);
    cl->addRow(buttonGrid({bNewProc, oR, oRW, oA, fRead, fWrite, fSeek, fDup, fClose}, 3));
    auto* note = new QLabel("close() removes a descriptor; unlink() removes a name. The inode and its blocks are freed only when "
                            "both the link count and the open references reach 0.");
    note->setWordWrap(true);
    cl->addRow(note);
    fl->addWidget(ctl, 0, 0, 2, 1);
    fl->addWidget(new QLabel("<b>Per-process descriptor tables</b>"), 0, 1);
    fl->addWidget(fdTable_, 1, 1);
    fl->addWidget(new QLabel("<b>Open-file table (shared offsets)</b>"), 0, 2);
    fl->addWidget(oft_, 1, 2);
    fl->setColumnStretch(1, 1);
    fl->setColumnStretch(2, 1);
    bottom->addTab(fdW, "Descriptors");
    // Live Linux: real open/write/read/fstat/link/readdir/unlink/close in the helper's own temp directory
    native_ = new NativeRunner(this);
    nativeTable_ = table({"Time (ms)", "Role", "PID", "PPID", "Event", "Detail"});
    auto* nW = new QWidget;
    auto* nl = new QVBoxLayout(nW);
    auto* nRun = button("Run Live Linux syscall demo", "ossim_helper creates its OWN temporary directory, performs real system calls on "
                                                       "its own files and removes everything again");
    nRun->setEnabled(NativeRunner::available());
    auto* nLbl = new QLabel(NativeRunner::available()
        ? "<b>Live Linux Demo</b> (real system calls, separate from the simulation above): open, write, lseek, read, fstat, link, "
          "readdir, unlink while open, close, rmdir - only inside a private mkdtemp() directory."
        : "<b>Live Linux Demo unavailable</b> on this platform; the simulated filesystem works everywhere.");
    nLbl->setWordWrap(true);
    nl->addWidget(nLbl);
    nl->addWidget(nRun);
    nl->addWidget(nativeTable_, 1);
    bottom->addTab(nW, "Live Linux syscalls");

    assembleModule(this, header_, cfgW, center, titledBox("Inspector", insW), nullptr, bottom);

    // ---- connections
    connect(area_, &StorageArea::changed, this, &ExplorerPage::refresh);
    connect(user_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_ || i < 0) return;
        area_->fs().setCurrentUser(user_->itemData(i).toInt());
        area_->notifyChanged();
    });
    auto target = [this] { return joinPath(cwd_, path_->text().trimmed()); };
    auto destPath = [this] { return joinPath(cwd_, dest_->text().trimmed()); };
    connect(bNew, &QPushButton::clicked, this, [=] { apply("create", target()); });
    connect(bDir, &QPushButton::clicked, this, [=] { apply("mkdir", target()); });
    connect(bDel, &QPushButton::clicked, this, [=] {
        FsResult r = area_->fs().resolve(s(target()), false);
        bool dir = r.ok() && area_->fs().inode(r.value)->type == FileType::Directory;
        apply(dir ? "rmdir" : "delete", target());
    });
    connect(bRen, &QPushButton::clicked, this, [=] { apply("rename", target(), destPath()); });
    connect(bCopy, &QPushButton::clicked, this, [=] { apply("copy", target(), destPath()); });
    connect(bLink, &QPushButton::clicked, this, [=] { apply("link", target(), destPath()); });
    connect(bSym, &QPushButton::clicked, this, [=] { apply("symlink", target(), dest_->text().trimmed()); });
    connect(bUp, &QPushButton::clicked, this, [this] { setCwd(parentOf(cwd_)); });
    connect(bSave, &QPushButton::clicked, this, [this] { if (!selected_.isEmpty()) apply("write", selected_, editor_->toPlainText()); });
    connect(bAppend, &QPushButton::clicked, this, [this] { if (!selected_.isEmpty()) apply("append", selected_, editor_->toPlainText()); });
    connect(bReload, &QPushButton::clicked, this, [this] { refreshInspector(); });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) { setCwd(it->data(0, Qt::UserRole).toString()); });
    connect(list_, &QTableWidget::cellClicked, this, [this](int r, int) {
        if (auto* it = list_->item(r, 0)) select(joinPath(cwd_, it->text()));
    });
    connect(list_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        if (!list_->item(r, 0)) return;
        QString p = joinPath(cwd_, list_->item(r, 0)->text());
        FsResult res = area_->fs().resolve(s(p));
        if (res.ok() && area_->fs().inode(res.value)->type == FileType::Directory) setCwd(p);
    });
    connect(bNewProc, &QPushButton::clicked, this, [this] {
        int pid = area_->fs().spawnProcess("proc", area_->fs().currentUser());
        area_->notifyChanged();
        proc_->setCurrentIndex(proc_->findData(pid));
    });
    auto fdo = [this](const QString& op) {
        QString arg = op.startsWith("open") ? (selected_.isEmpty() ? joinPath(cwd_, path_->text().trimmed()) : selected_) : fdArg_->text();
        fdOp(op, selectedProcess(), arg, fd_->value());
    };
    connect(oR, &QPushButton::clicked, this, [=] { fdo("open-r"); });
    connect(oRW, &QPushButton::clicked, this, [=] { fdo("open-rw"); });
    connect(oA, &QPushButton::clicked, this, [=] { fdo("open-append"); });
    connect(fRead, &QPushButton::clicked, this, [=] { fdo("read"); });
    connect(fWrite, &QPushButton::clicked, this, [=] { fdo("write"); });
    connect(fSeek, &QPushButton::clicked, this, [=] { fdo("seek"); });
    connect(fDup, &QPushButton::clicked, this, [=] { fdo("dup"); });
    connect(fClose, &QPushButton::clicked, this, [=] { fdo("close"); });
    connect(nRun, &QPushButton::clicked, this, [this] { startNativeDemo(); });
    connect(native_, &NativeRunner::event, this, [this](const NativeEvent&) { NativeRunner::showEvents(nativeTable_, native_->events()); });
    connect(native_, &NativeRunner::finished, this, [this](int code, bool) {
        emit area_->logMessage(QString("Storage: Live Linux syscall demo finished (exit %1, %2 events)").arg(code).arg(native_->events().size()));
    });
}

bool ExplorerPage::startNativeDemo() {
    nativeTable_->setRowCount(0);
    return native_->start({"fsdemo"});
}
bool ExplorerPage::nativeRunning() const { return native_->running(); }
int ExplorerPage::nativeExitCode() const { return native_->lastExitCode(); }
int ExplorerPage::nativeEventCount() const { return native_->events().size(); }

int ExplorerPage::selectedProcess() const {
    if (proc_->currentIndex() >= 0) return proc_->currentData().toInt();
    return -1;
}

FsResult ExplorerPage::apply(const QString& op, const QString& a, const QString& b) {
    VirtualFs& fs = area_->fs();
    FsResult r;
    std::string A = s(a), B = s(b);
    if (op == "mkdir") r = fs.mkdir(A);
    else if (op == "create") r = fs.create(A, 0644, B);
    else if (op == "write") r = fs.writeFile(A, B);
    else if (op == "append") r = fs.appendFile(A, B);
    else if (op == "read") r = fs.readFile(A);
    else if (op == "delete") r = fs.unlink(A);
    else if (op == "rmdir") r = fs.rmdir(A);
    else if (op == "rename") r = fs.rename(A, B);
    else if (op == "copy") r = fs.copy(A, B);
    else if (op == "link") r = fs.link(A, B);
    else if (op == "symlink") r = fs.symlink(B, A);
    else { r.err = FsErr::Inval; r.message = "unknown operation"; }
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    if (r.ok() && (op == "create" || op == "mkdir" || op == "copy" || op == "rename" || op == "link")) selected_ = op == "rename" || op == "copy" || op == "link" ? b : a;
    if (r.ok() && (op == "delete" || op == "rmdir") && selected_ == a) selected_.clear();
    area_->report(r, op + " " + a + (b.isEmpty() || op == "write" || op == "append" ? QString() : " " + b));
    return r;
}

FsResult ExplorerPage::fdOp(const QString& op, int pid, const QString& arg, int fd) {
    VirtualFs& fs = area_->fs();
    FsResult r;
    if (pid < 0) pid = fs.shellPid(fs.currentUser());
    if (op == "open-r") r = fs.open(pid, s(arg), true, false);
    else if (op == "open-rw") r = fs.open(pid, s(arg), true, true);
    else if (op == "open-append") r = fs.open(pid, s(arg), false, true, false, false, true);
    else if (op == "read") r = fs.read(pid, fd, arg.isEmpty() ? 16 : arg.toInt());
    else if (op == "write") r = fs.write(pid, fd, s(arg));
    else if (op == "seek") r = fs.lseek(pid, fd, arg.toLong());
    else if (op == "dup") r = fs.dup(pid, fd);
    else if (op == "close") r = fs.close(pid, fd);
    else { r.err = FsErr::Inval; r.message = "unknown descriptor operation"; }
    QString msg = q(r.message);
    if (op == "read" && r.ok()) msg += "  data: \"" + q(r.data).left(80) + "\"";
    header_->setStatus(msg, r.ok() ? ui::hitColor().darker(130) : ui::semError());
    if (r.ok() && op.startsWith("open")) fd_->setValue(r.value);
    if (r.ok() && op == "dup") fd_->setValue(r.value);
    area_->report(r, op);
    proc_->setCurrentIndex(proc_->findData(pid));
    return r;
}

void ExplorerPage::setCwd(const QString& dir) {
    FsResult r = area_->fs().resolve(s(dir));
    if (!r.ok() || area_->fs().inode(r.value)->type != FileType::Directory) {
        header_->setStatus(QString("Cannot enter %1: %2").arg(dir, q(r.ok() ? "not a directory" : r.message)), ui::semError());
        return;
    }
    cwd_ = q(area_->fs().pathOf(r.value));
    if (cwd_.isEmpty()) cwd_ = "/";
    refresh();
}

void ExplorerPage::select(const QString& path) {
    selected_ = path;
    path_->setText(path);
    refreshInspector();
}

void ExplorerPage::refresh() {
    updating_ = true;
    const VirtualFs& fs = area_->fs();
    fillUsers(user_, fs);
    int keepPid = proc_->currentIndex() >= 0 ? proc_->currentData().toInt() : -1;
    proc_->clear();
    for (auto& kv : fs.processes()) proc_->addItem(QString("%1 (pid %2, %3)").arg(q(kv.second.name)).arg(kv.first).arg(q(fs.userName(kv.second.uid))), kv.first);
    if (proc_->count() == 0) {
        updating_ = false;
        area_->fs().shellPid(fs.currentUser());
        refresh();
        return;
    }
    int pi = proc_->findData(keepPid);
    proc_->setCurrentIndex(pi >= 0 ? pi : 0);
    if (!fs.resolve(s(cwd_)).ok()) cwd_ = "/";
    refreshTree();
    refreshList();
    refreshInspector();
    refreshDescriptors();
    auto* ev = findChild<QTableWidget*>("fsEvents");
    const auto& e = fs.events();
    int n = std::min<int>((int)e.size(), 400);
    ev->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const FsEvent& x = e[e.size() - 1 - i];
        setRow(ev, i, {QString::number(x.time), q(x.user), q(x.op), x.ok ? "ok" : "denied / failed", q(x.text)});
        ev->item(i, 3)->setForeground(x.ok ? ui::hitColor().darker(120) : ui::semError());
        ev->item(i, 4)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    updating_ = false;
}

void ExplorerPage::refreshTree() {
    const VirtualFs& fs = area_->fs();
    tree_->clear();
    std::function<void(QTreeWidgetItem*, int, const QString&)> add = [&](QTreeWidgetItem* parent, int id, const QString& path) {
        const Inode* d = fs.inode(id);
        for (auto& kv : d->entries) {
            const Inode* c = fs.inode(kv.second);
            if (!c || c->type != FileType::Directory) continue;
            QString p = joinPath(path, q(kv.first));
            auto* it = new QTreeWidgetItem(parent, {q(kv.first)});
            it->setData(0, Qt::UserRole, p);
            if (!fs.may(fs.currentUser(), *c, 'x')) it->setForeground(0, ui::semError());
            add(it, kv.second, p);
            if (p == cwd_) tree_->setCurrentItem(it);
        }
    };
    auto* root = new QTreeWidgetItem(tree_, {"/"});
    root->setData(0, Qt::UserRole, "/");
    add(root, 0, "/");
    tree_->expandAll();
    if (cwd_ == "/") tree_->setCurrentItem(root);
}

void ExplorerPage::refreshList() {
    const VirtualFs& fs = area_->fs();
    QStringList parts = cwd_.split('/', Qt::SkipEmptyParts);
    QString c = "<b>Location:</b> /";
    for (const QString& p : parts) c += " " + p + " /";
    std::string why;
    FsResult d = fs.resolve(s(cwd_));
    const Inode* dir = d.ok() ? fs.inode(d.value) : nullptr;
    bool canList = dir && fs.may(fs.currentUser(), *dir, 'r', &why);
    c += QString(" &nbsp; <i>(as %1: %2)</i>").arg(q(fs.userName(fs.currentUser())), canList ? "listing allowed" : "listing DENIED - " + q(why).toHtmlEscaped());
    crumb_->setText(c);
    list_->setRowCount(0);
    if (!dir || !canList) return;
    list_->setRowCount((int)dir->entries.size());
    int r = 0;
    for (auto& kv : dir->entries) {
        const Inode* n = fs.inode(kv.second);
        QStringList blocks;
        for (int b : n->blocks) blocks << QString::number(b);
        QString type = n->type == FileType::Symlink ? "symlink -> " + q(n->target) : fileTypeName(n->type);
        setRow(list_, r, {q(kv.first), type, QString::number(n->id), QString::number(n->size), q(VirtualFs::modeString(n->mode, n->type)),
                          q(fs.userName(n->uid)), q(fs.groupName(n->gid)), QString::number(n->links), blocks.join(" ")});
        list_->item(r, 0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (joinPath(cwd_, q(kv.first)) == selected_) list_->selectRow(r);
        ++r;
    }
}

void ExplorerPage::refreshInspector() {
    const VirtualFs& fs = area_->fs();
    trace_->clear();
    if (selected_.isEmpty()) { meta_->setText("Select a file or directory in the list."); editor_->clear(); return; }
    std::vector<std::string> trace;
    FsResult r = fs.resolve(s(selected_), false, &trace);
    for (const std::string& t : trace) trace_->addItem(q(t));
    if (!r.ok()) { meta_->setText(q(r.message)); trace_->addItem(q(r.message)); editor_->clear(); return; }
    const Inode* n = fs.inode(r.value);
    QStringList blocks;
    for (int b : n->blocks) blocks << QString::number(b);
    QString m = QString("<b>%1</b><br>inode %2, %3<br>mode %4 (%5)<br>owner %6, group %7<br>link count %8, open references %9<br>size %10 bytes")
                    .arg(selected_.toHtmlEscaped()).arg(n->id).arg(fileTypeName(n->type)).arg(q(VirtualFs::modeString(n->mode, n->type)))
                    .arg(QString::number(n->mode, 8)).arg(q(fs.userName(n->uid)), q(fs.groupName(n->gid))).arg(n->links).arg(n->openRefs).arg(n->size);
    if (n->type == FileType::Regular)
        m += QString("<br>%1 allocation: blocks [%2]%3").arg(allocStrategyName(fs.strategy()), blocks.join(", "))
                 .arg(n->indexBlock >= 0 ? QString(", index block %1").arg(n->indexBlock) : QString());
    if (n->type == FileType::Symlink) {
        FsResult t = fs.resolve(s(selected_));
        m += QString("<br>target: %1 (%2)").arg(q(n->target).toHtmlEscaped(), t.ok() ? "resolves" : q(t.message).toHtmlEscaped());
    }
    if (n->type == FileType::Directory) m += QString("<br>%1 entries (link count = 2 + subdirectories)").arg(n->entries.size());
    m += QString("<br>times (logical clock): change %1, modify %2, access %3").arg(n->ctime).arg(n->mtime).arg(n->atime);
    meta_->setText(m);
    if (n->type == FileType::Regular) {
        std::string why;
        if (fs.may(fs.currentUser(), *n, 'r', &why)) editor_->setPlainText(q(fs.fileContent(n->id)));
        else editor_->setPlainText("(no read permission: " + q(why) + ")");
    } else editor_->clear();
}

void ExplorerPage::refreshDescriptors() {
    const VirtualFs& fs = area_->fs();
    fdTable_->setRowCount(0);
    for (auto& kv : fs.processes())
        for (auto& fd : kv.second.fds) {
            int r = fdTable_->rowCount();
            fdTable_->insertRow(r);
            const OpenFile& o = fs.openFiles()[fd.second];
            setRow(fdTable_, r, {QString("%1 (%2)").arg(q(kv.second.name)).arg(kv.first), q(fs.userName(kv.second.uid)), QString::number(fd.first),
                                 QString::number(fd.second), QString::number(o.inode), QString::number(o.offset)});
        }
    oft_->setRowCount(0);
    for (int i = 0; i < (int)fs.openFiles().size(); ++i) {
        const OpenFile& o = fs.openFiles()[i];
        if (!o.used) continue;
        int r = oft_->rowCount();
        oft_->insertRow(r);
        QString p = q(fs.pathOf(o.inode));
        setRow(oft_, r, {QString::number(i), QString::number(o.inode), p.isEmpty() ? "(no name left: orphan inode)" : p, QString::number(o.offset),
                         QString(o.rd ? "r" : "") + (o.wr ? "w" : "") + (o.append ? " append" : ""), QString::number(o.refs)});
    }
}

// ======================================================================= Access Control

QStringList AccessPage::ops() { return {"read", "write", "execute", "list", "create", "delete"}; }

AccessPage::AccessPage(StorageArea* area) : area_(area) {
    header_ = new ModuleHeader("Access Control",
        "Owner / group / other rwx bits with normal precedence (exactly one class applies), search permission on every path "
        "component, and directory permissions for creating, deleting and renaming entries.");
    user_ = new QComboBox;
    path_ = new QComboBox;
    path_->setEditable(true);
    path_->setToolTip("Virtual path to inspect and change");
    auto* bitsW = new QWidget;
    auto* bg = new QGridLayout(bitsW);
    bg->setContentsMargins(0, 0, 0, 0);
    const char* cls[3] = {"owner", "group", "other"};
    const char* rwx[3] = {"r", "w", "x"};
    for (int c = 0; c < 3; ++c) {
        bg->addWidget(new QLabel(cls[c]), c + 1, 0);
        for (int k = 0; k < 3; ++k) {
            if (c == 0) bg->addWidget(new QLabel(rwx[k]), 0, k + 1);
            bits_[c * 3 + k] = new QCheckBox;
            bits_[c * 3 + k]->setToolTip(QString("%1 %2").arg(cls[c], rwx[k]));
            bg->addWidget(bits_[c * 3 + k], c + 1, k + 1);
        }
    }
    modeText_ = new QLineEdit;
    modeText_->setPlaceholderText("750, rwxr-x--- or u+x,g-w");
    modeText_->setToolTip("Octal, 9-character symbolic or chmod-style clauses");
    auto* bChmod = button("chmod", "Only the owner or root may change the mode");
    owner_ = new QComboBox;
    group_ = new QComboBox;
    auto* bChown = button("chown", "Only root may change the owner (Linux rule)");
    auto* bChgrp = button("chgrp", "The owner may choose one of their own groups; root any group");
    preset_ = new QComboBox;
    preset_->addItems({"Private file (600)", "Shared-group file (660, group staff)", "Public readable file (644)", "Private directory (700)",
                       "Group directory (770, group staff)", "Traverse-only directory (711)", "List-only directory (744)"});
    auto* bPreset = button("Apply preset");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Current user:", user_);
    cf->addRow("Path:", path_);
    cf->addRow(new QLabel("<b>Permission bits</b> (tick to change, then chmod)"));
    cf->addRow(bitsW);
    cf->addRow("Mode text:", modeText_);
    cf->addRow(bChmod);
    cf->addRow("Owner:", owner_);
    cf->addRow(bChown);
    cf->addRow("Group:", group_);
    cf->addRow(bChgrp);
    cf->addRow("Preset:", preset_);
    cf->addRow(bPreset);

    matrix_ = table(QStringList{"User", "Class"} + ops());
    matrix_->setSelectionBehavior(QAbstractItemView::SelectItems);
    explain_ = new QLabel("Click a cell for the explanation.");
    explain_->setWordWrap(true);
    auto* centerW = new QWidget;
    auto* ml = new QVBoxLayout(centerW);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->addWidget(new QLabel("<b>Effective access matrix</b> for the selected path (every user, every operation)"));
    ml->addWidget(matrix_, 2);
    ml->addWidget(explain_);

    rwx_ = new QLabel;
    rwx_->setTextFormat(Qt::RichText);
    octal_ = new QLabel;
    octal_->setWordWrap(true);
    trace_ = new QListWidget;
    trace_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>rwx matrix</b>"));
    il->addWidget(rwx_);
    il->addWidget(octal_);
    il->addWidget(new QLabel("<b>Path resolution as the current user</b>"));
    il->addWidget(trace_, 1);

    auto* bottom = new QTabWidget;
    auto* rules = new QPlainTextEdit;
    rules->setReadOnly(true);
    rules->setPlainText(
        "Rules of this model\n"
        "- Exactly one class applies: the owner bits if you own the file, else the group bits if you are in its group, else the other bits. "
        "The classes are not OR-ed (an owner with --- is denied even if 'other' has rwx).\n"
        "- Every directory on the path needs x (search). Listing a directory needs r; creating, deleting or renaming an entry needs w and x "
        "on the PARENT directory - the file's own bits do not matter for deletion.\n"
        "- chmod: owner or root. chown: root only. chgrp: owner to one of their own groups, or root.\n"
        "- uid 0 (root) is a simplified privileged user: it bypasses read/write checks and directory search; executing a regular file "
        "still needs one x bit.\n"
        "- Not modelled: ACLs, sticky bit, setuid/setgid, umask, capabilities. This is NOT a POSIX-compliant filesystem.");
    bottom->addTab(rules, "Rules and exclusions");

    assembleModule(this, header_, cfgW, centerW, titledBox("Inspector", insW), nullptr, bottom);

    connect(area_, &StorageArea::changed, this, &AccessPage::refresh);
    connect(user_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_ || i < 0) return;
        area_->fs().setCurrentUser(user_->itemData(i).toInt());
        area_->notifyChanged();
    });
    connect(path_, &QComboBox::currentTextChanged, this, [this] { if (!updating_) refresh(); });
    for (QCheckBox* c : bits_) connect(c, &QCheckBox::toggled, this, [this] {
        if (updating_) return;
        int m = 0;
        for (int i = 0; i < 9; ++i) if (bits_[i]->isChecked()) m |= 1 << (8 - i);
        modeText_->setText(QString::number(m, 8).rightJustified(3, '0'));
    });
    connect(bChmod, &QPushButton::clicked, this, [this] { applyMode(modeText_->text()); });
    connect(bChown, &QPushButton::clicked, this, [this] { setOwner(owner_->currentData().toInt()); });
    connect(bChgrp, &QPushButton::clicked, this, [this] { setGroup(group_->currentData().toInt()); });
    connect(bPreset, &QPushButton::clicked, this, [this] { applyPreset(preset_->currentIndex()); });
    connect(matrix_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (c < 2 || r >= (int)area_->fs().users().size()) return;
        const VUser& u = area_->fs().users()[r];
        FsResult res = area_->fs().checkAccess(u.uid, s(path()), s(ops()[c - 2]));
        explain_->setText(QString("<b>%1, %2 %3:</b> %4").arg(q(u.name), ops()[c - 2], path().toHtmlEscaped(), q(res.message).toHtmlEscaped()));
    });
    path_->setCurrentText("/shared/plan.txt");
}

QString AccessPage::path() const { return path_->currentText().trimmed(); }
void AccessPage::setPath(const QString& p) { path_->setCurrentText(p); refresh(); }

bool AccessPage::applyMode(const QString& text) {
    VirtualFs& fs = area_->fs();
    FsResult r = fs.resolve(s(path()));
    int m = 0;
    if (!r.ok()) { header_->setStatus(q(r.message), ui::semError()); return false; }
    if (!VirtualFs::parseMode(s(text), fs.inode(r.value)->mode, m)) {
        ui::warn(this, "Mode '" + text + "' is not valid: use 3 octal digits (750), 9 characters (rwxr-x---) or clauses (u+x,g-w,o=r)");
        return false;
    }
    FsResult c = fs.chmod(s(path()), m);
    header_->setStatus(q(c.message), c.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(c, "chmod " + path());
    return c.ok();
}

bool AccessPage::applyPreset(int i) {
    static const int modes[] = {0600, 0660, 0644, 0700, 0770, 0711, 0744};
    if (i < 0 || i > 6) return false;
    bool ok = applyMode(QString::number(modes[i], 8));
    if (ok && (i == 1 || i == 4)) ok = setGroup(101);
    return ok;
}

bool AccessPage::setOwner(int uid) {
    FsResult r = area_->fs().chown(s(path()), uid);
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(r, "chown " + path());
    return r.ok();
}

bool AccessPage::setGroup(int gid) {
    FsResult r = area_->fs().chgrp(s(path()), gid);
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(r, "chgrp " + path());
    return r.ok();
}

bool AccessPage::matrixAllowed(int userRow, int opColumn) const {
    if (userRow < 0 || userRow >= (int)allowed_.size() || opColumn < 0 || opColumn >= (int)allowed_[userRow].size()) return false;
    return allowed_[userRow][opColumn];
}

void AccessPage::refresh() {
    updating_ = true;
    const VirtualFs& fs = area_->fs();
    fillUsers(user_, fs);
    QString keep = path();
    path_->clear();
    path_->addItems(allPaths(fs));
    path_->setCurrentText(keep.isEmpty() ? "/" : keep);
    owner_->clear();
    for (const VUser& u : fs.users()) owner_->addItem(q(u.name), u.uid);
    group_->clear();
    for (const VGroup& g : fs.groups()) group_->addItem(q(g.name), g.gid);
    trace_->clear();
    std::vector<std::string> tr;
    FsResult r = fs.resolve(s(path()), true, &tr);
    for (const std::string& t : tr) trace_->addItem(q(t));
    if (!r.ok()) trace_->addItem(q(r.message));
    FsResult any = fs.resolveAs(0, s(path()));      // the file exists (for root)?
    const Inode* n = any.ok() ? fs.inode(any.value) : nullptr;
    if (n) {
        for (int i = 0; i < 9; ++i) bits_[i]->setChecked(n->mode & (1 << (8 - i)));
        modeText_->setText(QString::number(n->mode, 8).rightJustified(3, '0'));
        owner_->setCurrentIndex(owner_->findData(n->uid));
        group_->setCurrentIndex(group_->findData(n->gid));
        QString h = "<table cellspacing=2><tr><td></td><td><b>r</b></td><td><b>w</b></td><td><b>x</b></td><td></td></tr>";
        const char* cls[3] = {"owner", "group", "other"};
        QString who[3] = {q(fs.userName(n->uid)), q(fs.groupName(n->gid)), "everyone else"};
        QString sum;
        for (int c = 0; c < 3; ++c) {
            int b = (n->mode >> (6 - 3 * c)) & 7;
            h += QString("<tr><td>%1</td>").arg(cls[c]);
            for (int k = 0; k < 3; ++k) {
                bool on = b & (4 >> k);
                h += QString("<td style='background:%1;color:#1f2328'>&nbsp;%2&nbsp;</td>").arg(on ? "#cfe8c9" : "#f8c9ca").arg(on ? "rwx"[k] : '-');
            }
            h += QString("<td>%1</td></tr>").arg(who[c].toHtmlEscaped());
            sum += QString("%1%2").arg(c ? " | " : "").arg(QString("%1+%2+%3 = %4").arg(b & 4).arg(b & 2).arg(b & 1).arg(b));
        }
        rwx_->setText(h + "</table>");
        octal_->setText(QString("%1 = %2 = octal %3   (%4)").arg(q(VirtualFs::modeString(n->mode, n->type))).arg(sum)
                            .arg(QString::number(n->mode, 8).rightJustified(3, '0')).arg(fileTypeName(n->type)));
    } else {
        rwx_->setText("(no such path)");
        octal_->clear();
    }
    refreshMatrix();
    updating_ = false;
}

void AccessPage::refreshMatrix() {
    const VirtualFs& fs = area_->fs();
    const auto& users = fs.users();
    matrix_->setRowCount((int)users.size());
    allowed_.assign(users.size(), std::vector<bool>(ops().size(), false));
    FsResult any = fs.resolveAs(0, s(path()));
    const Inode* n = any.ok() ? fs.inode(any.value) : nullptr;
    for (int r = 0; r < (int)users.size(); ++r) {
        const VUser& u = users[r];
        QString cls = !n ? "-" : u.uid == 0 ? "root (bypass)" : q(fs.accessClass(u.uid, *n));
        matrix_->setItem(r, 0, ui::roItem(q(u.name) + (u.uid == fs.currentUser() ? " (current)" : "")));
        matrix_->setItem(r, 1, ui::roItem(cls));
        for (int c = 0; c < ops().size(); ++c) {
            FsResult res = fs.checkAccess(u.uid, s(path()), s(ops()[c]));
            bool na = res.err == FsErr::NotDir || res.err == FsErr::NoEnt || res.err == FsErr::Inval;
            allowed_[r][c] = res.ok();
            auto* it = ui::roItem(na ? "n/a" : res.ok() ? "allowed" : "denied");
            it->setToolTip(q(res.message));
            if (!na) ui::shade(it, res.ok() ? ui::lightGreen() : ui::lightRed());
            matrix_->setItem(r, c + 2, it);
        }
    }
}

// ======================================================================= Block Allocation

BlockPage::BlockPage(StorageArea* area) : area_(area) {
    header_ = new ModuleHeader("Block Allocation",
        "How files are mapped to disk blocks: contiguous (start + length), linked (next pointers inside blocks) and indexed "
        "(one index block of pointers). Allocation is about WHERE blocks go - head scheduling is in the Disk tab.");
    auto* disk = new DiskControls(area);
    name_ = new QLineEdit("data.bin");
    name_->setToolTip("File in the root directory of the shared virtual disk");
    bytes_ = spin(1, 100000, 200, "Bytes to allocate (create) or to add (extend)");
    auto* bAlloc = button("Create / extend", "Creates the file, or appends this many bytes if it exists (rolled back completely on failure)");
    auto* bFree = button("Delete (free blocks)");
    preset_ = new QComboBox;
    preset_->addItem("(custom workload)");
    for (const AllocPreset& p : allocPresets()) preset_->addItem(q(p.name));
    presetText_ = new QLabel;
    presetText_->setWordWrap(true);
    workload_ = new QLineEdit;
    workload_->setToolTip("Comma separated: create <name> <bytes>, extend <name> <bytes>, delete <name>");
    cmpBlocks_ = spin(16, 512, 32);
    cmpSize_ = spin(32, 1024, 64);
    cmpSize_->setSingleStep(32);
    auto* bCmp = button("Compare the three strategies", "Runs the same workload on three isolated cloned disks");
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow(titledBox("Shared virtual disk", disk));
    cf->addRow("File:", name_);
    cf->addRow("Bytes:", bytes_);
    cf->addRow(buttonGrid({bAlloc, bFree}));
    cf->addRow(new QLabel("<b>Comparison (isolated clones)</b>"));
    cf->addRow("Preset:", preset_);
    cf->addRow(presetText_);
    cf->addRow("Workload:", workload_);
    cf->addRow("Blocks:", cmpBlocks_);
    cf->addRow("Block size:", cmpSize_);
    cf->addRow(bCmp);

    grid_ = new DiskGridView;
    grid_->setFs(&area_->fs());

    file_ = new QComboBox;
    logical_ = spin(0, 4096, 0, "Logical block number to read");
    map_ = table({"Logical", "Physical", "Kind", "Bytes used"});
    traverse_ = new QListWidget;
    traverse_->setWordWrap(true);
    stats_ = new QLabel;
    stats_->setWordWrap(true);
    auto* insW = new QWidget;
    auto* il = new QFormLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addRow("File:", file_);
    il->addRow(new QLabel("<b>Logical -> physical mapping</b>"));
    il->addRow(map_);
    il->addRow("Read logical block:", logical_);
    il->addRow(traverse_);
    il->addRow(stats_);

    auto* bottom = new QTabWidget;
    cmp_ = table({"Strategy", "Failures", "Data blocks", "Index blocks", "Pointer bytes", "Free", "Largest free run", "Ext. frag indicator", "Max file size"});
    auto* gw = new QWidget;
    auto* gl = new QHBoxLayout(gw);
    gl->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) { cmpGrid_[i] = new DiskGridView; gl->addWidget(cmpGrid_[i]); }
    auto* cw = new QSplitter(Qt::Vertical);
    cw->addWidget(cmp_);
    cw->addWidget(gw);
    bottom->addTab(cw, "Strategy comparison");
    cmpLog_ = new QPlainTextEdit;
    cmpLog_->setReadOnly(true);
    bottom->addTab(cmpLog_, "Comparison log");

    assembleModule(this, header_, cfgW, titledBox("Virtual disk blocks (click a block to select its file)", grid_), titledBox("Inspector", insW), nullptr, bottom);

    connect(area_, &StorageArea::changed, this, &BlockPage::refresh);
    connect(bAlloc, &QPushButton::clicked, this, [this] { allocate(name_->text().trimmed(), bytes_->value()); });
    connect(bFree, &QPushButton::clicked, this, [this] { release(name_->text().trimmed()); });
    connect(bCmp, &QPushButton::clicked, this, [this] { compare(-1); });
    connect(preset_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i <= 0) { presetText_->clear(); return; }
        const AllocPreset p = allocPresets()[i - 1];
        presetText_->setText(q(p.description));
        workload_->setText(q(p.workload));
        cmpBlocks_->setValue(p.blockCount);
        cmpSize_->setValue(p.blockSize);
    });
    connect(file_, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) refreshInspector(); });
    connect(logical_, &QSpinBox::valueChanged, this, [this] { if (!updating_) refreshInspector(); });
    connect(grid_, &DiskGridView::blockClicked, this, [this](int b) {
        const Block& bl = area_->fs().blocks()[b];
        if (bl.owner >= 0) selectFile(bl.owner);
    });
    preset_->setCurrentIndex(1);
    compare(-1);
}

FsResult BlockPage::allocate(const QString& name, int bytes) {
    VirtualFs& fs = area_->fs();
    QString p = joinPath("/", name);
    std::string fill(bytes, 'a' + (int)(fs.clock() % 26));
    FsResult r = fs.resolve(s(p)).ok() ? fs.appendFile(s(p), fill) : fs.create(s(p), 0644, fill);
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(r, "allocate " + p);
    FsResult id = fs.resolve(s(p));
    if (id.ok()) selectFile(id.value);
    return r;
}

FsResult BlockPage::release(const QString& name) {
    FsResult r = area_->fs().unlink(s(joinPath("/", name)));
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(r, "free " + name);
    return r;
}

bool BlockPage::compare(int preset) {
    if (preset >= 0) preset_->setCurrentIndex(preset + 1);
    try {
        rows_ = compareAllocation(parseAllocWorkload(s(workload_->text())), cmpBlocks_->value(), cmpSize_->value() / 8 * 8);
    } catch (const std::exception& e) {
        ui::warn(this, QString("Workload / geometry error: %1").arg(e.what()));
        return false;
    }
    cmp_->setRowCount((int)rows_.size());
    cmpLog_->clear();
    for (int i = 0; i < (int)rows_.size(); ++i) {
        const AllocCompareRow& r = rows_[i];
        const DiskStats& st = r.stats;
        setRow(cmp_, i, {allocStrategyName(r.strategy), QString::number(r.failures), QString::number(st.data), QString::number(st.index),
                         QString::number(r.pointerBytes), QString::number(st.free), QString::number(st.largestFreeRun),
                         st.free == 0 ? "n/a (no free space)" : QString::number(st.externalFrag(), 'f', 2), QString("%1 B").arg(r.fs.maxFileBytes())});
        ui::shade(cmp_->item(i, 1), r.failures ? ui::lightRed() : ui::lightGreen());
        cmpGrid_[i]->setFs(&rows_[i].fs);
        cmpGrid_[i]->setTitle(QString("%1: %2 failure(s)").arg(allocStrategyName(r.strategy)).arg(r.failures));
        cmpLog_->appendPlainText(QString("== %1 ==").arg(allocStrategyName(r.strategy)));
        for (const std::string& l : r.log) cmpLog_->appendPlainText("  " + q(l));
    }
    cmp_->setToolTip("Ext. frag indicator = 1 - largest free run / free blocks (a chosen indicator; n/a when nothing is free). "
                     "Pointer bytes: 4 per data block for linked (inside the block) and indexed (inside the index block).");
    emit area_->logMessage(QString("Storage: block allocation comparison - contiguous %1, linked %2, indexed %3 failure(s)")
                               .arg(rows_[0].failures).arg(rows_[1].failures).arg(rows_[2].failures));
    return true;
}

void BlockPage::selectFile(int inode) {
    int i = file_->findData(inode);
    if (i >= 0) file_->setCurrentIndex(i);
    refreshInspector();
}

int BlockPage::selectedFile() const { return file_->currentIndex() >= 0 ? file_->currentData().toInt() : -1; }

void BlockPage::refresh() {
    updating_ = true;
    const VirtualFs& fs = area_->fs();
    grid_->refresh();               // same model object (owned by the area): flash the blocks that changed
    int keep = selectedFile();
    file_->clear();
    for (const Inode& n : fs.inodes())
        if (n.used && n.type == FileType::Regular) file_->addItem(QString("%1 (inode %2, %3 B)").arg(q(fs.pathOf(n.id))).arg(n.id).arg(n.size), n.id);
    int i = file_->findData(keep);
    if (i >= 0) file_->setCurrentIndex(i);
    updating_ = false;
    refreshInspector();
}

void BlockPage::refreshInspector() {
    const VirtualFs& fs = area_->fs();
    int id = selectedFile();
    grid_->setHighlight(id);
    grid_->setTitle(QString("%1 allocation, %2 blocks x %3 B").arg(allocStrategyName(fs.strategy())).arg(fs.blockCount()).arg(fs.blockSize()));
    const Inode* n = fs.inode(id);
    map_->setRowCount(0);
    traverse_->clear();
    if (n) {
        int payload = fs.payloadPerBlock();
        if (n->indexBlock >= 0) {
            map_->insertRow(0);
            setRow(map_, 0, {"index", QString::number(n->indexBlock), "index", QString("%1 pointers x 4 B").arg(n->blocks.size())});
        }
        for (int k = 0; k < (int)n->blocks.size(); ++k) {
            int r = map_->rowCount();
            map_->insertRow(r);
            int used = std::min(payload, n->size - k * payload);
            setRow(map_, r, {QString::number(k), QString::number(n->blocks[k]), "data",
                             QString("%1 of %2%3").arg(used).arg(payload).arg(fs.strategy() == AllocStrategy::Linked ? " (+4 B next pointer)" : "")});
        }
        logical_->setMaximum(std::max(0, (int)n->blocks.size() - 1));
        for (const AccessStep& st : fs.accessPath(id, logical_->value())) traverse_->addItem(q(st.what));
        traverse_->addItem(QString("= %1 disk read(s) for logical block %2").arg(fs.accessPath(id, logical_->value()).size()).arg(logical_->value()));
    }
    DiskStats st = fs.stats();
    QString bitmap;
    for (const Block& b : fs.blocks()) bitmap += b.kind == BlockKind::Free ? '0' : '1';
    stats_->setText(QString("<b>Disk</b>: %1 free, %2 data, %3 index, %4 metadata blocks. Free runs %5, largest %6. "
                            "External fragmentation indicator (1 - largest run / free): %7. Payload %8 B in %9 B of data blocks. "
                            "Largest representable file: %10 B.<br><b>Free-space bitmap</b> (1 = used): <tt>%11</tt>")
                        .arg(st.free).arg(st.data).arg(st.index).arg(st.meta).arg(st.freeRuns).arg(st.largestFreeRun)
                        .arg(st.free ? QString::number(st.externalFrag(), 'f', 2) : QString("n/a (no free blocks)"))
                        .arg(st.payloadBytes).arg(st.capacityBytes).arg(fs.maxFileBytes()).arg(bitmap));
}

// ======================================================================= Recovery Lab

RecoveryPage::RecoveryPage(StorageArea* area) : area_(area) {
    header_ = new ModuleHeader("Recovery Lab",
        "Deletion frees blocks but (in this teaching model) keeps their bytes until they are reused. Recovery uses deleted "
        "metadata records or signature carving on the shared virtual disk only - never a real disk.");
    preset_ = new QComboBox;
    for (const RecoveryPreset& p : recoveryPresets()) preset_->addItem(q(p.name));
    auto* bPreset = button("Load preset", "Replaces the shared virtual disk with a deterministic fixture");
    delPath_ = new QLineEdit("/home/alice/todo.txt");
    auto* bDel = button("Delete file (unlink)");
    fill_ = spin(1, 100000, 64, "Size of the new file that reuses free blocks");
    auto* bFill = button("Overwrite: create a new file", "A new file takes the lowest free blocks, destroying old contents there");
    auto* bLose = button("Lose metadata of the record", "Simulates a lost journal / reused inode record: only carving can help then");
    auto* bCarve = button("Carve free blocks", "Scans free blocks for the synthetic OSF1 signature");
    restorePath_ = new QLineEdit("/recovered/restored.txt");
    auto* bRestore = button("Restore into a new file", "Never overwrites an existing file; blocks that still hold evidence are not used");
    auto* note = new QLabel("Teaching model: real filesystems may zero, trim or relocate blocks (SSDs!), so recovery is usually less likely "
                            "than here. A matching signature alone never proves integrity.");
    note->setWordWrap(true);
    auto* cfgW = new QWidget;
    auto* cf = new QFormLayout(cfgW);
    cf->addRow("Preset:", preset_);
    cf->addRow(bPreset);
    cf->addRow("Delete:", delPath_);
    cf->addRow(bDel);
    cf->addRow("New file bytes:", fill_);
    cf->addRow(bFill);
    cf->addRow(bLose);
    cf->addRow(bCarve);
    cf->addRow("Restore as:", restorePath_);
    cf->addRow(bRestore);
    cf->addRow(note);

    grid_ = new DiskGridView;
    grid_->setFs(&area_->fs());

    cands_ = table({"#", "Source", "Name (metadata)", "Size", "Classification", "Recoverable bytes"});
    evidence_ = table({"Logical", "Block", "Gen at deletion", "Gen now", "State"});
    cls_ = new QLabel;
    cls_->setWordWrap(true);
    explain_ = new QLabel;
    explain_->setWordWrap(true);
    preview_ = new QPlainTextEdit;
    preview_->setReadOnly(true);
    preview_->setPlaceholderText("Recovered bytes (overwritten regions shown as '?')");
    auto* insW = new QWidget;
    auto* il = new QVBoxLayout(insW);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("<b>Deleted-file candidates</b>"));
    il->addWidget(cands_, 2);
    il->addWidget(cls_);
    il->addWidget(explain_);
    il->addWidget(new QLabel("<b>Block evidence (before / after)</b>"));
    il->addWidget(evidence_, 2);
    il->addWidget(new QLabel("<b>Candidate preview</b>"));
    il->addWidget(preview_, 1);

    auto* bottom = new QTabWidget;
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    bottom->addTab(log_, "Recovery log");

    assembleModule(this, header_, cfgW, titledBox("Virtual disk (green frame = intact evidence, red cross = overwritten)", grid_),
                   titledBox("Inspector", insW), nullptr, bottom);

    connect(area_, &StorageArea::changed, this, &RecoveryPage::refresh);
    connect(bPreset, &QPushButton::clicked, this, [this] { loadPreset(preset_->currentIndex()); });
    connect(bDel, &QPushButton::clicked, this, [this] { deleteFile(delPath_->text().trimmed()); });
    connect(bFill, &QPushButton::clicked, this, [this] { overwrite(fill_->value()); });
    connect(bLose, &QPushButton::clicked, this, [this] { loseMetadata(); });
    connect(bCarve, &QPushButton::clicked, this, [this] { carve(); });
    connect(bRestore, &QPushButton::clicked, this, [this] { restore(restorePath_->text().trimmed()); });
    connect(cands_, &QTableWidget::cellClicked, this, [this](int r, int) { selectCandidate(r); });
}

bool RecoveryPage::loadPreset(int i) {
    if (i < 0 || i >= (int)recoveryPresets().size()) return false;
    VirtualFs fs;
    loadRecoveryPreset(fs, i);
    carved_.clear();
    selRow_ = -1;
    area_->replaceFs(fs, "recovery preset: " + q(recoveryPresets()[i].name));
    log_->appendPlainText(QString("Preset '%1': %2").arg(q(recoveryPresets()[i].name), q(recoveryPresets()[i].description)));
    header_->setStatus("Preset: " + q(recoveryPresets()[i].description), ui::semReady());
    if (i == 3) carve();
    selectCandidate(0);
    return true;
}

bool RecoveryPage::deleteFile(const QString& path) {
    FsResult r = area_->fs().unlink(s(path));
    log_->appendPlainText(q(r.message));
    area_->report(r, "delete " + path);
    if (r.ok()) selectCandidate(candidateCount() - 1 - (int)carved_.size());
    return r.ok();
}

bool RecoveryPage::overwrite(int bytes) {
    VirtualFs& fs = area_->fs();
    QString p;
    for (int k = 1; k < 1000; ++k) { p = QString("/tmp/overwrite%1.bin").arg(k); if (!fs.resolveAs(0, s(p)).ok()) break; }
    int old = fs.currentUser();
    fs.setCurrentUser(0);
    FsResult r = fs.create(s(p), 0644, std::string(bytes, 'W'));
    fs.setCurrentUser(old);
    log_->appendPlainText(q(r.message));
    area_->report(r, "overwrite " + p);
    if (selRow_ >= 0) selectCandidate(selRow_);
    return r.ok();
}

bool RecoveryPage::carve() {
    carved_ = area_->fs().carve();
    log_->appendPlainText(QString("Carving: %1 signature candidate(s) found in free blocks").arg(carved_.size()));
    refresh();
    return true;
}

bool RecoveryPage::loseMetadata() {
    const auto& d = area_->fs().deleted();
    int rec = selRow_ >= 0 && selRow_ < (int)d.size() ? selRow_ : (int)d.size() - 1;
    if (rec < 0) { header_->setStatus("No deleted record to lose", ui::semError()); return false; }
    area_->fs().loseMetadata(rec);
    log_->appendPlainText(QString("Metadata of record #%1 lost: name, size and block list are no longer known").arg(rec));
    area_->notifyChanged();
    selectCandidate(rec);
    return true;
}

int RecoveryPage::candidateCount() const { return (int)area_->fs().deleted().size() + (int)carved_.size(); }

void RecoveryPage::selectCandidate(int row) {
    int nd = (int)area_->fs().deleted().size();
    if (row < 0 || row >= candidateCount()) { selRow_ = -1; report_ = RecoveryReport(); showReport(); return; }
    selRow_ = row;
    report_ = row < nd ? area_->fs().analyse(row) : carved_[row - nd].report;
    if (row < nd) {
        const DeletedRecord& d = area_->fs().deleted()[row];
        QString base = q(d.path).section('/', -1);
        restorePath_->setText("/recovered/" + (d.metadataAvailable ? base : QString("record%1").arg(row)) + ".rec");
    } else restorePath_->setText(QString("/recovered/carved%1.osf").arg(row - nd));
    cands_->selectRow(row);
    showReport();
}

void RecoveryPage::showReport() {
    const VirtualFs& fs = area_->fs();
    std::map<int, bool> ev;
    evidence_->setRowCount(0);
    int nd = (int)fs.deleted().size();
    for (const BlockEvidence& e : report_.blocks) {
        ev[e.block] = e.intact;
        int r = evidence_->rowCount();
        evidence_->insertRow(r);
        QString genThen = "-";
        if (selRow_ >= 0 && selRow_ < nd && e.logical < (int)fs.deleted()[selRow_].gens.size()) genThen = QString::number(fs.deleted()[selRow_].gens[e.logical]);
        setRow(evidence_, r, {QString::number(e.logical), QString::number(e.block), genThen, QString::number(fs.blocks()[e.block].gen),
                              (e.intact ? "intact: " : "overwritten: ") + q(e.reason)});
        ui::shade(evidence_->item(r, 4), e.intact ? ui::lightGreen() : ui::lightRed());
    }
    grid_->setEvidence(ev);
    if (selRow_ < 0) {
        cls_->setText(candidateCount() == 0 ? "No deleted files on this disk yet: delete a file, or load a preset."
                                            : "Select a candidate in the table.");
        explain_->clear();
        preview_->clear();
        return;
    }
    QColor c = report_.cls == RecoveryClass::Full ? ui::hitColor() : report_.cls == RecoveryClass::Unrecoverable ? ui::faultColor() : ui::semWaiting();
    cls_->setText(QString("<b style='color:%1'>%2</b> (%3) - %4 of %5 bytes recoverable%6").arg(c.name(), recoveryClassName(report_.cls), q(report_.source))
                      .arg(report_.recoverableBytes).arg(report_.totalBytes).arg(report_.verified ? ", verified against the original checksum" : ", not verified"));
    explain_->setText(q(report_.explanation));
    QString pv;
    for (char ch : report_.preview) pv += (ch >= 32 && ch < 127) || ch == '\n' ? QChar(ch) : QChar('.');
    preview_->setPlainText(pv);
}

FsResult RecoveryPage::restore(const QString& newPath) {
    if (selRow_ < 0) { FsResult r; r.err = FsErr::Inval; r.message = "select a candidate first"; header_->setStatus(q(r.message), ui::semError()); return r; }
    RecoveryReport keep = report_;
    FsResult r = area_->fs().restore(keep, s(newPath));
    log_->appendPlainText(q(r.message));
    if (!r.ok() && r.err == FsErr::NoSpc) log_->appendPlainText("Preview only: the recovered bytes are shown in the inspector; the image was not changed.");
    header_->setStatus(q(r.message), r.ok() ? ui::hitColor().darker(130) : ui::semError());
    area_->report(r, "restore " + newPath);
    selectCandidate(selRow_);
    return r;
}

void RecoveryPage::refresh() {
    updating_ = true;
    const VirtualFs& fs = area_->fs();
    grid_->refresh();
    const auto& d = fs.deleted();
    cands_->setRowCount((int)(d.size() + carved_.size()));
    for (int i = 0; i < (int)d.size(); ++i) {
        RecoveryReport r = fs.analyse(i);
        setRow(cands_, i, {QString::number(i), "deleted record", d[i].metadataAvailable ? q(d[i].path) : "(metadata lost)",
                           d[i].metadataAvailable ? QString::number(d[i].size) : "?", recoveryClassName(r.cls) + QString(d[i].restored ? " (restored)" : ""),
                           QString::number(r.recoverableBytes)});
    }
    for (int k = 0; k < (int)carved_.size(); ++k) {
        const CarveCandidate& c = carved_[k];
        int row = (int)d.size() + k;
        setRow(cands_, row, {QString::number(row), QString("carved at block %1").arg(c.startBlock), "(unknown: no metadata)",
                             QString::number(c.report.totalBytes), recoveryClassName(c.report.cls), QString::number(c.report.recoverableBytes)});
    }
    updating_ = false;
    if (selRow_ >= candidateCount()) selRow_ = -1;
    if (selRow_ >= 0) {
        int nd = (int)d.size();
        report_ = selRow_ < nd ? fs.analyse(selRow_) : carved_[selRow_ - nd].report;
    }
    showReport();
}
