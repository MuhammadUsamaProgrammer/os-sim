// WorkspaceUi.cpp - module tree (Modules menu) and Manage Workspace dialog.
#include "WorkspaceUi.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QTreeWidgetItemIterator>
#include <functional>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "ModuleManager.h"
#include "Style.h"

static const int kIdRole = Qt::UserRole + 1;

// ================================================================ ModuleTree

ModuleTree::ModuleTree(ModuleManager* m, bool compact, QWidget* parent) : QWidget(parent), m_(m), compact_(compact) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(compact ? 8 : 0, compact ? 6 : 0, compact ? 8 : 0, compact ? 6 : 0);
    v->setSpacing(6);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search modules...");
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName("Search modules");
    search_->setToolTip("Type part of a module name; matching modules and their parents stay listed");
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->addWidget(search_, 1);
    auto* ex = new QPushButton("Expand All");
    ex->setToolTip("Expand every module group");
    auto* co = new QPushButton("Collapse All");
    co->setToolTip("Collapse every module group");
    for (QPushButton* b : {ex, co}) { b->setAutoDefault(false); b->setFocusPolicy(Qt::TabFocus); row->addWidget(b); }
    v->addLayout(row);
    tree_ = new QTreeWidget;
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"Module", "Shown"});
    tree_->setHeaderHidden(compact);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setUniformRowHeights(true);
    // Fusion draws the checkbox frame from the Window colour (darker): in the dark theme that frame
    // disappears on the dark base, so an unticked module would look blank. A mid grey Window role
    // (only this role; the rest follows the theme) keeps the frame visible in both themes.
    QPalette tp = tree_->palette();
    tp.setColor(QPalette::Window, QColor("#a4a8ae"));
    tree_->setPalette(tp);
    tree_->setAccessibleName("Module visibility tree");
    tree_->setToolTip("Tick a module to show it, untick to hide it (Space toggles the selected row)");
    v->addWidget(tree_, 1);
    counter_ = new QLabel;
    counter_->setWordWrap(true);
    message_ = new QLabel;
    message_->setWordWrap(true);
    message_->setStyleSheet(QString("color: %1;").arg(ui::faultColor().name()));
    message_->hide();
    v->addWidget(counter_);
    v->addWidget(message_);
    build();
    connect(ex, &QPushButton::clicked, this, &ModuleTree::expandAll);
    connect(co, &QPushButton::clicked, this, &ModuleTree::collapseAll);
    connect(search_, &QLineEdit::textChanged, this, &ModuleTree::setFilter);
    connect(tree_, &QTreeWidget::itemChanged, this, &ModuleTree::onItemChanged);
    connect(m_, &ModuleManager::changed, this, &ModuleTree::refresh);
    refresh();
}

// One item per registered entry, nested like the navigation (any depth).
void ModuleTree::build() {
    updating_ = true;
    tree_->clear();
    std::function<void(const std::string&, QTreeWidgetItem*)> add = [&](const std::string& parent, QTreeWidgetItem* pi) {
        for (const std::string& id : m_->workspace().children(parent)) {
            auto* it = pi ? new QTreeWidgetItem(pi) : new QTreeWidgetItem(tree_);
            QString qid = QString::fromStdString(id);
            it->setText(0, QString::fromStdString(m_->workspace().info(id).name));
            it->setData(0, kIdRole, qid);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            bool kids = !m_->workspace().children(id).empty();
            it->setIcon(0, style()->standardIcon(kids ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
            add(id, it);
        }
    };
    add("", nullptr);
    updating_ = false;
}

QTreeWidgetItem* ModuleTree::itemFor(const QString& id) const {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, kIdRole).toString() == id) return *it;
    return nullptr;
}

// Check states and counters follow the manager. A child of a hidden parent keeps its own check
// mark (its stored preference) but is disabled until the parent is shown again.
void ModuleTree::refresh() {
    updating_ = true;
    const Workspace& w = m_->workspace();
    int tops = 0, topsOn = 0;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        std::string id = (*it)->data(0, kIdRole).toString().toStdString();
        if (!w.has(id)) continue;
        const ModuleInfo& mi = w.info(id);
        (*it)->setCheckState(0, w.preference(id) ? Qt::Checked : Qt::Unchecked);
        bool parentOn = mi.parent.empty() || w.effective(mi.parent);
        (*it)->setDisabled(!parentOn);
        size_t kids = w.children(id).size();
        QString tip = QString("%1  (id: %2)").arg(QString::fromStdString(mi.name), QString::fromStdString(id));
        if (kids) {
            int on = w.visibleChildren(id);
            (*it)->setText(1, QString("%1/%2").arg(on).arg(kids));
            QFont f = (*it)->font(0);
            f.setItalic(w.preference(id) && on < (int)kids);   // partially enabled parent
            (*it)->setFont(0, f);
            tip += on < (int)kids ? QString("\n%1 of %2 parts shown (partial)").arg(on).arg(kids) : QString("\nall %1 parts shown").arg(kids);
        } else {
            (*it)->setText(1, w.preference(id) ? (parentOn ? "shown" : "kept") : "hidden");
        }
        if (!parentOn) tip += "\nThe parent module is hidden: this choice is kept and applies when it is shown again.";
        (*it)->setToolTip(0, tip);
        (*it)->setToolTip(1, tip);
        if (mi.parent.empty()) { ++tops; if (w.preference(id)) ++topsOn; }
    }
    int all = (int)w.modules().size();
    counter_->setText(QString("%1 of %2 views visible · %3 of %4 main modules")
                          .arg(w.effectiveCount()).arg(all).arg(topsOn).arg(tops));
    updating_ = false;
}

void ModuleTree::onItemChanged(QTreeWidgetItem* it, int col) {
    if (updating_ || col != 0) return;
    QString id = it->data(0, kIdRole).toString();
    bool on = it->checkState(0) == Qt::Checked;
    QString why;
    if (!m_->setVisible(id, on, &why)) {
        message_->setText(why);
        message_->show();
        refresh();                                             // put the check mark back
        return;
    }
    message_->hide();
    message_->clear();
}

bool ModuleTree::clickCheck(const QString& id) {
    QTreeWidgetItem* it = itemFor(id);
    if (!it || it->isDisabled()) return false;
    bool before = m_->isVisible(id);
    it->setCheckState(0, it->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    return m_->isVisible(id) != before;
}

QString ModuleTree::lastMessage() const { return message_->isVisible() || !message_->text().isEmpty() ? message_->text() : QString(); }

// Search: an item stays listed if its name or id matches, or one of its descendants does.
void ModuleTree::setFilter(const QString& text) {
    QString t = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
        bool any = false;
        for (int i = 0; i < it->childCount(); ++i) any = walk(it->child(i)) || any;
        bool self = t.isEmpty() || it->text(0).contains(t, Qt::CaseInsensitive) || it->data(0, kIdRole).toString().contains(t, Qt::CaseInsensitive);
        it->setHidden(!(self || any));
        if (!t.isEmpty() && any) it->setExpanded(true);
        return self || any;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) walk(tree_->topLevelItem(i));
}

void ModuleTree::expandAll() { tree_->expandAll(); }
void ModuleTree::collapseAll() { tree_->collapseAll(); }

// In the menu the tree is as tall as needed for the main modules plus a few children, capped so
// the menu always fits the screen; it scrolls inside. Width comes from the font (no fixed pixels).
QSize ModuleTree::sizeHint() const {
    QFontMetrics fm(font());
    int rows = 0;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) ++rows;
    int rowH = std::max(fm.height() + 6, 20);
    int shown = compact_ ? std::min(rows, 14) : std::min(rows, 24);
    int w = fm.horizontalAdvance("Paging && Address Translation") + fm.horizontalAdvance("00/00") + 120;
    return QSize(w, shown * rowH + search_->sizeHint().height() + fm.height() * 3 + 30);
}

// ================================================================ WorkspaceDialog

WorkspaceDialog::WorkspaceDialog(ModuleManager* m, QWidget* parent) : QDialog(parent), m_(m) {
    setWindowTitle("Manage Workspace - OS-Sim");
    setModal(false);
    auto* outer = new QVBoxLayout(this);
    auto* intro = new QLabel("Choose which modules appear in the navigation. Changes apply immediately; hidden modules keep "
                             "their inputs, results and playback position. Hiding a running simulation pauses it.");
    intro->setWordWrap(true);
    outer->addWidget(intro);
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    // left: the tree
    auto* treeBox = new QGroupBox("Modules");
    auto* tl = new QVBoxLayout(treeBox);
    tree_ = new ModuleTree(m, false);
    tl->addWidget(tree_);
    split->addWidget(treeBox);
    // right: bulk actions and presets
    auto* side = new QWidget;
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    auto* bulk = new QGroupBox("Quick actions");
    auto* bl = new QVBoxLayout(bulk);
    auto mk = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setToolTip(tip);
        b->setAutoDefault(false);
        bl->addWidget(b);
        return b;
    };
    QPushButton* all = mk("Show All Modules", "Show every module and every child view");
    QPushButton* only = mk("Show Current Module Only", "Keep the selected main module and hide the other main modules (child choices are kept)");
    QPushButton* reset = mk("Reset to Default", "Restore the default visibility of every module");
    sl->addWidget(bulk);
    auto* pBox = new QGroupBox("Workspace presets");
    auto* pl = new QVBoxLayout(pBox);
    presets_ = new QListWidget;
    presets_->setAccessibleName("Workspace presets");
    presets_->setMinimumHeight(QFontMetrics(font()).height() * 6);
    pl->addWidget(presets_, 1);
    presetInfo_ = new QLabel;
    presetInfo_->setWordWrap(true);
    pl->addWidget(presetInfo_);
    auto* r1 = new QHBoxLayout;
    apply_ = new QPushButton("Apply");
    apply_->setToolTip("Apply the selected preset (visibility only - simulations are not reset)");
    auto* save = new QPushButton("Save Current As...");
    save->setToolTip("Save the current selection as a custom preset");
    r1->addWidget(apply_);
    r1->addWidget(save);
    auto* r2 = new QHBoxLayout;
    rename_ = new QPushButton("Rename...");
    delete_ = new QPushButton("Delete");
    rename_->setToolTip("Rename the selected custom preset");
    delete_->setToolTip("Delete the selected custom preset");
    r2->addWidget(rename_);
    r2->addWidget(delete_);
    for (QPushButton* b : {apply_, save, rename_, delete_}) b->setAutoDefault(false);
    pl->addLayout(r1);
    pl->addLayout(r2);
    sl->addWidget(pBox, 1);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    connect(all, &QPushButton::clicked, m_, &ModuleManager::showAll);
    connect(only, &QPushButton::clicked, m_, [this] { m_->showCurrentOnly(); });
    connect(reset, &QPushButton::clicked, m_, &ModuleManager::resetToDefaults);
    connect(presets_, &QListWidget::currentRowChanged, this, [this] {
        bool builtin = false;
        QString n = selectedPreset(&builtin);
        apply_->setEnabled(!n.isEmpty());
        rename_->setEnabled(!n.isEmpty() && !builtin);
        delete_->setEnabled(!n.isEmpty() && !builtin);
        presetInfo_->setText(n.isEmpty() ? QString() : builtin ? m_->builtinPresetDescription(n) : QString("Custom preset (saved selection)."));
    });
    connect(presets_, &QListWidget::itemActivated, this, [this] { apply_->click(); });
    connect(apply_, &QPushButton::clicked, this, [this] {
        QString n = selectedPreset();
        if (!n.isEmpty()) m_->applyPreset(n);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QString name = QInputDialog::getText(this, "Save workspace preset", "Preset name:", QLineEdit::Normal, "My workspace", &ok);
        if (!ok) return;
        QString why;
        if (!m_->saveCustomPreset(name, &why)) ui::warn(this, why);
    });
    connect(rename_, &QPushButton::clicked, this, [this] {
        bool builtin = false;
        QString n = selectedPreset(&builtin);
        if (n.isEmpty() || builtin) return;
        bool ok = false;
        QString to = QInputDialog::getText(this, "Rename workspace preset", "New name:", QLineEdit::Normal, n, &ok);
        if (!ok) return;
        QString why;
        if (!m_->renameCustomPreset(n, to, &why)) ui::warn(this, why);
    });
    connect(delete_, &QPushButton::clicked, this, [this] {
        bool builtin = false;
        QString n = selectedPreset(&builtin);
        if (n.isEmpty() || builtin) return;
        if (!ui::selfTestMode() &&
            QMessageBox::question(this, "Delete preset", "Delete the custom preset \"" + n + "\"?") != QMessageBox::Yes) return;
        m_->deleteCustomPreset(n);
    });
    connect(m_, &ModuleManager::changed, this, &WorkspaceDialog::refreshPresets);
    refreshPresets();
    tree_->expandAll();
    QFontMetrics fm(font());
    resize(fm.horizontalAdvance('M') * 70, fm.height() * 34);
}

static const int kBuiltinRole = Qt::UserRole + 2;

// Lists the built-in presets, then the custom ones (keeps the selection when possible).
void WorkspaceDialog::refreshPresets() {
    QString keep = selectedPreset();
    presets_->blockSignals(true);
    presets_->clear();
    for (const QString& n : m_->builtinPresetNames()) {
        auto* it = new QListWidgetItem(n + "  (built-in)");
        it->setData(Qt::UserRole, n);
        it->setData(kBuiltinRole, true);
        it->setToolTip(m_->builtinPresetDescription(n));
        presets_->addItem(it);
    }
    for (const QString& n : m_->customPresetNames()) {
        auto* it = new QListWidgetItem(n);
        it->setData(Qt::UserRole, n);
        it->setData(kBuiltinRole, false);
        it->setToolTip("Custom preset");
        presets_->addItem(it);
    }
    int row = 0;
    for (int i = 0; i < presets_->count(); ++i)
        if (presets_->item(i)->data(Qt::UserRole).toString() == keep) row = i;
    presets_->blockSignals(false);
    presets_->setCurrentRow(row);
    emit presets_->currentRowChanged(row);
}

QString WorkspaceDialog::selectedPreset(bool* builtin) const {
    QListWidgetItem* it = presets_->currentItem();
    if (builtin) *builtin = it && it->data(kBuiltinRole).toBool();
    return it ? it->data(Qt::UserRole).toString() : QString();
}
