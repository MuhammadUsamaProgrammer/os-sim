// workspace.h - Module Visibility Manager: the pure, Qt-free model behind the Modules menu.
//
// This is NOT an OS algorithm. It only decides which navigation entries (parent tabs and their
// child views) are shown, so the GUI can be customised for a lecture or a viva. The model is kept
// in core/ (pure C++17, deterministic) so its rules are unit-tested without a display; the GUI
// adapter (gui/ModuleManager.*) binds it to the real QTabWidgets.
//
// Terms:
//   preference  - what the user chose for this entry (checkbox state). Kept even while a parent
//                 is hidden, so hiding and restoring Memory does not change its children.
//   effective   - preference of the entry AND of every ancestor: whether it can be seen.
// Rules (enforced by setVisible / sanitize, never left broken):
//   - at least one top-level module stays visible;
//   - a visible parent that has children keeps at least one visible child (no empty workspace).
// Entries are addressed by stable identifiers ("memory.heap"), never by tab indexes or names.
#pragma once
#include <map>
#include <string>
#include <vector>

struct ModuleInfo {
    std::string id;              // stable unique identifier, e.g. "scheduling.realtime"
    std::string name;            // display name
    std::string parent;          // parent id, empty for a top-level module
    int order = 0;               // original navigation order among its siblings
    bool defaultVisible = true;  // documented default
};

using VisibilityMap = std::map<std::string, bool>;   // id -> preference

class Workspace {
public:
    // Registers a module. The parent (if any) must already be registered; ids must be unique and
    // non-empty and may not contain '=' ',' or whitespace. Throws std::invalid_argument otherwise.
    // Any depth is allowed (a child can itself have children).
    void add(const ModuleInfo& m);

    const std::vector<ModuleInfo>& modules() const { return mods_; }
    int indexOf(const std::string& id) const;                  // -1 when unknown
    bool has(const std::string& id) const { return indexOf(id) >= 0; }
    const ModuleInfo& info(const std::string& id) const;       // throws for an unknown id
    std::vector<std::string> children(const std::string& id) const;   // in original order ("" = top level)
    std::vector<std::string> topLevel() const { return children(""); }
    std::string topOf(const std::string& id) const;            // top-level ancestor (or itself)

    bool preference(const std::string& id) const;              // user's checkbox state
    bool effective(const std::string& id) const;               // preference of it and all ancestors
    int visibleChildren(const std::string& id) const;          // children whose preference is on
    int effectiveCount() const;                                // modules that can be seen now

    // Changes one preference. Returns false (and changes nothing) when the change would break a
    // rule; *why then explains it in one sentence.
    bool setVisible(const std::string& id, bool on, std::string* why = nullptr);
    bool canHide(const std::string& id, std::string* why = nullptr) const;

    // Bulk actions (preferences only - simulations are not touched).
    void showAll();
    void resetToDefaults();
    bool showOnly(const std::string& topId);                   // hides the other top-level modules
    // Applies a full or partial map: known ids take the given value, ids missing from the map
    // take their default, unknown ids are ignored; the result is then sanitized.
    void apply(const VisibilityMap& m);
    VisibilityMap snapshot() const;                            // every registered id
    // Repairs a state that breaks a rule (used after loading settings or a preset).
    // Returns true when something had to be changed.
    bool sanitize();

private:
    std::vector<ModuleInfo> mods_;
    std::vector<bool> pref_;
};

// "id=1,id=0" text form (used for settings and saved presets). parse() is tolerant: malformed
// entries are skipped and reported through the return value (number of entries rejected).
std::string serializeVisibility(const VisibilityMap& m);
int parseVisibility(const std::string& text, VisibilityMap& out);

// A built-in workspace preset: entries to show. Listing a parent shows it with all of its
// children (unless one of its children is also listed, then only the listed ones); listing a
// child shows that child and its ancestors. Everything else is hidden.
struct WorkspacePreset {
    std::string name;
    std::string description;
    std::vector<std::string> show;
};
std::vector<WorkspacePreset> builtinPresets();
// Turns a preset into a full visibility map for the registered modules (unknown ids ignored).
VisibilityMap presetVisibility(const Workspace& ws, const WorkspacePreset& p);
