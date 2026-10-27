// SPDX-License-Identifier: GPL-3.0-only
// Host lifecycle, real SDK spawn module and file coordinator over a synthetic disk.
#include "workflow_module.h"
#include <sco/app.h>
#include <sco/widgets.h>
#include <sco/game/asop_selection.h>
#include <algorithm>
#include <cstdio>
#include <string>
namespace S = sco::game::asop_selection;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
struct Disk {
    S::Document file;
    int writes = 0;
    static S::FileResult Read(void* p, S::Document& out) { out = static_cast<Disk*>(p)->file; return S::FileResult::Ok; }
    static S::FileResult Replace(void* p, const S::Document& expected, std::string_view bytes, S::Document& out, const S::CommitPermit& permit) {
        auto& d = *static_cast<Disk*>(p);
        if (d.file.bytes != expected.bytes || d.file.identity != expected.identity || d.file.present != expected.present) return S::FileResult::Conflict;
        if (!permit.begin(permit.context)) return S::FileResult::Cancelled;
        d.file.present = true; d.file.bytes = bytes; ++d.file.identity[0]; ++d.writes; out = d.file; return S::FileResult::Ok;
    }
};
static sco::Result start(void* context) {
    S::Options options; options.file = {context, Disk::Read, Disk::Replace}; options.catalogue = {"A", "B"};
    return S::Start(options);
}
static sco::Result pump(void*) { return S::Pump(); }
static sco::Result stopped = sco::Result::Failed;
static sco::Result stop(void*) { stopped = S::Stop(); return stopped; }
struct Click : sco::widgets::Renderer {
    const char* label = nullptr; uint32_t kind = 0; double value = 0;
    void Render(const sco::widgets::Panel& panel, std::vector<sco::widgets::Event>& events) override {
        const auto it = std::find_if(panel.nodes.begin(), panel.nodes.end(), [&](const auto& node) { return node.label == label; });
        CHECK(it != panel.nodes.end()); sco::widgets::Event event{}; event.node_id = it->id; event.kind = kind; event.value = value; events.push_back(event);
    }
};
static void Fire(const char* label, uint32_t kind, double value = 0) {
    Click click; click.label = label; click.kind = kind; click.value = value;
    CHECK(sco::widgets::RenderPanel("spawn.asop", click) == sco::Result::Ok);
}
int main(int argc, char** argv) {
    test_module::Open(argc, argv);
    Disk disk; disk.file.present = true; disk.file.bytes = "A\nUnknown\n";
    const sco::plugins::Builtin spawn{"spawn", test_module::Query, test_module::Load, test_module::Unload};
    sco::app::Platform platform; platform.builtins = &spawn; platform.nBuiltins = 1;
    platform.hostService = {&disk, start, pump, stop};
    CHECK(sco::app::Start(platform));
    Fire("B", SCO_WIDGET_TOGGLE, 1); Fire("Save selection", SCO_WIDGET_BUTTON);
    CHECK(disk.writes == 0); sco::app::Tick(1);
    CHECK(disk.writes == 1 && disk.file.bytes.find("B\n") != std::string::npos && disk.file.bytes.find("Unknown\n") != std::string::npos);
    Fire("Select none", SCO_WIDGET_BUTTON); Fire("Save selection", SCO_WIDGET_BUTTON);
    sco::app::Stop();
    CHECK(stopped == sco::Result::Ok && disk.writes == 1);
    test_module::Close();
    std::puts("ASOP app lifecycle + SDK spawn module + synthetic disk passed");
}
