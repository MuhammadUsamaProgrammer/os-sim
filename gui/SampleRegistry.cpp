// SampleRegistry.cpp - central sample catalogue, sample browser dialog and the samples folder
// lookup (disk first, embedded Qt resources as the always-available fallback).
#include "SampleRegistry.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <memory>
#include "Style.h"

// ================================================================ samples folder

namespace {
bool g_embedded = false;

// A usable samples folder must contain the reference workload.
bool validSamplesDir(const QString& d) { return QFile::exists(d + "/processes_fcfs.csv"); }

// Copies the embedded :/samples resources into a private temporary folder once per process.
QString extractEmbedded() {
    static std::unique_ptr<QTemporaryDir> tmp;
    static QString path;
    if (!path.isEmpty() && validSamplesDir(path)) return path;
    tmp = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/ossim-samples-XXXXXX");
    if (!tmp->isValid()) return QString(":/samples");
    QString out = tmp->path() + "/samples";
    QDir().mkpath(out);
    QDirIterator it(":/samples", QDir::Files);
    while (it.hasNext()) {
        QString src = it.next();
        QString dst = out + "/" + QFileInfo(src).fileName();
        if (QFile::copy(src, dst)) QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser);
    }
    path = out;
    return path;
}
}

namespace ui {
// Folder with the sample files. Order: $OSSIM_SAMPLES, next to the executable (installed or build
// tree), the working directory, and finally the copy embedded in the executable (:/samples), so the
// built-in samples work from any working directory. OSSIM_FORCE_EMBEDDED_SAMPLES=1 skips the disk
// folders (used by the "samples from anywhere" test). No absolute source path is compiled in.
QString samplesDir() {
    g_embedded = false;
    QStringList cands;
    QString env = qEnvironmentVariable("OSSIM_SAMPLES");
    if (!env.isEmpty()) cands << env;
    if (qEnvironmentVariable("OSSIM_FORCE_EMBEDDED_SAMPLES").isEmpty()) {
        QString app = QCoreApplication::applicationDirPath();
        cands << app + "/samples" << app + "/../samples" << app + "/../../samples" << app + "/../share/os-sim/samples"
              << "samples" << "../samples";
    }
    for (const QString& c : cands)
        if (validSamplesDir(c)) return QDir(c).absolutePath();
    g_embedded = true;
    return extractEmbedded();
}

bool usingEmbeddedSamples() { samplesDir(); return g_embedded; }

QString sampleDetails(const SampleDef& d, bool html) {
    QStringList rows;
    auto row = [&](const QString& k, const QString& v) {
        if (v.isEmpty()) return;
        rows << (html ? QString("<b>%1:</b> %2").arg(k, v.toHtmlEscaped()) : QString("%1: %2").arg(k, v));
    };
    row("Mode", d.mode);
    row("Parameters", d.params);
    row("Category", d.category);
    row("Level", d.level);
    row("Sample file", d.file.isEmpty() ? QString() : "samples/" + d.file);
    if (d.replacesShared) row("Note", "replaces the shared virtual disk of all Storage & Files pages");
    QString head = html ? QString("<p><b>%1</b></p><p>%2</p>").arg(d.name.toHtmlEscaped(), d.description.toHtmlEscaped())
                        : d.name + "\n" + d.description;
    return head + (html ? "<p>" + rows.join("<br>") + "</p>" : "\n" + rows.join("\n"));
}
}

// ================================================================ registry

SampleRegistry::SampleRegistry(QObject* parent) : QObject(parent) {}

bool SampleRegistry::add(const SampleDef& d) {
    if (d.id.isEmpty() || d.module.isEmpty() || !d.load || find(d.id)) return false;
    defs_.push_back(d);
    if (defs_.back().category.isEmpty()) defs_.back().category = "Basic Examples";
    return true;
}

const SampleDef* SampleRegistry::find(const QString& id) const {
    for (const SampleDef& d : defs_) if (d.id == id) return &d;
    return nullptr;
}

std::vector<const SampleDef*> SampleRegistry::forModule(const QString& module) const {
    std::vector<const SampleDef*> v;
    for (const SampleDef& d : defs_) if (d.module == module) v.push_back(&d);
    return v;
}

QStringList SampleRegistry::categories(const QString& module) const {
    QStringList c;
    for (const SampleDef* d : forModule(module)) if (!c.contains(d->category)) c << d->category;
    return c;
}

QStringList SampleRegistry::modules() const {
    QStringList m;
    for (const SampleDef& d : defs_) if (!m.contains(d.module)) m << d.module;
    return m;
}

const SampleDef* SampleRegistry::forFile(const QString& fileName) const {
    for (const SampleDef& d : defs_) if (!d.file.isEmpty() && d.file == fileName) return &d;
    return nullptr;
}

bool SampleRegistry::load(const QString& id, bool run) {
    const SampleDef* d = find(id);
    if (!d) return false;
    SampleDef def = *d;                            // a copy: handlers may add nothing, but stay safe
    if (guard_ && !guard_(def)) return false;      // cancelled by the user (or nothing to do)
    bool ok = false;
    try { ok = def.load(); } catch (const std::exception& e) { ui::warn(nullptr, QString("Sample '%1': %2").arg(def.name, e.what())); }
    if (!ok) { emit loadFailed(def); return false; }
    ++loads_;
    bool found = false;
    for (auto& kv : current_) if (kv.first == def.module) { kv.second = def.id; found = true; }
    if (!found) current_.push_back({def.module, def.id});
    emit loaded(def);
    if (run && def.run) def.run();
    return true;
}

QString SampleRegistry::current(const QString& module) const {
    for (const auto& kv : current_) if (kv.first == module) return kv.second;
    return QString();
}

QString SampleRegistry::report(const std::function<QString(const QString&)>& moduleName) const {
    QString out;
    for (const QString& m : modules()) {
        std::vector<const SampleDef*> v = forModule(m);
        out += QString("%1 [%2] - %3 sample(s)\n").arg(moduleName ? moduleName(m) : m, m).arg(v.size());
        for (const QString& c : categories(m)) {
            out += "  " + c + "\n";
            for (const SampleDef* d : v)
                if (d->category == c)
                    out += QString("    - %1  (%2%3)\n").arg(d->name, d->mode, d->file.isEmpty() ? QString() : ", samples/" + d->file);
        }
    }
    return out;
}

// ================================================================ dialog

SampleDialog::SampleDialog(SampleRegistry* reg, const QString& module, const QString& title, QWidget* parent)
    : QDialog(parent), reg_(reg) {
    setWindowTitle(title.isEmpty() ? QString("Load Sample") : "Load Sample - " + title);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({"Sample", "Mode"});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setMinimumWidth(fontMetrics().averageCharWidth() * 40);
    QStringList mods = module.isEmpty() ? reg->modules() : QStringList{module};
    for (const QString& m : mods) {
        QTreeWidgetItem* modItem = nullptr;
        if (mods.size() > 1) { modItem = new QTreeWidgetItem(tree_, {m}); modItem->setFirstColumnSpanned(true); }
        for (const QString& c : reg->categories(m)) {
            auto* cat = modItem ? new QTreeWidgetItem(modItem, {c}) : new QTreeWidgetItem(tree_, {c});
            QFont f = cat->font(0); f.setBold(true); cat->setFont(0, f);
            cat->setFlags(Qt::ItemIsEnabled);
            for (const SampleDef* d : reg->forModule(m)) {
                if (d->category != c) continue;
                auto* it = new QTreeWidgetItem(cat, {d->name, d->mode});
                it->setData(0, Qt::UserRole, d->id);
                it->setToolTip(0, ui::sampleDetails(*d, false));
            }
        }
    }
    tree_->expandAll();
    details_ = new QLabel("Choose a sample.");
    details_->setWordWrap(true);
    details_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    details_->setTextFormat(Qt::RichText);
    details_->setMinimumWidth(fontMetrics().averageCharWidth() * 30);
    auto* split = new QSplitter;
    split->addWidget(tree_);
    split->addWidget(details_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    auto* box = new QDialogButtonBox;
    load_ = box->addButton("Load", QDialogButtonBox::AcceptRole);
    load_->setToolTip("Fill the module's inputs with this sample; you start the simulation yourself");
    loadRun_ = box->addButton("Load && Run", QDialogButtonBox::AcceptRole);
    loadRun_->setToolTip("Fill the inputs and start the simulation at once");
    box->addButton(QDialogButtonBox::Cancel);
    connect(load_, &QPushButton::clicked, this, [this] { run_ = false; accept(); });
    connect(loadRun_, &QPushButton::clicked, this, [this] { run_ = true; accept(); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { showDetails(); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) { run_ = false; accept(); }
    });
    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(8);
    auto* hint = new QLabel("Built-in educational workloads. Loading fills the inputs only; "
                            "use <b>File &gt; Open File...</b> to import your own file.");
    hint->setWordWrap(true);
    lay->addWidget(hint);
    lay->addWidget(split, 1);
    lay->addWidget(box);
    resize(fontMetrics().averageCharWidth() * 110, fontMetrics().height() * 28);
    // preselect the current sample of the module, else the first sample
    QString cur = module.isEmpty() ? QString() : reg->current(module);
    if (cur.isEmpty() || !select(cur)) {
        QTreeWidgetItemIterator it(tree_);
        while (*it && (*it)->data(0, Qt::UserRole).toString().isEmpty()) ++it;
        if (*it) tree_->setCurrentItem(*it);
    }
    showDetails();
}

QString SampleDialog::selectedId() const {
    return tree_->currentItem() ? tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
}

bool SampleDialog::select(const QString& id) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == id) { tree_->setCurrentItem(*it); return true; }
    return false;
}

int SampleDialog::sampleCount() const {
    int n = 0;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) n += !(*it)->data(0, Qt::UserRole).toString().isEmpty();
    return n;
}

void SampleDialog::showDetails() {
    const SampleDef* d = reg_->find(selectedId());
    load_->setEnabled(d != nullptr);
    loadRun_->setEnabled(d && d->run);
    details_->setText(d ? ui::sampleDetails(*d, true) : QString("Choose a sample."));
}
