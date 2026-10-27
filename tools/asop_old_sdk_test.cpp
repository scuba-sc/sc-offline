// SPDX-License-Identifier: GPL-3.0-only
#include "workflow_module.h"
#include <sco/host.h>
#include <sco/widgets.h>
#include <cstdio>
#include <cstring>
int main(int argc,char** argv) {
    sco::SetGameThread();const auto* api=sco::host::BuildApi({"old SDK test"});
    if(!api || sco::widgets::Start()!=sco::Result::Ok)return 1;
    test_module::Open(argc,argv);auto* owner=sco::host::NewPlugin("spawn");
    if(!owner || test_module::Load(api,owner)!=SCO_OK)return 1;
    bool found=false;
    for(const auto& panel:sco::widgets::Panels())if(panel.id=="spawn.asop")
        for(const auto& node:panel.nodes)if(node.label=="ASOP selection unavailable on this SDK")found=true;
    test_module::Unload();if(sco::Release(owner)!=sco::Result::Ok)return 1;
    sco::widgets::Stop();test_module::Close();
    if(!found)return 1;
    std::puts("RC1 spawn module displays ASOP unavailable with real Core widgets: PASS");
}
