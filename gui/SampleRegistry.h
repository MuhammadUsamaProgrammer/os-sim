// SampleRegistry.h - one central catalogue of the built-in educational samples of every module.
//
//   SampleDef       one sample: stable id, name, description, the module (workspace id such as
//                   "scheduling.cpu"), a category, the algorithm / mode it demonstrates, its key
//                   parameters, a level, the backing sample file (if any) and a loading handler.
//                   The handler only POPULATES the module's inputs (it never starts a simulation);
//                   the optional run handler is used only by an explicit "Load && Run".
//   SampleRegistry  owns the definitions; load(id) validates, asks the guard (stop / pause an active
//                   simulation, confirm before replacing shared state), calls the handler and emits
//                   loaded() exactly once. The File menu and every in-module "Load Sample" button go
//                   through the same load() call, so both paths behave identically.
//   SampleDialog    compact browser for one module (or all): categories -> samples, details pane,
//                   Load / Load && Run.
//
// The adapters (MainWindow::registerSamples, gui/SampleCatalog.cpp) reuse the existing preset
// providers (core preset lists, tab presets, sample files); no workload is defined twice.
// Built-in sample files are embedded as Qt resources (:/samples) so they are available from any
// working directory: ui::samplesDir() falls back to an extracted copy (see SampleRegistry.cpp).
#pragma once
#include <QDialog>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

struct SampleDef {
    QString id;            // stable, unique: "<module>/<short-name>"
    QString name;          // display name
    QString description;   // short explanation of the scenario
    QString module;        // ModuleManager id of the page that owns the sample (e.g. "memory.heap")
    QString category;      // "Basic Examples", "Course Examples", "Algorithm-Specific Examples", ...
    QString mode;          // algorithm or simulation mode it demonstrates
    QString params;        // the relevant input parameters, in a few words
    QString level;         // "Beginner", "Intermediate" or "Advanced" (optional)
    QString file;          // backing file in samples/ ("" = built-in preset of the module)
    bool replacesShared = false;   // replaces state shared by several pages (Storage virtual disk)
    std::function<bool()> load;    // populates the inputs; returns false on invalid data (warning shown)
    std::function<bool()> run;     // optional: starts execution (only for "Load && Run")
};

class SampleRegistry : public QObject {
    Q_OBJECT
public:
    explicit SampleRegistry(QObject* parent = nullptr);
    // Adds a definition; a duplicate id or a missing handler is rejected (returns false).
    bool add(const SampleDef& d);
    const std::vector<SampleDef>& all() const { return defs_; }
    const SampleDef* find(const QString& id) const;
    // Samples of one module in registration order (only that module: never the parent's or a sibling's).
    std::vector<const SampleDef*> forModule(const QString& module) const;
    // Categories of one module in order of first appearance.
    QStringList categories(const QString& module) const;
    QStringList modules() const;                       // modules with samples, registration order
    // First sample backed by this file name (File menu reuse), or nullptr.
    const SampleDef* forFile(const QString& fileName) const;

    // Called before a load; returns false to cancel (user said no). Used for stop / pause / confirm.
    void setGuard(std::function<bool(const SampleDef&)> g) { guard_ = std::move(g); }
    // Validates, guards, loads; with run = true also starts it (Load && Run). Emits loaded() once.
    bool load(const QString& id, bool run = false);
    QString current(const QString& module) const;      // id of the last sample loaded into module
    int loadCount() const { return loads_; }

    // Text inventory (module -> category -> sample), used by --sample-report and the docs.
    QString report(const std::function<QString(const QString&)>& moduleName = {}) const;

signals:
    void loaded(const SampleDef& d);
    void loadFailed(const SampleDef& d);

private:
    std::vector<SampleDef> defs_;
    std::function<bool(const SampleDef&)> guard_;
    std::vector<std::pair<QString, QString>> current_;   // module -> sample id
    int loads_ = 0;
};

class QTreeWidget; class QLabel; class QPushButton;

// Compact sample browser: categories and samples on the left, the details on the right.
class SampleDialog : public QDialog {
    Q_OBJECT
public:
    SampleDialog(SampleRegistry* reg, const QString& module, const QString& title, QWidget* parent = nullptr);
    QString selectedId() const;
    bool select(const QString& id);
    int sampleCount() const;
    bool runRequested() const { return run_; }
private:
    void showDetails();
    SampleRegistry* reg_;
    QTreeWidget* tree_;
    QLabel* details_;
    QPushButton *load_, *loadRun_;
    bool run_ = false;
};

namespace ui {
// Tooltip / details text of a sample (description, mode, parameters, level, file).
QString sampleDetails(const SampleDef& d, bool html);
// True when the embedded copy of the samples folder is used (no samples/ folder was found on disk).
bool usingEmbeddedSamples();
}
