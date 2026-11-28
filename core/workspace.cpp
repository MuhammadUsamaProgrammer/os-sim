// workspace.cpp - Module Visibility Manager model (see workspace.h). Pure C++17, no Qt.
#include "workspace.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

// Registers one module (checks the id and that the parent exists).
void Workspace::add(const ModuleInfo& m) {
    if (m.id.empty()) throw std::invalid_argument("module id must not be empty");
    for (char c : m.id)
        if (c == '=' || c == ',' || std::isspace((unsigned char)c)) throw std::invalid_argument("bad character in module id: " + m.id);
    if (has(m.id)) throw std::invalid_argument("duplicate module id: " + m.id);
    if (!m.parent.empty() && !has(m.parent)) throw std::invalid_argument("unknown parent '" + m.parent + "' for " + m.id);
    mods_.push_back(m);
    pref_.push_back(m.defaultVisible);
}

int Workspace::indexOf(const std::string& id) const {
    for (size_t i = 0; i < mods_.size(); ++i) if (mods_[i].id == id) return (int)i;
    return -1;
}

const ModuleInfo& Workspace::info(const std::string& id) const {
    int i = indexOf(id);
    if (i < 0) throw std::invalid_argument("unknown module: " + id);
    return mods_[i];
}

// Children of a module (or the top-level modules for ""), sorted by their original order.
std::vector<std::string> Workspace::children(const std::string& id) const {
    std::vector<const ModuleInfo*> v;
    for (const ModuleInfo& m : mods_) if (m.parent == id) v.push_back(&m);
    std::stable_sort(v.begin(), v.end(), [](const ModuleInfo* a, const ModuleInfo* b) { return a->order < b->order; });
    std::vector<std::string> out;
    for (const ModuleInfo* m : v) out.push_back(m->id);
    return out;
}

std::string Workspace::topOf(const std::string& id) const {
    std::string cur = id;
    for (int guard = 0; guard < 64; ++guard) {
        const ModuleInfo& m = info(cur);
        if (m.parent.empty()) return cur;
        cur = m.parent;
    }
    return cur;
}

bool Workspace::preference(const std::string& id) const {
    int i = indexOf(id);
    return i >= 0 && pref_[i];
}

bool Workspace::effective(const std::string& id) const {
    int i = indexOf(id);
    if (i < 0) return false;
    for (int guard = 0; i >= 0 && guard < 64; ++guard) {
        if (!pref_[i]) return false;
        i = mods_[i].parent.empty() ? -1 : indexOf(mods_[i].parent);
    }
    return true;
}

int Workspace::visibleChildren(const std::string& id) const {
    int n = 0;
    for (const std::string& c : children(id)) if (preference(c)) ++n;
    return n;
}

int Workspace::effectiveCount() const {
    int n = 0;
    for (const ModuleInfo& m : mods_) if (effective(m.id)) ++n;
    return n;
}

// A module may be hidden unless it is the last visible entry among its siblings: the last
// top-level module (the workspace would be empty) or the last child of a parent (the parent
// would show nothing).
bool Workspace::canHide(const std::string& id, std::string* why) const {
    int i = indexOf(id);
    if (i < 0) { if (why) *why = "unknown module " + id; return false; }
    if (!pref_[i]) return true;                                  // already hidden
    const std::string& parent = mods_[i].parent;
    if (visibleChildren(parent) <= 1) {
        if (why) *why = parent.empty()
            ? "At least one main module must stay visible."
            : "\"" + mods_[i].name + "\" is the last visible part of \"" + info(parent).name +
              "\". Hide \"" + info(parent).name + "\" itself instead, or show another part first.";
        return false;
    }
    return true;
}

bool Workspace::setVisible(const std::string& id, bool on, std::string* why) {
    int i = indexOf(id);
    if (i < 0) { if (why) *why = "unknown module " + id; return false; }
    if (pref_[i] == on) return true;
    if (!on && !canHide(id, why)) return false;
    pref_[i] = on;
    return true;
}

void Workspace::showAll() { std::fill(pref_.begin(), pref_.end(), true); }

void Workspace::resetToDefaults() {
    for (size_t i = 0; i < mods_.size(); ++i) pref_[i] = mods_[i].defaultVisible;
    sanitize();
}

// Shows topId (and keeps its children's preferences) and hides every other top-level module.
bool Workspace::showOnly(const std::string& topId) {
    int i = indexOf(topId);
    if (i < 0 || !mods_[i].parent.empty()) return false;
    for (const std::string& t : topLevel()) pref_[indexOf(t)] = (t == topId);
    sanitize();                                                  // e.g. all of its children were hidden
    return true;
}

void Workspace::apply(const VisibilityMap& m) {
    for (size_t i = 0; i < mods_.size(); ++i) {
        auto it = m.find(mods_[i].id);
        pref_[i] = it == m.end() ? mods_[i].defaultVisible : it->second;
    }
    sanitize();
}

VisibilityMap Workspace::snapshot() const {
    VisibilityMap m;
    for (size_t i = 0; i < mods_.size(); ++i) m[mods_[i].id] = pref_[i];
    return m;
}

// Repairs the two rules: no visible top-level module -> show the first one that is visible by
// default (else the first); a visible parent without a visible child -> show its first child that
// is visible by default (else its first child). Parents are repaired top-down, so a newly shown
// child that is itself a parent is repaired too.
bool Workspace::sanitize() {
    bool changed = false;
    auto repair = [&](const std::string& parent) {
        std::vector<std::string> kids = children(parent);
        if (kids.empty() || visibleChildren(parent) > 0) return;
        std::string pick = kids.front();
        for (const std::string& k : kids) if (info(k).defaultVisible) { pick = k; break; }
        pref_[indexOf(pick)] = true;
        changed = true;
    };
    repair("");
    // breadth-first over the tree, so the order of registration does not matter
    std::vector<std::string> queue = topLevel();
    for (size_t q = 0; q < queue.size(); ++q) {
        if (preference(queue[q])) repair(queue[q]);
        for (const std::string& k : children(queue[q])) queue.push_back(k);
    }
    return changed;
}

std::string serializeVisibility(const VisibilityMap& m) {
    std::string s;
    for (const auto& [id, on] : m) {
        if (!s.empty()) s += ',';
        s += id + '=' + (on ? '1' : '0');
    }
    return s;
}

// Parses "a=1,b=0". Accepts 1/0/true/false, ignores surrounding spaces and empty entries;
// anything else is counted as rejected and skipped (never throws).
int parseVisibility(const std::string& text, VisibilityMap& out) {
    int bad = 0;
    size_t start = 0;
    auto trim = [](std::string x) {
        while (!x.empty() && std::isspace((unsigned char)x.front())) x.erase(x.begin());
        while (!x.empty() && std::isspace((unsigned char)x.back())) x.pop_back();
        return x;
    };
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        std::string item = trim(text.substr(start, end - start));
        start = end + 1;
        if (item.empty()) continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos || eq == 0) { ++bad; continue; }
        std::string id = trim(item.substr(0, eq)), v = trim(item.substr(eq + 1));
        if (id.empty() || id.find_first_of(" \t") != std::string::npos) { ++bad; continue; }
        if (v == "1" || v == "true") out[id] = true;
        else if (v == "0" || v == "false") out[id] = false;
        else ++bad;
    }
    return bad;
}

// Built-in presets (membership uses the identifiers registered by gui/MainWindow.cpp; see
// docs/WORKSPACE.md for the full hierarchy). Ids that are not registered are ignored.
std::vector<WorkspacePreset> builtinPresets() {
    return {
        {"Complete OS-Sim", "Every module and every child view.", {}},
        {"CPU & Process Management", "Scheduling, Processes and Threads.",
         {"scheduling", "processes", "threads"}},
        {"Memory & Storage", "Memory, Disk scheduling and Storage & Files.",
         {"memory", "disk", "storage"}},
        {"Synchronization & Deadlocks", "Synchronization, Deadlock, Threads and IPC.",
         {"synchronization", "deadlock", "threads", "ipc"}},
        {"Linux Practical Lab", "Pages with Live Linux demonstrations: Process Lifecycle, System Monitor, Job Control, "
                                "Threads, IPC, Synchronization (real POSIX demo) and the File Explorer (real syscalls).",
         {"processes", "threads", "ipc", "synchronization", "storage.explorer"}},
    };
}

VisibilityMap presetVisibility(const Workspace& ws, const WorkspacePreset& p) {
    VisibilityMap m;
    if (p.show.empty()) {                                        // "everything"
        for (const ModuleInfo& mi : ws.modules()) m[mi.id] = true;
        return m;
    }
    for (const ModuleInfo& mi : ws.modules()) m[mi.id] = false;
    std::set<std::string> listed;
    for (const std::string& id : p.show) if (ws.has(id)) listed.insert(id);
    auto isAncestor = [&](const std::string& anc, const std::string& id) {
        for (std::string cur = ws.info(id).parent; !cur.empty(); cur = ws.info(cur).parent)
            if (cur == anc) return true;
        return false;
    };
    for (const std::string& id : listed) {
        for (std::string cur = id; !cur.empty(); cur = ws.info(cur).parent) m[cur] = true;   // it and its ancestors
        bool childListed = false;
        for (const std::string& other : listed) if (other != id && isAncestor(id, other)) childListed = true;
        if (!childListed)                                        // a listed parent shows all of its descendants
            for (const ModuleInfo& mi : ws.modules()) if (isAncestor(id, mi.id)) m[mi.id] = true;
    }
    return m;
}
