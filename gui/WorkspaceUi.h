// WorkspaceUi.h - widgets of the Modules menu: the checkable module tree (embedded in the menu
// through a QWidgetAction, so the menu stays open while several boxes are changed) and the
// Manage Workspace dialog (searchable tree, counters, bulk actions, presets). Both only call
// ModuleManager; neither knows any simulation.
#pragma once
#include <QDialog>
#include <QWidget>

class QTreeWidget; class QTreeWidgetItem; class QLineEdit; class QLabel; class QListWidget; class QPushButton;
class ModuleManager;

class ModuleTree : public QWidget {
    Q_OBJECT
public:
    // compact = the version inside the menu (fixed height with scrolling, smaller margins).
    ModuleTree(ModuleManager* m, bool compact, QWidget* parent = nullptr);
    void refresh();                                  // check states, counters, enabled state
    void setFilter(const QString& text);             // search: shows matches and their parents
    void expandAll();
    void collapseAll();
    QTreeWidget* tree() const { return tree_; }
    QLineEdit* search() const { return search_; }
    QString lastMessage() const;
    QTreeWidgetItem* itemFor(const QString& id) const;
    // Toggles one entry as a click on its checkbox would (used by the self-test).
    bool clickCheck(const QString& id);
    QSize sizeHint() const override;
private:
    void build();
    void onItemChanged(QTreeWidgetItem* it, int col);
    ModuleManager* m_;
    bool compact_;
    QTreeWidget* tree_;
    QLineEdit* search_;
    QLabel *counter_, *message_;
    bool updating_ = false;
};

class WorkspaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit WorkspaceDialog(ModuleManager* m, QWidget* parent = nullptr);
    ModuleTree* moduleTree() const { return tree_; }
    QListWidget* presetList() const { return presets_; }
    void refreshPresets();
private:
    QString selectedPreset(bool* builtin = nullptr) const;
    ModuleManager* m_;
    ModuleTree* tree_;
    QListWidget* presets_;
    QLabel* presetInfo_;
    QPushButton *apply_, *rename_, *delete_;
};
