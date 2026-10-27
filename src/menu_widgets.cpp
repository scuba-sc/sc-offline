// SPDX-License-Identifier: GPL-3.0-only
#if defined(SCO_HOST_MENU_CLIENT) && !__has_include(<sco/host_menu.h>)
#error "Selected development composition requires its matching Core headers"
#endif
#if __has_include(<sco/host_menu.h>)
#include "menu_widgets.h"
#include "version.h"
#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace sc_offline::menu_sdk {
namespace {
constexpr uint32_t kShow=1, kDarkness=2, kPosition=3;
constexpr const char* kMain="menu.main";
constexpr const char* kStatus="menu.status";
bool Covers(size_t size,size_t offset,size_t member) { return size>=offset && size-offset>=member; }
}

sco::app::HostMenuClient Client::Descriptor() noexcept { return {this,Load,Frame,Stop}; }

sco_result Client::Load(const sco::app::HostMenuContext& identity,void* context) {
    auto& self=*static_cast<Client*>(context);
    // The app invokes a new load only after a prior instance's cleanup succeeded.
    self.cookie_.reset();self.retired_.reset();self.model_=Model{};
    self.identity_=identity;self.widgets_=nullptr;self.mainRevision_=0;
    self.active_=self.mainReady_=self.statusReady_=self.inEvent_=false;
    if(!identity.api || !identity.self ||
       !Covers(identity.api->size,offsetof(sco_api,query_service),sizeof(identity.api->query_service)) ||
       !identity.api->query_service) return SCO_UNAVAILABLE;
    const void* table=nullptr;
    const sco_result result=identity.api->query_service(SCO_WIDGETS_NAME,SCO_WIDGETS_VERSION_1_3,&table);
    if(result!=SCO_OK) return result;
    if(!table) return SCO_UNAVAILABLE;
    const auto* widgets=static_cast<const sco_widgets_v1*>(table);
    if(!Covers(widgets->size,offsetof(sco_widgets_v1,capabilities),sizeof(widgets->capabilities)) ||
       !widgets->set_panel || !widgets->remove_panel || !widgets->capabilities) return SCO_UNAVAILABLE;
    uint32_t caps[SCO_WIDGETS_CAPS_1_2]={SCO_WIDGETS_CAPS_1_2};
    const sco_result capsResult=widgets->capabilities(identity.self,caps);
    if(capsResult!=SCO_OK) return capsResult;
    if(caps[0]<2 || caps[0]>SCO_WIDGETS_CAPS_1_2 ||
       !(caps[SCO_WIDGETS_CAP_FEATURES]&SCO_WIDGETS_FEATURE_NUMERIC_PRESENTATION))
        return SCO_UNAVAILABLE;
    self.widgets_=widgets;self.active_=true;
    return SCO_OK;
}

void Client::Stop(const sco::app::HostMenuContext&,void* context) {
    // The app releases widget resources after this callback; keep cookies reachable until then.
    auto& self=*static_cast<Client*>(context);
    self.active_=self.mainReady_=self.statusReady_=false;
}

void Client::Frame(const sco::app::HostMenuContext& identity,void* context,void* request) {
    auto& self=*static_cast<Client*>(context);
    auto* input=static_cast<FrameInput*>(request);
    if(!self.active_ || !sco::app::HostMenuActive(identity) || !input || !input->capture || !input->render) return;
    self.retired_.reset();
    Snapshot incoming;input->capture(input->context,incoming);
    const bool mainChanged=incoming.imageAvailable!=self.model_.snapshot.imageAvailable ||
        incoming.width!=self.model_.snapshot.width || incoming.height!=self.model_.snapshot.height ||
        incoming.pathUtf8!=self.model_.snapshot.pathUtf8;
    const bool statusChanged=incoming.statusUtf8!=self.model_.snapshot.statusUtf8;
    self.model_.snapshot=std::move(incoming);
    if(mainChanged || !self.mainReady_) self.mainReady_=self.PublishMain();
    if(statusChanged || !self.statusReady_) self.statusReady_=self.PublishStatus();
    input->render(input->context);
    self.retired_.reset();
}

void Client::Event(const char* panel,const sco_widget_event* event,void* context) {
    if(!context || !panel || std::strcmp(panel,kMain) || !event || event->size<sizeof(*event)) return;
    const Cookie copy=*static_cast<const Cookie*>(context);
    Client& self=*copy.client;
    if(!self.active_ || !self.mainReady_ || !sco::app::HostMenuActive(copy.identity) ||
       copy.identity.self!=self.identity_.self || copy.identity.generation!=self.identity_.generation ||
       copy.revision!=self.mainRevision_) return;
    Control control;
    if(event->node_id==kShow && event->kind==SCO_WIDGET_TOGGLE) control=Control::Show;
    else if(event->node_id==kDarkness && event->kind==SCO_WIDGET_SLIDER) control=Control::Darkness;
    else if(event->node_id==kPosition && event->kind==SCO_WIDGET_SLIDER) control=Control::Position;
    else return;
    // Even a refused event may have changed the retained node; reconcile before another event.
    (void)self.model_.Apply(control,event->value);
    self.mainReady_=false;self.inEvent_=true;
    self.mainReady_=self.PublishMain();
    self.inEvent_=false;
}

bool Client::PublishMain() {
    if(!active_ || !widgets_ || mainRevision_==std::numeric_limits<uint64_t>::max()) return false;
    const std::string path=model_.snapshot.pathUtf8.empty() ? "data\\menu_background.png" : model_.snapshot.pathUtf8;
    const auto chunks=TextChunks(path,SCO_WIDGETS_MAX_TEXT);
    if(chunks.empty() || chunks.size()+8>SCO_WIDGETS_MAX_NODES) return false;
    std::vector<sco_widget_node_1_3> nodes;
    nodes.reserve(chunks.size()+8);
    const auto add=[&](uint32_t kind,uint32_t id,const char* label)->sco_widget_node_1_3& {
        nodes.emplace_back();auto& x=nodes.back();auto& n=x.base.base.base;
        n.size=sizeof(x);n.kind=kind;n.id=id;n.label=label;return x;
    };
    add(SCO_WIDGET_LABEL,10,"Background");
    if(model_.ImageAvailable()) {
        auto& show=add(SCO_WIDGET_TOGGLE,kShow,"Show background image").base.base.base;
        show.value=model_.settings.show ? 1 : 0;
        auto& dark=add(SCO_WIDGET_SLIDER,kDarkness,"Darkness");
        dark.base.base.base.minimum=20;dark.base.base.base.maximum=95;dark.base.base.base.value=model_.settings.darkness;
        dark.base.base.base.flags=model_.settings.show ? 0 : SCO_WIDGET_DISABLED;
        dark.presentation=SCO_WIDGET_NUMERIC_INTEGER_PERCENT;
        if(model_.PositionAvailable()) {
            auto& extended=add(SCO_WIDGET_SLIDER,kPosition,"Image position");
            extended.base.base.tooltip="The menu is taller than the picture is wide. Slide to choose which part shows.";
            auto& pos=extended.base.base.base;
            pos.minimum=0;pos.maximum=1;pos.value=model_.settings.position;
            pos.flags=model_.settings.show ? 0 : SCO_WIDGET_DISABLED;
        }
    } else {
        add(SCO_WIDGET_LABEL,11,"To use a background image, save it as menu_background.png (or .jpg) here, then restart the game:");
    }
    for(size_t i=0;i<chunks.size();++i) add(SCO_WIDGET_LABEL,100+static_cast<uint32_t>(i),chunks[i].c_str());
    add(SCO_WIDGET_LABEL,20,"About");
    add(SCO_WIDGET_LABEL,21,SCO_TITLE " is a work in progress, " SCO_BASED_ON ".");
    add(SCO_WIDGET_LABEL,22,"Bug reports: github.com/scuba-sc/sc-offline/issues");
    auto next=std::make_unique<Cookie>(Cookie{this,identity_,mainRevision_+1});
    const auto result=widgets_->set_panel(identity_.self,kMain,"Menu",&nodes[0].base.base.base,
                                           static_cast<uint32_t>(nodes.size()),sizeof(nodes[0]),Event,next.get());
    if(result!=SCO_OK) return false;
    if(inEvent_) retired_=std::move(cookie_);else cookie_.reset();
    cookie_=std::move(next);++mainRevision_;
    return true;
}

bool Client::PublishStatus() {
    if(!active_ || !widgets_) return false;
    const auto chunks=TextChunks(CleanStatus(model_.snapshot.statusUtf8),SCO_WIDGETS_MAX_TEXT);
    if(chunks.empty() || chunks.size()>SCO_WIDGETS_MAX_NODES) return false;
    std::vector<sco_widget_node> nodes(chunks.size());
    for(size_t i=0;i<chunks.size();++i) {
        nodes[i].size=sizeof(nodes[i]);nodes[i].kind=SCO_WIDGET_LABEL;
        nodes[i].id=1+static_cast<uint32_t>(i);nodes[i].label=chunks[i].c_str();
    }
    return widgets_->set_panel(identity_.self,kStatus,"Status",nodes.data(),static_cast<uint32_t>(nodes.size()),
                               sizeof(nodes[0]),nullptr,nullptr)==SCO_OK;
}
} // namespace sc_offline::menu_sdk

#endif // matching development Core header is available
