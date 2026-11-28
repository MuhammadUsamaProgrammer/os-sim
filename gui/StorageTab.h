// StorageTab.h - Phase 3 "Storage & Files" parent tab: File Explorer, Access Control, Block
// Allocation and Recovery Lab. All four pages show ONE authoritative VirtualFs (core/vfs.h) owned
// by StorageArea; every operation goes through the model and every page refreshes from it
// (changed() signal), so the views can never disagree. Simulation only - the host filesystem is
// never written, except by the optional Live Linux syscall demo in its own temporary directory.
#pragma once
#include <QTabWidget>
#include <QWidget>
#include <vector>
#include "vfs.h"

class QComboBox; class QSpinBox; class QLineEdit; class QTreeWidget; class QTableWidget; class QPlainTextEdit;
class QLabel; class QListWidget; class QCheckBox; class QGroupBox; class ModuleHeader; class DiskGridView; class NativeRunner;
class StorageArea;

// One "virtual disk" control box (geometry, strategy, new / sample / save / load image).
class DiskControls : public QWidget {
    Q_OBJECT
public:
    explicit DiskControls(StorageArea* area, QWidget* parent = nullptr);
    void sync();
private:
    StorageArea* area_;
    QSpinBox *blocks_, *size_;
    QComboBox* strategy_;
    QLabel* summary_;
};

class ExplorerPage : public QWidget {
    Q_OBJECT
public:
    explicit ExplorerPage(StorageArea* area);
    void refresh();
    void setCwd(const QString& dir);
    QString cwd() const { return cwd_; }
    void select(const QString& path);
    // Fills the path / destination fields (used by samples; nothing is executed).
    void prefill(const QString& path, const QString& dest = QString());
    // op: mkdir create write append read delete rmdir rename copy link symlink (b = destination / target / text)
    FsResult apply(const QString& op, const QString& a, const QString& b = QString());
    // op: open-r open-rw open-append read write seek dup close
    FsResult fdOp(const QString& op, int pid, const QString& arg = QString(), int fd = -1);
    int selectedProcess() const;
    bool startNativeDemo();
    bool nativeRunning() const;
    int nativeExitCode() const;
    int nativeEventCount() const;
private:
    void refreshTree();
    void refreshList();
    void refreshInspector();
    void refreshDescriptors();
    StorageArea* area_;
    ModuleHeader* header_;
    QComboBox *user_, *proc_;
    QLineEdit *path_, *dest_, *fdArg_;
    QSpinBox* fd_;
    QTreeWidget* tree_;
    QTableWidget *list_, *fdTable_, *oft_, *nativeTable_;
    QLabel *crumb_, *meta_;
    QPlainTextEdit* editor_;
    QListWidget* trace_;
    NativeRunner* native_;
    QString cwd_ = "/", selected_;
    bool updating_ = false;
};

class AccessPage : public QWidget {
    Q_OBJECT
public:
    explicit AccessPage(StorageArea* area);
    void refresh();
    void setPath(const QString& p);
    QString path() const;
    bool applyMode(const QString& text);        // octal, rwx string or u+x,g-w clauses (chmod as current user)
    bool applyPreset(int i);
    bool setOwner(int uid);
    bool setGroup(int gid);
    // matrix cell: is op allowed for the user (row order = VirtualFs::users())
    bool matrixAllowed(int userRow, int opColumn) const;
    static QStringList ops();
    static QStringList presetNames();               // permission presets (index = applyPreset)
private:
    void refreshMatrix();
    StorageArea* area_;
    ModuleHeader* header_;
    QComboBox *user_, *path_, *owner_, *group_, *preset_;
    QCheckBox* bits_[9];
    QLineEdit* modeText_;
    QTableWidget* matrix_;
    QLabel *rwx_, *octal_, *explain_;
    QListWidget* trace_;
    bool updating_ = false;
    std::vector<std::vector<bool>> allowed_;
};

class BlockPage : public QWidget {
    Q_OBJECT
public:
    explicit BlockPage(StorageArea* area);
    void refresh();
    FsResult allocate(const QString& name, int bytes);   // create or extend /name on the shared disk
    FsResult release(const QString& name);
    bool compare(int preset);                           // -1 = use the workload field
    const std::vector<AllocCompareRow>& comparison() const { return rows_; }
    void selectFile(int inode);
    int selectedFile() const;
private:
    void refreshInspector();
    StorageArea* area_;
    ModuleHeader* header_;
    DiskGridView* grid_;
    QLineEdit *name_, *workload_;
    QSpinBox *bytes_, *logical_, *cmpBlocks_, *cmpSize_;
    QComboBox *file_, *preset_;
    QTableWidget *map_, *cmp_;
    QListWidget* traverse_;
    QLabel *stats_, *presetText_;
    DiskGridView* cmpGrid_[3];
    QPlainTextEdit* cmpLog_;
    std::vector<AllocCompareRow> rows_;
    bool updating_ = false;
};

class RecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit RecoveryPage(StorageArea* area);
    void refresh();
    bool loadPreset(int i);
    bool deleteFile(const QString& path);
    bool overwrite(int bytes);                  // new filler file reusing freed blocks
    bool carve();
    bool loseMetadata();
    void selectCandidate(int row);              // row in the candidate table
    int candidateCount() const;
    const RecoveryReport& current() const { return report_; }
    FsResult restore(const QString& newPath);
private:
    void showReport();
    StorageArea* area_;
    ModuleHeader* header_;
    DiskGridView* grid_;
    QComboBox* preset_;
    QLineEdit *delPath_, *restorePath_;
    QSpinBox* fill_;
    QTableWidget *cands_, *evidence_;
    QPlainTextEdit *preview_, *log_;
    QLabel *cls_, *explain_;
    std::vector<CarveCandidate> carved_;
    RecoveryReport report_;
    int selRow_ = -1;
    bool updating_ = false;
};

class StorageArea : public QTabWidget {
    Q_OBJECT
public:
    explicit StorageArea(QWidget* parent = nullptr);
    VirtualFs& fs() { return fs_; }
    const VirtualFs& fs() const { return fs_; }
    bool newDisk(int blocks, int blockSize, AllocStrategy s, bool sample);
    void replaceFs(const VirtualFs& fs, const QString& why);
    bool saveImage(const QString& path, QString* err = nullptr) const;
    bool loadImage(const QString& path, QString* err = nullptr);
    ExplorerPage* explorer() const { return explorer_; }
    AccessPage* access() const { return access_; }
    BlockPage* blocks() const { return blocks_; }
    RecoveryPage* recovery() const { return recovery_; }
    void report(const FsResult& r, const QString& what);   // log line + changed()
    QString reportText() const;
    QString csvSection() const;
signals:
    void changed();
    void logMessage(const QString& line);
public slots:
    void notifyChanged();
private:
    VirtualFs fs_;
    ExplorerPage* explorer_;
    AccessPage* access_;
    BlockPage* blocks_;
    RecoveryPage* recovery_;
};
