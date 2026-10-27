// SPDX-License-Identifier: GPL-3.0-only
#include "menu_widgets.h"
#include "sco/app.h"
#include "sco/widgets.h"
#include <cassert>
#include <cstdio>

using namespace sc_offline::menu_sdk;
struct Fixture {
    Client* client;
    Snapshot snapshot;
    double candidate = 0;
    bool send = false;
    int renders = 0;
};
static void Capture(void* p, Snapshot& out) { out=static_cast<Fixture*>(p)->snapshot; }
struct InputRenderer : sco::widgets::Renderer {
    double value;
    void Render(const sco::widgets::Panel&,std::vector<sco::widgets::Event>& events) override {
        sco::widgets::Event e;e.node_id=2;e.kind=SCO_WIDGET_SLIDER;e.value=value;
        events.push_back(e);e.value=21;events.push_back(e);
    }
};
static void Render(void* p) {
    auto& f=*static_cast<Fixture*>(p);++f.renders;
    if(f.send && f.client->MainReady()) {
        InputRenderer renderer;renderer.value=f.candidate;
        assert(sco::widgets::RenderPanel("menu.main",renderer)==sco::Result::Ok);
    }
}
int main() {
    Client client;sco::app::Platform p;p.pluginsEnabled=false;p.hostMenu=client.Descriptor();
    assert(sco::app::Start(p));
    Fixture f{};f.client=&client;FrameInput frame{&f,Capture,Render};
    f.snapshot.pathUtf8="data/menu_background.png";
    assert(sco::app::HostMenuFrame(&frame));assert(client.MainReady() && client.StatusReady());
    assert(sco::app::IsHostMenuPanel("menu.main") && sco::app::IsHostMenuPanel("menu.status"));
    f.snapshot.imageAvailable=true;f.snapshot.width=100;f.snapshot.height=100;f.snapshot.statusUtf8="Ready";
    f.send=true;f.candidate=75;
    assert(sco::app::HostMenuFrame(&frame));assert(client.CurrentSettings().darkness==75);
    // A callback publishes a new generation; the second stale event must be dropped.
    assert(client.CurrentSettings().darkness!=21);
    f.send=false;f.snapshot.pathUtf8=std::string(255*260,'x');
    assert(sco::app::HostMenuFrame(&frame));assert(!client.MainReady() && client.StatusReady());
    assert(client.CurrentSettings().darkness==75);
    f.snapshot.pathUtf8="restored.png";f.snapshot.statusUtf8=std::string(255*260,'x');
    assert(sco::app::HostMenuFrame(&frame));assert(client.MainReady() && !client.StatusReady());
    f.snapshot.statusUtf8="Recovered";
    assert(sco::app::HostMenuFrame(&frame));assert(client.MainReady() && client.StatusReady());
    sco::app::Stop();
    std::puts("menu widgets: publication, stale-event and independent recovery passed");
}
