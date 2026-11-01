// SPDX-License-Identifier: GPL-3.0-only
// Each mode runs in its own process so destructor and module-close markers
// demonstrate whether backing survives a failed or unknowable shutdown.
#include "test_app_stop.h"
#include <cstring>
#include <memory>

struct Backing {
    ~Backing() { std::fputs("backing destroyed\n", stderr); std::fflush(stderr); }
};
struct Module {
    ~Module() { std::fputs("module closed\n", stderr); std::fflush(stderr); }
};
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto backing = std::make_unique<Backing>();
    auto module = std::make_unique<Module>();
    if (!std::strcmp(argv[1], "legacy")) {
        test_app::StopOrExit([&] { if (backing && module) std::puts("legacy stop called"); });
    } else if (!std::strcmp(argv[1], "failed")) {
        test_app::StopOrExit([&] { return backing && module ? sco::Result::Failed : sco::Result::Ok; });
    } else if (!std::strcmp(argv[1], "throw")) {
        test_app::StopOrExit([&]() -> sco::Result { if (backing && module) throw 1; return sco::Result::Ok; });
    } else if (!std::strcmp(argv[1], "ok")) {
        test_app::StopOrExit([&] { return backing && module ? sco::Result::Ok : sco::Result::Failed; });
    } else if (!std::strcmp(argv[1], "destroy-then-exit")) {
        module.reset();
        backing.reset();
        std::_Exit(EXIT_FAILURE);
    } else return 2;
    module.reset();
    backing.reset();
}
