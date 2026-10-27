// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "menu_model.h"
#include "sco/host_menu.h"
#include "sco_widgets.h"
#include <memory>

namespace sc_offline::menu_sdk {
struct FrameInput {
    void* context = nullptr;
    void (*capture)(void*, Snapshot&) = nullptr;
    void (*render)(void*) = nullptr;
};

class Client {
public:
    sco::app::HostMenuClient Descriptor() noexcept;
    const Settings& CurrentSettings() const noexcept { return model_.settings; }
    bool Available() const { return active_ && sco::app::HostMenuActive(identity_); }
    bool MainReady() const noexcept { return mainReady_; }
    bool StatusReady() const noexcept { return statusReady_; }
private:
    struct Cookie { Client* client; sco::app::HostMenuContext identity; uint64_t revision; };
    static sco_result Load(const sco::app::HostMenuContext&, void*);
    static void Frame(const sco::app::HostMenuContext&, void*, void*);
    static void Stop(const sco::app::HostMenuContext&, void*);
    static void Event(const char*, const sco_widget_event*, void*);
    bool PublishMain();
    bool PublishStatus();
    sco::app::HostMenuContext identity_{};
    const sco_widgets_v1* widgets_ = nullptr;
    Model model_;
    uint64_t mainRevision_ = 0;
    bool active_ = false, mainReady_ = false, statusReady_ = false, inEvent_ = false;
    std::unique_ptr<Cookie> cookie_, retired_;
};
} // namespace sc_offline::menu_sdk
