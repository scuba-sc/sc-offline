// SPDX-License-Identifier: GPL-3.0-only
#include "menu_model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace sc_offline::menu_sdk;
int main() {
    Model model;
    assert(model.settings.show && model.settings.darkness == 74 && model.settings.position == 0.5);
    assert(!model.Apply(Control::Darkness, 30));
    model.snapshot.imageAvailable = true; model.snapshot.width = 100; model.snapshot.height = 100;
    for (int value = 20; value <= 95; ++value) {
        assert(model.Apply(Control::Darkness, value));
        assert(model.settings.darkness == value);
    }
    for (double value : {19.0, 96.0, 74.5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        assert(!model.Apply(Control::Darkness, value)); assert(model.settings.darkness == 95);
    }
    assert(model.Apply(Control::Position, 0.25));
    assert(model.Apply(Control::Show, 0));
    assert(!model.Apply(Control::Darkness, 30));
    assert(!model.Apply(Control::Position, 0.6));
    assert(!model.Apply(Control::Show, 0.5));
    assert(model.Apply(Control::Show, 1));
    model.snapshot.width = 70; model.snapshot.height = 100;
    assert(!model.Apply(Control::Position, 0.6));
    assert(model.settings.position == 0.25);
    const std::string path = std::string(254, 'a') + "\xe2\x82\xac" + std::string(254, '%') + "##";
    const auto chunks = TextChunks(path, 255);
    std::string joined;
    for (const auto& chunk : chunks) { assert(chunk.size() <= 255); assert(ValidUtf8(chunk)); joined += chunk; }
    assert(joined == path && chunks.size() == 3);
    assert(CleanStatus(std::string(254, 'x') + "\xe2") == std::string(254, 'x') + "\xef\xbf\xbd");
    assert(CleanStatus("100% ## literal") == "100% ## literal");
    std::puts("menu model: defaults, exact percent domain and unavailable/disabled event refusal passed");
}
