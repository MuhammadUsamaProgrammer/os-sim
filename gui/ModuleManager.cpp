// ModuleManager.cpp - Module Visibility Manager: registry, tab visibility, presets, settings.
#include "ModuleManager.h"
#include <QSettings>
#include <QTabWidget>
#include <QWidget>
#include "ModuleKit.h"

ModuleManager::ModuleManager(QObject* parent) : QObject(parent) {}

void ModuleManager::add(const QString& id, const QString& name, const QString& parent, QTabWidget* area,
                        QWidget* page, bool defaultVisible) {
    std::string p = parent.toStdString();
    ModuleInfo m{id.toStdString(), name.toStdString(), p, childCounter_[p]++, defaultVisible};
    ws_.add(m);                                                // throws on a bad / duplicate id
    bind_[m.id] = Bind{area, page, {}};
    applyToTabs(effectiveMap());
}

void ModuleManager::setPauseHook(const QString& id, std::function<void()> f) {
    auto it = bind_.find(id.toStdString());
    if (it != bind_.end()) it->second.pauseHook = std::move(f);
}

QStringList ModuleManager::ids() const {
    QStringList v;
    for (const ModuleInfo& m : ws_.modules()) v << QString::fromStdString(m.id);
    return v;
}

QString ModuleManager::name(const QString& id) const {
    return ws_.has(id.toStdString()) ? QString::fromStdString(ws_.info(id.toStdString()).name) : QString();
}

QTabWidget* ModuleManager::area(const QString& id) const {
    auto it = bind_.find(id.toStdString());
    return it == bind_.end() ? nullptr : it->second.area.data();
}

QWidget* ModuleManager::page(const QString& id) const {
    auto it = bind_.find(id.toStdString());
    return it == bind_.end() ? nullptr : it->second.page.data();
}

QString ModuleManager::idOfWidget(const QWidget* w) const {
    for (const QWidget* c = w; c; c = c->parentWidget())
        for (const auto& [id, b] : bind_)
            if (b.page == c) return QString::fromStdString(id);
    return QString();
}

bool ModuleManager::reachable(const QWidget* w) const {
    for (const QWidget* c = w; c; c = c->parentWidget())
        for (const auto& [id, b] : bind_)
            if (b.page == c && !ws_.effective(id)) return false;
    return true;
}

VisibilityMap ModuleManager::effectiveMap() const {
    VisibilityMap m;
    for (const ModuleInfo& mi : ws_.modules()) m[mi.id] = ws_.effective(mi.id);
    return m;
}

// If the entry is the selected tab of its area and is now switched off, select the nearest
// sibling that is on: first the next one to the right (original order), then to the left.
void ModuleManager::moveSelectionOff(const std::string& id) {
    const Bind& b = bind_[id];
    if (!b.area || !b.page || b.area->currentWidget() != b.page || ws_.preference(id)) return;
    std::vector<std::string> sib = ws_.children(ws_.info(id).parent);
    int me = 0;
    for (int i = 0; i < (int)sib.size(); ++i) if (sib[i] == id) me = i;
    for (int d = 1; d < (int)sib.size(); ++d)
        for (int k : {me + d, me - d})
            if (k >= 0 && k < (int)sib.size() && ws_.preference(sib[k]) && bind_[sib[k]].page) {
                b.area->setCurrentWidget(bind_[sib[k]].page);
                return;
            }
}

// Pauses deterministic playback on the page (never resets it). PlaybackBar::pause and the hooks
// are no-ops when nothing is playing.
void ModuleManager::pauseEntry(const std::string& id) {
    Bind& b = bind_[id];
    if (b.pauseHook) b.pauseHook();
    if (b.page)
        for (PlaybackBar* bar : b.page->findChildren<PlaybackBar*>()) bar->pause();
}

void ModuleManager::applyToTabs(const VisibilityMap& before) {
    ++applies_;
    // 1. move the selection away from entries that are switched off (before hiding their tab, so
    //    Qt never has to pick an arbitrary neighbour)
    for (const ModuleInfo& m : ws_.modules()) moveSelectionOff(m.id);
    // 2. tab visibility follows each entry's own preference; a hidden parent hides its children
    //    implicitly, and their preferences come back unchanged when it is shown again
    for (const ModuleInfo& m : ws_.modules()) {
        const Bind& b = bind_[m.id];
        if (!b.area || !b.page) continue;
        int idx = b.area->indexOf(b.page);
        if (idx >= 0 && b.area->isTabVisible(idx) != ws_.preference(m.id)) b.area->setTabVisible(idx, ws_.preference(m.id));
    }
    // 3. lifecycle: pause what became effectively hidden
    for (const ModuleInfo& m : ws_.modules()) {
        auto it = before.find(m.id);
        if (it != before.end() && it->second && !ws_.effective(m.id)) pauseEntry(m.id);
    }
}

void ModuleManager::changedNow(const VisibilityMap& before) {
    applyToTabs(before);
    saveToStore();
    emit changed();
}

bool ModuleManager::setVisible(const QString& id, bool on, QString* why) {
    VisibilityMap before = effectiveMap();
    std::string w;
    if (!ws_.setVisible(id.toStdString(), on, &w)) {
        if (why) *why = QString::fromStdString(w);
        return false;
    }
    changedNow(before);
    return true;
}

void ModuleManager::showAll() {
    VisibilityMap before = effectiveMap();
    ws_.showAll();
    changedNow(before);
}

void ModuleManager::resetToDefaults() {
    VisibilityMap before = effectiveMap();
    ws_.resetToDefaults();
    changedNow(before);
}

bool ModuleManager::showCurrentOnly() {
    QString top = currentTopId();
    if (top.isEmpty()) return false;
    VisibilityMap before = effectiveMap();
    bool ok = ws_.showOnly(top.toStdString());
    changedNow(before);
    return ok;
}

void ModuleManager::applyMap(const VisibilityMap& m) {
    VisibilityMap before = effectiveMap();
    ws_.apply(m);
    changedNow(before);
}

// Walks down from the top-level area through the selected tabs.
QString ModuleManager::currentId() const {
    QString cur;
    std::string parent;
    for (int depth = 0; depth < 16; ++depth) {
        QString next;
        for (const std::string& c : ws_.children(parent)) {
            auto it = bind_.find(c);
            if (it != bind_.end() && it->second.area && it->second.area->currentWidget() == it->second.page && ws_.preference(c)) {
                next = QString::fromStdString(c);
                break;
            }
        }
        if (next.isEmpty()) break;
        cur = next;
        parent = next.toStdString();
    }
    return cur;
}

QString ModuleManager::currentTopId() const {
    QString c = currentId();
    return c.isEmpty() ? QString() : QString::fromStdString(ws_.topOf(c.toStdString()));
}

// Selects the entry and every enclosing entry; refuses entries that are not effectively visible.
bool ModuleManager::select(const QString& id) {
    std::string s = id.toStdString();
    if (!ws_.has(s) || !ws_.effective(s)) return false;
    for (std::string cur = s; !cur.empty(); cur = ws_.info(cur).parent) {
        const Bind& b = bind_[cur];
        if (b.area && b.page) b.area->setCurrentWidget(b.page);
    }
    emit changed();
    return true;
}

// ---------------------------------------------------------------- presets

QStringList ModuleManager::builtinPresetNames() const {
    QStringList v;
    for (const WorkspacePreset& p : builtinPresets()) v << QString::fromStdString(p.name);
    return v;
}

QString ModuleManager::builtinPresetDescription(const QString& name) const {
    for (const WorkspacePreset& p : builtinPresets())
        if (QString::fromStdString(p.name) == name) return QString::fromStdString(p.description);
    return QString();
}

QStringList ModuleManager::customPresetNames() const {
    QStringList v;
    for (const auto& c : custom_) v << c.first;
    return v;
}

bool ModuleManager::applyPreset(const QString& name) {
    for (const WorkspacePreset& p : builtinPresets())
        if (QString::fromStdString(p.name) == name) { applyMap(presetVisibility(ws_, p)); return true; }
    for (const auto& c : custom_)
        if (c.first == name) { applyMap(c.second); return true; }
    return false;
}

bool ModuleManager::saveCustomPreset(const QString& raw, QString* why) {
    QString name = raw.simplified();
    if (name.isEmpty()) { if (why) *why = "The preset needs a name."; return false; }
    if (builtinPresetNames().contains(name, Qt::CaseInsensitive)) { if (why) *why = "\"" + name + "\" is a built-in preset name."; return false; }
    if (name.size() > 60) { if (why) *why = "Use at most 60 characters."; return false; }
    for (auto& c : custom_)
        if (c.first.compare(name, Qt::CaseInsensitive) == 0) { c.second = ws_.snapshot(); saveToStore(); emit changed(); return true; }  // overwrite
    custom_.push_back({name, ws_.snapshot()});
    saveToStore();
    emit changed();
    return true;
}

bool ModuleManager::renameCustomPreset(const QString& from, const QString& raw, QString* why) {
    QString to = raw.simplified();
    if (to.isEmpty()) { if (why) *why = "The preset needs a name."; return false; }
    if (builtinPresetNames().contains(to, Qt::CaseInsensitive)) { if (why) *why = "\"" + to + "\" is a built-in preset name."; return false; }
    for (const auto& c : custom_)
        if (c.first != from && c.first.compare(to, Qt::CaseInsensitive) == 0) { if (why) *why = "A preset named \"" + to + "\" already exists."; return false; }
    for (auto& c : custom_)
        if (c.first == from) { c.first = to; saveToStore(); emit changed(); return true; }
    if (why) *why = "No custom preset named \"" + from + "\".";
    return false;
}

bool ModuleManager::deleteCustomPreset(const QString& name) {
    for (size_t i = 0; i < custom_.size(); ++i)
        if (custom_[i].first == name) {
            custom_.erase(custom_.begin() + i);
            saveToStore();
            emit changed();
            return true;
        }
    return false;
}

// ---------------------------------------------------------------- settings

void ModuleManager::setStore(const QString& organisation) { store_ = organisation; }

// Layout (version 1):  modules/version = 1
//                      modules/v1/visible/<id> = true|false
//                      modules/v1/current = <id>
//                      modules/v1/presets/<n>/name, modules/v1/presets/<n>/state = "id=1,id=0"
// Unknown ids are ignored (removed / renamed modules), missing ids get their default, values that
// are not booleans are reported and replaced by the default; the result is sanitized.
void ModuleManager::load(QSettings& s) {
    warnings_.clear();
    VisibilityMap before = effectiveMap();
    VisibilityMap m;
    QVariant ver = s.value("modules/version");
    bool verOk = false;
    int v = ver.toInt(&verOk);
    if (ver.isValid() && (!verOk || v != kSettingsVersion)) {
        warnings_ << QString("workspace settings version '%1' is not supported - defaults used").arg(ver.toString());
    } else {
        s.beginGroup("modules/v1/visible");
        for (const QString& key : s.childKeys()) {
            std::string id = key.toStdString();
            if (!ws_.has(id)) continue;                        // module no longer exists
            QString val = s.value(key).toString().trimmed().toLower();
            if (val == "true" || val == "1") m[id] = true;
            else if (val == "false" || val == "0") m[id] = false;
            else warnings_ << QString("bad value '%1' for %2 - default used").arg(val, key);
        }
        s.endGroup();
        custom_.clear();
        int n = s.beginReadArray("modules/v1/presets");
        for (int i = 0; i < n && i < 100; ++i) {
            s.setArrayIndex(i);
            QString name = s.value("name").toString().simplified();
            VisibilityMap pm;
            // a hand-edited INI value with commas is read back as a string list: join it again
            QVariant st = s.value("state");
            QString text = st.typeId() == QMetaType::QStringList ? st.toStringList().join(',') : st.toString();
            int bad = parseVisibility(text.toStdString(), pm);
            if (bad) warnings_ << QString("preset '%1': %2 malformed entries skipped").arg(name).arg(bad);
            if (name.isEmpty() || customPresetNames().contains(name) || builtinPresetNames().contains(name)) continue;
            custom_.push_back({name, pm});
        }
        s.endArray();
    }
    ws_.apply(m);                                              // missing -> defaults, then sanitized
    VisibilityMap got = ws_.snapshot();
    for (const auto& [id, on] : m)
        if (got[id] != on) warnings_ << QString("%1 had to be shown again (no visible module would remain)").arg(QString::fromStdString(id));
    changedNow(before);
}

void ModuleManager::save(QSettings& s) const {
    s.setValue("modules/version", kSettingsVersion);
    s.remove("modules/v1/visible");
    for (const ModuleInfo& m : ws_.modules())
        s.setValue(QString("modules/v1/visible/%1").arg(QString::fromStdString(m.id)), ws_.preference(m.id));
    s.remove("modules/v1/presets");
    s.beginWriteArray("modules/v1/presets", (int)custom_.size());
    for (int i = 0; i < (int)custom_.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("name", custom_[i].first);
        s.setValue("state", QString::fromStdString(serializeVisibility(custom_[i].second)));
    }
    s.endArray();
}

void ModuleManager::loadFromStore() {
    if (store_.isEmpty()) return;
    QSettings s(store_, "OS-Sim");
    load(s);
}

void ModuleManager::saveToStore() const {
    if (store_.isEmpty()) return;
    QSettings s(store_, "OS-Sim");
    save(s);
}

void ModuleManager::saveCurrentToStore() const {
    if (store_.isEmpty()) return;
    QString c = currentId();
    if (!c.isEmpty()) QSettings(store_, "OS-Sim").setValue("modules/v1/current", c);
}

void ModuleManager::restoreCurrentFromStore() {
    if (store_.isEmpty()) return;
    QString c = QSettings(store_, "OS-Sim").value("modules/v1/current").toString();
    if (!c.isEmpty()) select(c);                               // ignored when unknown or hidden
}

QString ModuleManager::report() const {
    QString out;
    std::function<void(const std::string&, int)> walk = [&](const std::string& parent, int depth) {
        for (const std::string& id : ws_.children(parent)) {
            const ModuleInfo& m = ws_.info(id);
            QString line = QString(depth * 2, ' ') + (ws_.preference(id) ? "[x] " : "[ ] ") + QString::fromStdString(m.name);
            std::vector<std::string> kids = ws_.children(id);
            if (!kids.empty()) line += QString(" (%1/%2 shown)").arg(ws_.visibleChildren(id)).arg(kids.size());
            if (ws_.preference(id) && !ws_.effective(id)) line += " (parent hidden)";
            out += line.leftJustified(46) + " " + QString::fromStdString(id) + "\n";
            walk(id, depth + 1);
        }
    };
    walk("", 0);
    return out;
}
