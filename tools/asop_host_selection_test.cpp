// SPDX-License-Identifier: GPL-3.0-only
#include "../src/asop_shell.h"
#include <sco/runtime.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
namespace {
using namespace sco::game::asop_host;
Config captured;std::vector<std::string> seen;
std::string selection; sco::Result result=sco::Result::NotFound;
}
namespace sco::game::asop_host {
Result Start(const Config& config){captured=config;seen.clear();for(size_t i=0;i<config.shipCount;++i)seen.emplace_back(config.shipClasses[i]);return Result::Ok;}
Status Snapshot(){return {};}
void Pump(uint32_t){}
void Stop(){}
}
namespace sco {
Result InvokeRaw(const void*,const char* name,const void*,uint32_t,void* out,uint32_t* n){
    assert(!std::strcmp(name,"asop_selector.snapshot"));
    if(result!=Result::Ok)return result;
    if(*n<selection.size()){*n=static_cast<uint32_t>(selection.size());return Result::TooMany;}
    if(!selection.empty())std::memcpy(out,selection.data(),selection.size());*n=static_cast<uint32_t>(selection.size());return Result::Ok;
}
}
int main(){
    using namespace offline::asop;
    const auto path=std::filesystem::temp_directory_path()/"asop-selection-host-fixture.txt";
    {std::ofstream f(path);f<<"Test_Ship_A\nTest_Ship_B\n";}
    assert(_putenv_s("SC_OFFLINE_ASOP","on")==0);assert(_putenv_s("SC_OFFLINE_ASOP_FLEET_LIST","ships")==0);
    assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::Ok&&seen.size()==2);
    result=sco::Result::Ok;selection="Test_Ship_B\n";
    assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::Ok&&seen==std::vector<std::string>{"Test_Ship_B"});
    selection.clear();assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::Ok&&seen.empty());
    selection="Other_Ship\n";assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::InvalidConfig);
    selection="Test_Ship_A\nTest_Ship_A\n";assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::InvalidConfig);
    result=sco::Result::Unavailable;assert(StartFromEnvironment(true,true,path.string().c_str())==sco::game::asop_host::Result::InvalidConfig);
    assert(StartFromEnvironment(false,true,path.string().c_str())==sco::game::asop_host::Result::AdmissionRefused);
    std::filesystem::remove(path);_putenv_s("SC_OFFLINE_ASOP","");_putenv_s("SC_OFFLINE_ASOP_FLEET_LIST","");
    std::puts("ASOP_HOST_PASS saved subset reaches public startup config; invalid/unavailable/admission refuse");
}
