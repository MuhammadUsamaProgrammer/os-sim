// ModuleManager.h - Module Visibility Manager (GUI adapter of core/workspace.h).
//
// One central registry for every navigation entry: the parent tabs of the main window and the
// child views inside the sub-tab areas (Scheduling, Processes, Memory, Storage & Files). Each
// entry has a stable id ("memory.heap"), its QTabWidget and its page widget. Visibility is applied
// with QTabWidget::setTabVisible on the page's CURRENT index (looked up by widget pointer, never a
// stored index), so pages are never destroyed, re-created, re-parented or re-connected: their
// inputs, results, logs and playback history survive any number of hide / show cycles.
//
// Lifecycle policy when an entry becomes effectively hidden (its own or a parent's checkbox):
//   - deterministic playback (every PlaybackBar on the page, and the registered pause hooks:
//     the Scheduling engine replay and the Synchronization animation) is paused, never reset;
//     showing the page again shows the paused state and the user resumes manually;
//   - the System Monitor stops sampling through its own hideEvent and restarts when shown;
//   - Live Linux demos keep their own bounded lifecycle (owned helper processes only); nothing
//     is ever killed or signalled because of a visibility change.
// Everything runs on the GUI thread (it is only called from menu / dialog / self-test code).
#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <string>
#include "workspace.h"

class QTabWidget; class QWidget; class QSettings;

class ModuleManager : public QObject {
    Q_OBJECT
public:
    explicit ModuleManager(QObject* parent = nullptr);

    // Registers an entry shown as a tab of `area` (page = the widget added to that QTabWidget).
    // Parents must be registered first; the original order is the registration order per parent.
    void add(const QString& id, const QString& name, const QString& parent, QTabWidget* area, QWidget* page,
             bool defaultVisible = true);
    // Extra pause action for a page whose playback is not a PlaybackBar.
    void setPauseHook(const QString& id, std::function<void()> f);

    const Workspace& workspace() const { return ws_; }
    QStringList ids() const;                                   // registration order
    QString name(const QString& id) const;
    QTabWidget* area(const QString& id) const;
    QWidget* page(const QString& id) const;
    bool isVisible(const QString& id) const { return ws_.preference(id.toStdString()); }    // preference
    bool isEffective(const QString& id) const { return ws_.effective(id.toStdString()); }   // can be seen
    // Registered entry that contains w (w itself or its nearest registered ancestor), or "".
    QString idOfWidget(const QWidget* w) const;
    // True when every registered entry around w is effectively visible (w can be navigated to).
    bool reachable(const QWidget* w) const;

    // ---- changes (each one applies immediately, emits changed() and saves when a store is set)
    bool setVisible(const QString& id, bool on, QString* why = nullptr);
    void showAll();
    void resetToDefaults();
    bool showCurrentOnly();                                    // keeps the selected main module
    void applyMap(const VisibilityMap& m);

    // Selected entry: the deepest selected, visible page (e.g. "memory.heap"); select() shows one.
    QString currentId() const;
    QString currentTopId() const;
    bool select(const QString& id);

    // ---- presets (built-in ones come from core/workspace.cpp; custom ones are saved by name)
    QStringList builtinPresetNames() const;
    QString builtinPresetDescription(const QString& name) const;
    QStringList customPresetNames() const;
    bool applyPreset(const QString& name);                     // built-in or custom
    bool saveCustomPreset(const QString& name, QString* why = nullptr);   // current selection
    bool renameCustomPreset(const QString& from, const QString& to, QString* why = nullptr);
    bool deleteCustomPreset(const QString& name);

    // ---- persistence (QSettings group "modules/"; other settings are never touched)
    // Store = QSettings organisation used for automatic load / save; empty = no persistence
    // (self-test and layout report use defaults and never write the user's settings).
    void setStore(const QString& organisation);
    QString store() const { return store_; }
    void load(QSettings& s);                                   // missing / bad values -> defaults
    void save(QSettings& s) const;
    void loadFromStore();
    void saveToStore() const;
    void saveCurrentToStore() const;                           // only the last selected entry
    void restoreCurrentFromStore();
    QStringList loadWarnings() const { return warnings_; }     // what load() had to repair
    static constexpr int kSettingsVersion = 1;

    // Text tree of the registry ("[x] Memory (6/8)") used by --workspace-report and the docs.
    QString report() const;
    int applyCount() const { return applies_; }                // for tests: applications so far

signals:
    void changed();                                            // preferences / selection changed

private:
    struct Bind {
        QPointer<QTabWidget> area;
        QPointer<QWidget> page;
        std::function<void()> pauseHook;
    };
    // Pushes the model to the tab widgets; pauses entries that became hidden.
    void applyToTabs(const VisibilityMap& effectiveBefore);
    VisibilityMap effectiveMap() const;
    void moveSelectionOff(const std::string& id);
    void pauseEntry(const std::string& id);
    void changedNow(const VisibilityMap& before);

    Workspace ws_;
    std::map<std::string, Bind> bind_;
    std::map<std::string, int> childCounter_;
    std::vector<std::pair<QString, VisibilityMap>> custom_;   // custom presets in creation order
    QString store_;
    QStringList warnings_;
    int applies_ = 0;
};
