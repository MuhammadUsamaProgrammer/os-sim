// test_workspace.cpp - Module Visibility Manager model (core/workspace.h): preferences vs effective
// visibility, the "never empty" rules, bulk actions, presets, text form and bad settings.
#include <cstdio>
#include "check.h"
#include "workspace.h"

// A tree shaped like the real OS-Sim navigation (plus one 3-level branch for depth).
static Workspace sample() {
    Workspace w;
    int o = 0;
    w.add({"scheduling", "Scheduling", "", o++});
    w.add({"scheduling.cpu", "CPU Scheduling", "scheduling", 0});
    w.add({"scheduling.mlq", "Multilevel Queues", "scheduling", 1});
    w.add({"scheduling.realtime", "Real-Time Scheduling", "scheduling", 2});
    w.add({"processes", "Processes", "", o++});
    w.add({"processes.lifecycle", "Process Lifecycle", "processes", 0});
    w.add({"processes.monitor", "System Monitor", "processes", 1});
    w.add({"processes.jobs", "Job Control", "processes", 2});
    w.add({"memory", "Memory", "", o++});
    w.add({"memory.replacement", "Page replacement", "memory", 0});
    w.add({"memory.heap", "Heap Laboratory", "memory", 1});
    w.add({"memory.heap.buddy", "Buddy view", "memory.heap", 0, false});   // depth 3, hidden by default
    w.add({"memory.heap.freelist", "Free list view", "memory.heap", 1});
    w.add({"deadlock", "Deadlock", "", o++});
    w.add({"threads", "Threads", "", o++});
    w.add({"ipc", "IPC", "", o++});
    w.add({"synchronization", "Synchronization", "", o++});
    w.add({"disk", "Disk", "", o++});
    w.add({"storage", "Storage & Files", "", o++});
    w.add({"storage.explorer", "File Explorer", "storage", 0});
    w.add({"storage.access", "Access Control", "storage", 1});
    return w;
}

static void testRegistry() {
    Workspace w = sample();
    CHECK(w.modules().size() == 21);
    CHECK(w.topLevel().size() == 9 && w.topLevel()[0] == "scheduling" && w.topLevel()[8] == "storage");
    CHECK(w.children("memory").size() == 2 && w.children("memory.heap").size() == 2);
    CHECK(w.topOf("memory.heap.buddy") == "memory" && w.topOf("disk") == "disk");
    CHECK(!w.preference("memory.heap.buddy") && w.preference("memory.heap.freelist"));     // documented default
    CHECK(w.indexOf("nope") == -1 && !w.effective("nope") && !w.preference("nope"));
    CHECK_THROWS(w.add({"disk", "dup", "", 99}), std::invalid_argument);
    CHECK_THROWS(w.add({"x", "orphan", "missing", 0}), std::invalid_argument);
    CHECK_THROWS(w.add({"", "empty", "", 0}), std::invalid_argument);
    CHECK_THROWS(w.add({"a b", "space", "", 0}), std::invalid_argument);
    CHECK_THROWS(w.add({"a=b", "eq", "", 0}), std::invalid_argument);
    CHECK_THROWS(w.info("nope"), std::invalid_argument);
    // children are returned in original order even when registered out of order
    Workspace r;
    r.add({"p", "P", "", 0});
    r.add({"p.b", "B", "p", 2});
    r.add({"p.a", "A", "p", 1});
    CHECK(r.children("p").size() == 2 && r.children("p")[0] == "p.a");
}

static void testParentChild() {
    Workspace w = sample();
    // hide one child: siblings untouched
    CHECK(w.setVisible("processes.monitor", false));
    CHECK(!w.effective("processes.monitor") && w.effective("processes.lifecycle") && w.effective("processes.jobs"));
    CHECK(w.visibleChildren("processes") == 2);
    // hide the parent: children keep their preferences, but are not effective
    CHECK(w.setVisible("processes", false));
    CHECK(!w.effective("processes") && !w.effective("processes.lifecycle"));
    CHECK(w.preference("processes.lifecycle") && !w.preference("processes.monitor") && w.preference("processes.jobs"));
    // a child can be changed while its parent is hidden (stored for later)
    CHECK(w.setVisible("processes.jobs", false));
    // restore the parent: exactly the stored child preferences come back
    CHECK(w.setVisible("processes", true));
    CHECK(w.effective("processes.lifecycle") && !w.effective("processes.monitor") && !w.effective("processes.jobs"));
    // depth 3: effective needs every ancestor
    CHECK(w.setVisible("memory.heap.buddy", true));
    CHECK(w.effective("memory.heap.buddy"));
    CHECK(w.setVisible("memory", false));
    CHECK(!w.effective("memory.heap.buddy") && w.preference("memory.heap.buddy"));
    CHECK(w.setVisible("memory", true) && w.effective("memory.heap.buddy"));
    // idempotent
    CHECK(w.setVisible("disk", true) && w.preference("disk"));
}

static void testRules() {
    Workspace w = sample();
    std::string why;
    // last visible child of a visible parent cannot be hidden
    CHECK(w.setVisible("storage.explorer", false));
    CHECK(!w.canHide("storage.access", &why) && why.find("last visible") != std::string::npos);
    CHECK(!w.setVisible("storage.access", false, &why) && w.preference("storage.access"));
    // the last visible top-level module cannot be hidden
    for (const std::string& t : w.topLevel()) if (t != "deadlock") CHECK(w.setVisible(t, false));
    CHECK(w.effectiveCount() == 1 && w.effective("deadlock"));
    CHECK(!w.setVisible("deadlock", false, &why) && w.preference("deadlock"));
    CHECK(why.find("At least one main module") != std::string::npos);
    // already hidden can always be "hidden" again; unknown id rejected
    CHECK(w.canHide("disk") && !w.setVisible("unknown", true, &why));
}

static void testBulk() {
    Workspace w = sample();
    w.setVisible("memory.heap", false);
    w.setVisible("processes", false);
    // show current only: other top-level hidden, child preferences of memory kept
    CHECK(w.showOnly("memory"));
    CHECK(w.effectiveCount() == 2);                                   // memory + replacement
    CHECK(w.effective("memory") && !w.effective("scheduling") && !w.preference("memory.heap"));
    CHECK(!w.showOnly("memory.heap") && !w.showOnly("nope"));         // only top-level ids
    // show all: every preference on
    w.showAll();
    CHECK(w.effectiveCount() == (int)w.modules().size());
    // reset: documented defaults (buddy is hidden by default)
    w.resetToDefaults();
    CHECK(w.effectiveCount() == (int)w.modules().size() - 1 && !w.preference("memory.heap.buddy"));
    // snapshot / apply round trip
    w.setVisible("ipc", false);
    w.setVisible("scheduling.mlq", false);
    VisibilityMap snap = w.snapshot();
    CHECK(snap.size() == w.modules().size());
    w.showAll();
    w.apply(snap);
    CHECK(!w.preference("ipc") && !w.preference("scheduling.mlq") && w.preference("scheduling.cpu"));
    // apply: missing ids -> default, unknown ids ignored
    w.apply({{"ipc", false}, {"removed.module", false}});
    CHECK(!w.preference("ipc") && w.preference("scheduling.mlq") && !w.preference("memory.heap.buddy"));
}

static void testSanitize() {
    Workspace w = sample();
    // a stored state that hides everything is repaired
    VisibilityMap allOff;
    for (const ModuleInfo& m : w.modules()) allOff[m.id] = false;
    w.apply(allOff);
    CHECK(w.effectiveCount() >= 2 && w.effective("scheduling") && w.effective("scheduling.cpu"));
    // a visible parent whose children are all hidden gets its first default-visible child back
    Workspace v = sample();
    v.apply({{"memory.replacement", false}, {"memory.heap", false}});
    CHECK(v.effective("memory.replacement") && !v.preference("memory.heap"));
    // nested: heap visible with both its children off -> the default-visible one (freelist) returns
    Workspace d = sample();
    d.apply({{"memory.heap.buddy", false}, {"memory.heap.freelist", false}});
    CHECK(d.preference("memory.heap.freelist") && !d.preference("memory.heap.buddy"));
    CHECK(!d.sanitize());                                              // valid state: nothing changes
}

static void testText() {
    VisibilityMap m{{"a", true}, {"b.c", false}};
    std::string s = serializeVisibility(m);
    CHECK(s == "a=1,b.c=0");
    VisibilityMap back;
    CHECK(parseVisibility(s, back) == 0 && back == m);
    // tolerant parser: spaces, true/false, empty entries; malformed entries rejected and counted
    VisibilityMap t;
    int bad = parseVisibility(" x = true ,, y=false, junk, =1, z=maybe, w x=1,", t);
    CHECK(bad == 4 && t.size() == 2 && t["x"] && !t["y"]);
    VisibilityMap e;
    CHECK(parseVisibility("", e) == 0 && e.empty());
    // corrupted settings text applied to a workspace: safe result
    Workspace w = sample();
    VisibilityMap garbage;
    parseVisibility("\x01\x02=1,scheduling=0,processes=zz,storage.access=0,storage.explorer=0", garbage);
    w.apply(garbage);
    CHECK(!w.preference("scheduling") && w.preference("processes"));
    CHECK(w.effective("storage") && w.visibleChildren("storage") == 1);
}

static void testPresets() {
    Workspace w = sample();
    std::vector<WorkspacePreset> ps = builtinPresets();
    CHECK(ps.size() == 5 && ps[0].name == "Complete OS-Sim");
    // complete: everything (also the default-hidden entry)
    w.apply(presetVisibility(w, ps[0]));
    CHECK(w.effectiveCount() == (int)w.modules().size());
    // CPU & Process Management: the three parents with all children, nothing else
    w.apply(presetVisibility(w, ps[1]));
    CHECK(w.effective("scheduling.realtime") && w.effective("processes.jobs") && w.effective("threads"));
    CHECK(!w.effective("memory") && !w.effective("disk") && !w.effective("storage") && !w.effective("deadlock"));
    // Memory & Storage
    w.apply(presetVisibility(w, ps[2]));
    CHECK(w.effective("memory.heap.buddy") && w.effective("storage.access") && w.effective("disk") && !w.effective("scheduling"));
    // Linux lab: a listed child shows its ancestors, but not its siblings
    w.apply(presetVisibility(w, ps[4]));
    CHECK(w.effective("storage") && w.effective("storage.explorer") && !w.preference("storage.access"));
    CHECK(w.effective("processes.monitor") && w.effective("ipc") && !w.effective("memory"));
    // unknown ids in a preset are ignored; an empty result is sanitized
    WorkspacePreset bogus{"x", "", {"no.such.module"}};
    w.apply(presetVisibility(w, bogus));
    CHECK(w.effectiveCount() >= 1);
}

int main() {
    testRegistry();
    testParentChild();
    testRules();
    testBulk();
    testSanitize();
    testText();
    testPresets();
    return report("test_workspace");
}
