// SPDX-License-Identifier: GPL-3.0-only
// Test host for the Rust menu (launcher-rs/crates/scol-menu) against sco-core's real widgets view.
//
// It starts sco.widgets with synthetic panels, loads sc_offline_menu.dll, hands it the view table
// and client the way src/menu_rust.cpp does, then plays a click-through on a worker thread with
// real window input (toggle, slider, combo, text, button, list, paging). The main thread is the
// game thread: it drains dispatch_events like Menu_GameThreadFrame. Every click must arrive in the
// plugin callback with the right kind, value and canonical tag. Screenshots go to the folder given.
//
//   rust_menu_host <path to sc_offline_menu.dll> [screenshot folder] [panel dump file]
//
// With a panel dump (the file SCO_PANEL_DUMP makes the plugin workflow tests write, see
// tools/panel_dump.h; the repository keeps one in launcher-rs/tests/menu/panels.txt) every panel of
// the dump becomes a page and every control on it is clicked: toggle, slider, combo, list row, table
// and list paging, text and button, each checked for the kind, value and tag the plugin callback sees.
//
// Synthetic only: no game, no Star Citizen process. Not run in game.
#include "sco/widgets.h"
#include "sco/host.h"
#include "src/widgets/view_internal.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace W = sco::widgets;
namespace V = sco::widgets::viewimpl;
using sco::Result;

// ---- the product-private start block (mirror of StartV1 in scol-menu/src/lib.rs) ----
struct StartV1 {
    uint32_t size, abi;
    void* game_window;
    const sco_widgets_view_v1* view;
    sco_view_client* client;
    void (*log)(const char*);
    void (*nudge_game)();
    const char* shot_dir;
    uint32_t flags, hotkey;
    const char* title;
    const char* view_path;
    const char* size_path;
    void (*open_classic)();
    void* (*game_window_fn)();
    int32_t (*asop_available)();
    int32_t (*classic_visible)();
};
static const uint32_t kFlagVisible = 1, kFlagTest = 2;

struct MenuApi {
    int32_t (*start)(const StartV1*) = nullptr;
    void (*request_stop)() = nullptr;
    int32_t (*drain)(uint32_t) = nullptr;
    int32_t (*test_step)(const char*, char*, uint32_t) = nullptr;
    int32_t (*is_typing)() = nullptr;
    uint32_t (*abi)() = nullptr;
    int32_t (*is_broken)() = nullptr;
    const char* (*version)() = nullptr;
};

static std::atomic<int> passed{0}, failed{0};
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

// ---- a panel builder over the full 1.3 node layout (the shape tests/test_widgets_view.cpp uses) ----
struct Spec {
    uint32_t kind = 0, id = 0, parent = 0, flags = 0;
    std::string label, tag, text, tooltip;
    double value = 0, minimum = 0, maximum = 0;
    int32_t index = 0;
    std::vector<std::string> items;
    uint32_t columns = 0, visibleRows = 0, firstRow = 0, severity = 0, presentation = 0;
};
static Spec Mk(uint32_t kind, uint32_t id, uint32_t parent, const std::string& label, const std::string& tag = "") {
    Spec s; s.kind = kind; s.id = id; s.parent = parent; s.label = label; s.tag = tag; return s;
}
struct Built {
    std::vector<sco_widget_node_1_3> nodes;
    std::vector<std::vector<const char*>> pointers;
};
static Built Build(const std::vector<Spec>& specs) {
    Built b;
    b.nodes.resize(specs.size());
    b.pointers.resize(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const Spec& s = specs[i];
        auto& x = b.nodes[i];
        x = sco_widget_node_1_3{};
        sco_widget_node& n = x.base.base.base;
        n.size = sizeof(sco_widget_node_1_3);
        n.kind = s.kind; n.id = s.id; n.parent = s.parent; n.flags = s.flags; n.index = s.index;
        n.label = s.label.c_str();
        n.text = s.kind == SCO_WIDGET_TEXT_INPUT ? s.text.c_str() : nullptr;
        n.value = s.value; n.minimum = s.minimum; n.maximum = s.maximum;
        n.columns = s.columns;
        for (const auto& item : s.items) b.pointers[i].push_back(item.c_str());
        n.items = b.pointers[i].empty() ? nullptr : b.pointers[i].data();
        n.item_count = static_cast<uint32_t>(b.pointers[i].size());
        x.base.base.tooltip = s.tooltip.empty() ? nullptr : s.tooltip.c_str();
        x.base.base.severity = s.severity;
        x.base.base.first_row = s.firstRow;
        x.base.base.visible_rows = s.visibleRows;
        x.base.tag = s.tag.empty() ? nullptr : s.tag.c_str();
        x.presentation = s.presentation;
    }
    return b;
}

static const sco_widgets_v1* table = nullptr;
static sco_plugin* owner = nullptr;
static const sco_widgets_view_v1* view = nullptr;
static sco_view_client* client = nullptr;

struct Got { std::string panel, tag, text; uint32_t id = 0, kind = 0; int32_t index = 0, row = -1; double value = 0; };
static std::mutex gotLock;
static std::vector<Got> got;
static void OnEvent(const char* panel, const sco_widget_event* event, void*) {
    Got g;
    g.panel = panel; g.id = event->node_id; g.kind = event->kind; g.index = event->index; g.value = event->value;
    g.text = event->text ? event->text : "";
    if (event->size >= sizeof(sco_widget_event_1_1)) g.row = reinterpret_cast<const sco_widget_event_1_1*>(event)->row;
    if (event->size >= sizeof(sco_widget_event_1_4)) {
        const char* tag = reinterpret_cast<const sco_widget_event_1_4*>(event)->tag;
        g.tag = tag ? tag : "";
    }
    std::lock_guard<std::mutex> hold(gotLock);
    got.push_back(g);
}
static std::vector<Got> Snapshot() { std::lock_guard<std::mutex> hold(gotLock); return got; }

static std::vector<Spec> Controls(uint32_t shift) {
    std::vector<Spec> n;
    n.push_back(Mk(SCO_WIDGET_COLUMN, 1, 0, "Flight"));
    n.push_back(Mk(SCO_WIDGET_LABEL, 2, 1, "Tune how the ship handles. Changes apply at once."));
    { Spec s = Mk(SCO_WIDGET_TOGGLE, 3, 1, "Flight assist", "flight.assist"); s.value = 1; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_SLIDER, 4, 1, "Thruster gain", "flight.gain"); s.maximum = 2; s.value = 0.5; s.tooltip = "Scales every thruster."; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_SLIDER, 5, 1, "Seats", "flight.seats"); s.minimum = 1; s.maximum = 10; s.value = 3; s.presentation = SCO_WIDGET_NUMERIC_INTEGER; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_SLIDER, 6, 1, "Mix", "flight.mix"); s.minimum = 20; s.maximum = 95; s.value = 60; s.presentation = SCO_WIDGET_NUMERIC_INTEGER_PERCENT; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_COMBO, 7, 1, "Mode", "flight.mode"); s.items = {"Arcade", "Simulation", "Hover"}; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_TEXT_INPUT, 8, 1, "Callsign", "flight.name"); s.text = "Rook"; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_TEXT_INPUT, 9, 1, "Access code", "flight.code"); s.text = "hunter2"; s.flags = SCO_WIDGET_PASSWORD; n.push_back(s); }
    n.push_back(Mk(SCO_WIDGET_ROW, 10, 1, ""));
    n.push_back(Mk(SCO_WIDGET_BUTTON, 11, 10, "Reset to defaults", "flight.reset"));
    { Spec s = Mk(SCO_WIDGET_BUTTON, 12, 10, "Calibrate", "flight.calibrate"); s.flags = SCO_WIDGET_DISABLED; s.tooltip = "Not available in this build."; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_SLIDER, 13, 1, "Locked value", "flight.locked"); s.flags = SCO_WIDGET_DISABLED; s.maximum = 10; s.value = 4; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_LABEL, 20, 0, "Info lines show a prefix as well as a colour."); s.severity = SCO_WIDGET_SEVERITY_INFO; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_LABEL, 21, 0, "The engine must be off."); s.severity = SCO_WIDGET_SEVERITY_WARNING; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_LABEL, 22, 0, "The last request failed."); s.severity = SCO_WIDGET_SEVERITY_ERROR; n.push_back(s); }
    if (shift) for (auto& s : n) { s.id += shift; if (s.parent) s.parent += shift; }
    return n;
}
static std::vector<Spec> Lists() {
    std::vector<Spec> n;
    { Spec s = Mk(SCO_WIDGET_LIST, 1, 0, "Your ships", "list.ships");
      s.items = {"Aurora MR", "Avenger Titan", "Cutlass Black", "Freelancer", "Gladius", "Hornet F7C", "Mustang Alpha", "Nomad"};
      s.index = 1; s.visibleRows = 4; n.push_back(s); }
    { Spec s = Mk(SCO_WIDGET_TABLE, 2, 0, "Status table", "table.stats"); s.columns = 3; s.visibleRows = 3;
      for (int r = 0; r < 6; ++r) { s.items.push_back("Row " + std::to_string(r)); s.items.push_back(std::to_string(r * 11)); s.items.push_back(r % 2 ? "busy" : "ready"); }
      n.push_back(s); }
    n.push_back(Mk(SCO_WIDGET_ROW, 3, 0, ""));
    n.push_back(Mk(SCO_WIDGET_COLUMN, 4, 3, "Left column"));
    n.push_back(Mk(SCO_WIDGET_LABEL, 5, 4, "Two columns share the row."));
    n.push_back(Mk(SCO_WIDGET_COLUMN, 6, 3, "Right column"));
    n.push_back(Mk(SCO_WIDGET_TOGGLE, 7, 6, "Show details", "cols.details"));
    return n;
}
static sco_result SetPanel(const char* id, const char* title, const std::vector<Spec>& specs) {
    const Built b = Build(specs);
    return table->set_panel(owner, id, title, &b.nodes[0].base.base.base, static_cast<uint32_t>(b.nodes.size()),
                            sizeof(sco_widget_node_1_3), OnEvent, nullptr);
}

// Core's panel table is a game-thread table: the worker asks the main thread to touch it.
static std::mutex gameLock;
static std::function<void()> gameTask;
static std::atomic<bool> gamePending{false};
static void OnGame(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> hold(gameLock);
        gameTask = std::move(fn);
        gamePending = true;
    }
    while (gamePending) std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
static void RunGameTask() {
    if (!gamePending) return;
    std::function<void()> fn;
    { std::lock_guard<std::mutex> hold(gameLock); fn = std::move(gameTask); }
    fn();
    gamePending = false;
}

static std::atomic<int> classicCalls{0};
static void OpenClassic() { ++classicCalls; }
static int32_t AsopYes() { return 1; }
static void LogLine(const char* line) { std::printf("%s\n", line); std::fflush(stdout); }
static std::atomic<int> nudges{0};
static void Nudge() { ++nudges; }

static bool Step(const MenuApi& api, const std::string& line) {
    char out[512] = {};
    const int rc = api.test_step(line.c_str(), out, sizeof(out));
    std::printf("step %-34s -> %s\n", line.c_str(), out);
    if (rc != 0) { ++failed; std::printf("FAIL step '%s': %s\n", line.c_str(), out); return false; }
    ++passed;
    return true;
}
static bool Expect(const char* what, const std::function<bool(const std::vector<Got>&)>& ok) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        if (ok(Snapshot())) { std::printf("ok   %s\n", what); ++passed; return true; }
        if (std::chrono::steady_clock::now() > end) { std::printf("FAIL %s\n", what); ++failed; return false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
static bool Has(const std::vector<Got>& g, const char* tag, uint32_t kind, const std::function<bool(const Got&)>& pred) {
    for (const auto& e : g) if (e.tag == tag && e.kind == kind && pred(e)) return true;
    return false;
}
static bool LiveValue(const char* panel, const char* tag, double& out) {
    for (const auto& p : W::Panels()) if (p.id == panel)
        for (const auto& n : p.nodes) if (n.tag == tag) { out = n.value; return true; }
    return false;
}

// ---- panel dump reader (the format of tools/panel_dump.h; the dump has no tags, so node N is "nN") ----
static std::string Unescape(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) { out += in[i]; continue; }
        const char c = in[++i];
        out += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
    }
    return out;
}
static std::vector<std::string> SplitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t next = s.find(sep, at);
        out.push_back(s.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return out;
}
struct DumpPanel { std::string id, title; std::vector<Spec> nodes; };
static std::vector<DumpPanel> ReadDump(const char* path) {
    std::vector<DumpPanel> all;
    FILE* file = std::fopen(path, "rb");
    if (!file) { std::printf("cannot read dump %s\n", path); return all; }
    std::string line;
    int c;
    auto handle = [&](const std::string& l) {
        const auto f = SplitOn(l, '\t');
        if (f.empty()) return;
        if (f[0] == "panel" && f.size() == 5) { all.push_back({Unescape(f[2]), Unescape(f[3]), {}}); return; }
        if (f[0] != "node" || f.size() != 18 || all.empty()) return;
        Spec s;
        s.kind = std::stoul(f[1]); s.id = std::stoul(f[2]); s.parent = std::stoul(f[3]); s.flags = std::stoul(f[4]);
        s.index = std::stoi(f[5]); s.value = std::stod(f[6]); s.minimum = std::stod(f[7]); s.maximum = std::stod(f[8]);
        s.columns = std::stoul(f[9]); s.severity = std::stoul(f[10]); s.firstRow = std::stoul(f[11]);
        s.visibleRows = std::stoul(f[12]); s.presentation = std::stoul(f[13]);
        s.label = Unescape(f[14]); s.text = Unescape(f[15]); s.tooltip = Unescape(f[16]);
        if (!f[17].empty()) for (const auto& item : SplitOn(f[17], '\x1f')) s.items.push_back(Unescape(item));
        s.tag = "n" + std::to_string(s.id);
        all.back().nodes.push_back(std::move(s));
    };
    while ((c = std::fgetc(file)) != EOF) {
        if (c == '\n') { handle(line); line.clear(); } else if (c != '\r') line += static_cast<char>(c);
    }
    if (!line.empty()) handle(line);
    std::fclose(file);
    return all;
}
// The most complete state of each panel (the file keeps one record per publish).
static std::vector<DumpPanel> BestStates(const std::vector<DumpPanel>& all) {
    std::vector<DumpPanel> out;
    for (const auto& p : all) {
        auto it = std::find_if(out.begin(), out.end(), [&](const DumpPanel& q) { return q.id == p.id; });
        if (it == out.end()) out.push_back(p);
        else if (p.nodes.size() >= it->nodes.size()) *it = p;
    }
    return out;
}
static const W::Node* LiveNode(const std::vector<W::Panel>& panels, const std::string& panel, const std::string& tag) {
    for (const auto& p : panels) if (p.id == panel)
        for (const auto& n : p.nodes) if (n.tag == tag) return &n;
    return nullptr;
}
static bool DisabledChain(const std::vector<Spec>& nodes, size_t i) {
    const Spec* cur = &nodes[i];
    for (;;) {
        if (cur->flags & SCO_WIDGET_DISABLED) return true;
        if (!cur->parent) return false;
        const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Spec& n) { return n.id == cur->parent; });
        if (it == nodes.end()) return false;
        cur = &*it;
    }
}
static bool ExpectFor(const std::string& what, const std::string& panel, const std::string& tag, uint32_t kind,
                      const std::function<bool(const Got&)>& pred) {
    return Expect(what.c_str(), [&](const std::vector<Got>& g) {
        for (const auto& e : g) if (e.panel == panel && e.tag == tag && e.kind == kind && pred(e)) return true;
        return false;
    });
}
// Clicks every control of one panel. Returns how many.
static int Exercise(const MenuApi& api, const std::string& panel, const std::vector<Spec>& nodes) {
    int done = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Spec& n = nodes[i];
        if (n.tag.empty() || DisabledChain(nodes, i)) continue;
        std::vector<W::Panel> live;
        OnGame([&] { live = W::Panels(); });
        const W::Node* node = LiveNode(live, panel, n.tag);
        if (!node) continue;
        { std::lock_guard<std::mutex> hold(gotLock); got.clear(); }
        const std::string label = panel + " " + n.tag + ": ";
        switch (n.kind) {
        case SCO_WIDGET_TOGGLE:
            if (Step(api, "click " + n.tag)) { const double want = node->value != 0 ? 0.0 : 1.0; ExpectFor(label + "toggle", panel, n.tag, n.kind, [&](const Got& e) { return e.value == want; }); ++done; }
            break;
        case SCO_WIDGET_SLIDER: {
            double lo = node->minimum, hi = node->maximum;
            if (hi <= lo) hi = lo + 1;
            const double frac = (node->value - lo) / (hi - lo) > 0.5 ? 0.15 : 0.85;
            const bool integer = node->presentation != SCO_WIDGET_NUMERIC_NUMBER;
            char step[128];
            std::snprintf(step, sizeof(step), "slider %s %.2f", n.tag.c_str(), frac);
            if (Step(api, step)) {
                ExpectFor(label + "slider", panel, n.tag, n.kind, [&](const Got& e) {
                    return e.value >= lo - 1e-9 && e.value <= hi + 1e-9 && (!integer || e.value == std::floor(e.value));
                });
                ++done;
            }
            break;
        }
        case SCO_WIDGET_COMBO:
            if (node->items.size() > 1) {
                const int k = static_cast<int>(((std::max)(node->index, 0) + 1) % static_cast<int>(node->items.size()));
                if (Step(api, "combo " + n.tag + " " + std::to_string(k))) { ExpectFor(label + "combo item " + std::to_string(k), panel, n.tag, n.kind, [&](const Got& e) { return e.index == k; }); ++done; }
            }
            break;
        case SCO_WIDGET_LIST:
        case SCO_WIDGET_TABLE: {
            const size_t vis = node->visible_rows;
            const size_t rows = n.kind == SCO_WIDGET_TABLE ? (node->columns ? node->items.size() / node->columns : 0) : node->items.size();
            const size_t first = (std::min<size_t>)(node->first_row, rows);
            const size_t last = vis == 0 ? rows : (std::min)(rows, first + vis);
            if (n.kind == SCO_WIDGET_LIST && last > first) {
                for (size_t k = first; k < last; ++k) {
                    if (static_cast<int>(k) == node->index) continue;
                    if (Step(api, "list " + n.tag + " " + std::to_string(k))) { ExpectFor(label + "list row " + std::to_string(k), panel, n.tag, n.kind, [&](const Got& e) { return e.index == static_cast<int>(k); }); ++done; }
                    break;
                }
            }
            if (vis > 0 && rows > vis && last < rows) {
                if (Step(api, "click " + n.tag + ".next")) { ExpectFor(label + "page to row " + std::to_string(last), panel, n.tag, SCO_WIDGET_EVENT_PAGE, [&](const Got& e) { return e.row == static_cast<int>(last); }); ++done; }
                if (Step(api, "click " + n.tag + ".prev")) ExpectFor(label + "page back", panel, n.tag, SCO_WIDGET_EVENT_PAGE, [&](const Got& e) { return e.row == static_cast<int>(first); });
            }
            break;
        }
        case SCO_WIDGET_TEXT_INPUT:
            if (Step(api, "text " + n.tag + " Zed")) { ExpectFor(label + "text", panel, n.tag, n.kind, [](const Got& e) { return e.text == "Zed"; }); ++done; }
            break;
        case SCO_WIDGET_BUTTON:
            if (Step(api, "click " + n.tag)) { ExpectFor(label + "button", panel, n.tag, n.kind, [](const Got&) { return true; }); ++done; }
            break;
        default: break;
        }
    }
    return done;
}
static std::vector<DumpPanel> g_dump;

static void Script(const MenuApi& api, std::atomic<bool>& done) {
    const auto yes = [](const Got&) { return true; };
    if (Step(api, "select demo.controls") && Step(api, "check flight.assist") && Step(api, "shot core-controls-before")) {
        if (Step(api, "click flight.assist"))
            Expect("toggle off, tag flight.assist", [&](auto& g) { return Has(g, "flight.assist", SCO_WIDGET_TOGGLE, [](const Got& e) { return e.value == 0.0; }); });
        if (Step(api, "slider flight.mix 0.5"))
            Expect("percent slider near 58, integral", [&](auto& g) { return Has(g, "flight.mix", SCO_WIDGET_SLIDER, [](const Got& e) { return std::fabs(e.value - 57.5) <= 6 && e.value == std::floor(e.value); }); });
        if (Step(api, "slider flight.gain 1.0"))
            Expect("number slider near top", [&](auto& g) { return Has(g, "flight.gain", SCO_WIDGET_SLIDER, [](const Got& e) { return e.value > 1.8; }); });
        if (Step(api, "combo flight.mode 2"))
            Expect("combo index 2", [&](auto& g) { return Has(g, "flight.mode", SCO_WIDGET_COMBO, [](const Got& e) { return e.index == 2; }); });
        if (Step(api, "text flight.name Zed"))
            Expect("text input Zed", [&](auto& g) { return Has(g, "flight.name", SCO_WIDGET_TEXT_INPUT, [](const Got& e) { return e.text == "Zed"; }); });
        if (Step(api, "click flight.reset"))
            Expect("button flight.reset", [&](auto& g) { return Has(g, "flight.reset", SCO_WIDGET_BUTTON, yes); });
        const size_t before = Snapshot().size();
        if (Step(api, "click flight.calibrate")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            CHECK(Snapshot().size() == before); // a disabled control sends nothing
        }
        // Core applied the changes to the retained nodes.
        OnGame([] {
            double v = -1;
            CHECK(LiveValue("demo.controls", "flight.assist", v) && v == 0.0);
            CHECK(LiveValue("demo.controls", "flight.mix", v) && std::fabs(v - 57.5) <= 6);
        });
        Step(api, "shot core-controls-after");
        // A republish with other node ids: the next click still finds its control by tag and key.
        OnGame([] { CHECK(SetPanel("demo.controls", "Flight", Controls(100)) == SCO_OK); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (Step(api, "click flight.reset"))
            Expect("button after a republish reaches the shifted id", [&](auto& g) { return Has(g, "flight.reset", SCO_WIDGET_BUTTON, [](const Got& e) { return e.id == 111; }); });
    }
    if (Step(api, "select demo.lists")) {
        if (Step(api, "list list.ships 2"))
            Expect("list row 2", [&](auto& g) { return Has(g, "list.ships", SCO_WIDGET_LIST, [](const Got& e) { return e.index == 2; }); });
        if (Step(api, "click list.ships.next"))
            Expect("page to row 4", [&](auto& g) { return Has(g, "list.ships", SCO_WIDGET_EVENT_PAGE, [](const Got& e) { return e.row == 4; }); });
        if (Step(api, "click table.stats.next"))
            Expect("table page to row 3", [&](auto& g) { return Has(g, "table.stats", SCO_WIDGET_EVENT_PAGE, [](const Got& e) { return e.row == 3; }); });
        if (Step(api, "click cols.details"))
            Expect("toggle in a column", [&](auto& g) { return Has(g, "cols.details", SCO_WIDGET_TOGGLE, [](const Got& e) { return e.value == 1.0; }); });
        Step(api, "shot core-lists");
    }
    int controls = 0;
    for (const auto& p : g_dump) {
        const std::string page = p.id == "spawn.asop" ? "spawn.vehicles" : p.id;
        if (!Step(api, "select " + page)) continue;
        Step(api, "wait 150");
        std::string shot = p.id;
        for (auto& ch : shot) if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == '_')) ch = '_';
        Step(api, "shot core-" + shot);
        controls += Exercise(api, p.id, p.nodes);
        Step(api, "scroll 0");
    }
    std::printf("info %d controls clicked on %zu dump panels\n", controls, g_dump.size());
    done = true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: rust_menu_host <sc_offline_menu.dll> [screenshot folder]\n"); return 2; }
    const std::string shots = argc > 2 ? argv[2]
        : (std::filesystem::temp_directory_path() / "rust-menu-host-shots").generic_string();
    if (argc > 3) g_dump = BestStates(ReadDump(argv[3]));
    sco::SetGameThread();
    sco::host::BuildApi({"rust-menu-host 1.0"});
    owner = sco::host::NewPlugin("demo");
    CHECK(W::Start() == Result::Ok);
    table = W::Table();
    view = V::ViewTable();
    client = V::ViewClient();
    CHECK(view && client && view->size >= sizeof(sco_widgets_view_v1));
    CHECK(SetPanel("demo.controls", "Flight", Controls(0)) == SCO_OK);
    CHECK(SetPanel("demo.lists", "Ships and tables", Lists()) == SCO_OK);
    for (const auto& p : g_dump) {
        // One plugin identity per id prefix: a plugin registers only ids under its own name.
        static std::vector<std::pair<std::string, sco_plugin*>> owners;
        const std::string prefix = p.id.substr(0, p.id.find('.'));
        auto owned = std::find_if(owners.begin(), owners.end(), [&](const auto& o) { return o.first == prefix; });
        if (owned == owners.end()) { owners.emplace_back(prefix, sco::host::NewPlugin(prefix.c_str())); owned = owners.end() - 1; }
        sco_plugin* const mine = owned->second;
        const Built b = Build(p.nodes);
        CHECK(table->set_panel(mine, p.id.c_str(), p.title.c_str(), &b.nodes[0].base.base.base, static_cast<uint32_t>(b.nodes.size()),
                               sizeof(sco_widget_node_1_3), OnEvent, nullptr) == SCO_OK);
    }

    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::printf("cannot load %s (%lu)\n", argv[1], GetLastError()); return 2; }
    MenuApi api;
    api.start = reinterpret_cast<decltype(api.start)>(GetProcAddress(dll, "scol_menu_start"));
    api.request_stop = reinterpret_cast<decltype(api.request_stop)>(GetProcAddress(dll, "scol_menu_request_stop"));
    api.drain = reinterpret_cast<decltype(api.drain)>(GetProcAddress(dll, "scol_menu_drain"));
    api.test_step = reinterpret_cast<decltype(api.test_step)>(GetProcAddress(dll, "scol_menu_test_step"));
    api.is_typing = reinterpret_cast<decltype(api.is_typing)>(GetProcAddress(dll, "scol_menu_is_typing"));
    api.abi = reinterpret_cast<decltype(api.abi)>(GetProcAddress(dll, "scol_menu_abi"));
    if (!api.start || !api.request_stop || !api.drain || !api.test_step || !api.is_typing || !api.abi) { std::printf("exports missing\n"); return 2; }
    CHECK(api.abi() == 2);
    // The exports src/menu_rust.cpp checks before it starts the module.
    api.is_broken = reinterpret_cast<decltype(api.is_broken)>(GetProcAddress(dll, "scol_menu_is_broken"));
    api.version = reinterpret_cast<decltype(api.version)>(GetProcAddress(dll, "scol_menu_version"));
    CHECK(api.is_broken && api.version && api.version()[0] != '\0');
    if (api.version) std::printf("module version %s\n", api.version());

    StartV1 s{};
    s.size = sizeof(s); s.abi = 2; s.view = view; s.client = client; s.log = LogLine; s.nudge_game = Nudge;
    s.shot_dir = shots.c_str(); s.flags = kFlagVisible | kFlagTest;
    s.title = "sc-offline v0.8.0";
    s.open_classic = OpenClassic; s.asop_available = AsopYes;
    CHECK(api.start(&s) == 0);
    CHECK(api.start(&s) == -2); // already started

    std::atomic<bool> done{false};
    std::thread worker(Script, std::cref(api), std::ref(done));
    // The game thread: what Menu_GameThreadFrame does, many times a second.
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (!done && std::chrono::steady_clock::now() < limit) {
        RunGameTask();
        uint32_t dispatched = 0, dropped = 0;
        const sco_result r = view->dispatch_events(client, &dispatched, &dropped);
        if (r != SCO_OK && r != SCO_CRASHED) { std::printf("dispatch_events answered %d\n", static_cast<int>(r)); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    if (!done) { ++failed; std::printf("FAIL the click-through did not finish in 90 s\n"); }
    worker.join();

    uint64_t g = 0, l = 0, drops = 0, doneSeq = 0;
    CHECK(view->stamps(client, &g, &l, &drops, &doneSeq) == SCO_OK);
    std::printf("stamps: global=%llu list=%llu drops=%llu events_done=%llu nudges=%d\n", (unsigned long long)g, (unsigned long long)l,
                (unsigned long long)drops, (unsigned long long)doneSeq, nudges.load());
    CHECK(doneSeq > 0);
    CHECK(nudges.load() > 0);
    CHECK(api.is_typing() == 0);
    CHECK(api.is_broken() == 0); // no panic and no window error during the click-through

    // Stop the way the host does: request, then drain (the thread leaves the view by itself).
    api.request_stop();
    CHECK(api.drain(5000) == 0);
    // After the join, a menu-thread call answers WRONG_THREAD (nobody is registered) or works for a new thread.
    CHECK(view->stamps(client, nullptr, nullptr, nullptr, nullptr) == SCO_OK);
    W::Stop();
    std::printf("rust_menu_host: %d passed, %d failed\n", passed.load(), failed.load());
    return failed ? 1 : 0;
}
