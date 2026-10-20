// SPDX-License-Identifier: GPL-3.0-only
// Actual native module with copied public-service fixtures; no game execution.
#include "workflow_module.h"
#include <sco_content.h>
#include <sc_loadout.h>
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
namespace {
int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
std::map<std::string,sco_command> commands;
int owner_storage;auto* owner=reinterpret_cast<sco_plugin*>(&owner_storage);
unsigned submits=0,cancels=0;bool malformed=false,complete=false;
bool cancel_failed=false,available=true;int report_mode=0;
uint64_t current=0;sc_loadout_item copied[SC_LOADOUT_MAX_ITEMS]{};uint32_t count=0;
const char data[]="[primary_optic]\noptic_fixture\n[primary]\nweapon_fixture\n[sidearm_tool_battery]\nbattery_fixture\n";
sco_result Item(sco_plugin*,sco_content_kind kind,uint32_t index,sco_content_item* out){
    if(kind!=SCO_CONTENT_LIST||index)return SCO_NOT_FOUND;
    out->id=1;std::strcpy(out->name,"lists/items.txt");return SCO_OK;
}
sco_result Read(sco_plugin*,uint64_t,void* out,uint32_t* size){
    if(!out||*size<sizeof(data)-1){*size=sizeof(data)-1;return SCO_TOO_MANY;}
    std::memcpy(out,data,sizeof(data)-1);*size=sizeof(data)-1;return SCO_OK;
}
sco_content_v1 content{sizeof(content),0,Item,Read};
sco_result Submit(sco_plugin* self,const sc_loadout_request* request,uint64_t* job){
    CHECK(self==owner);++submits;count=request->count;
    for(uint32_t i=0;i<count;++i)copied[i]=request->items[i];
    *job=current=100+submits;return SCO_OK;
}
sco_result Status(sco_plugin*,uint64_t job,sc_loadout_job_report* out){
    CHECK(job==current);out->state=complete?SC_LOADOUT_JOB_COMPLETE:SC_LOADOUT_JOB_PENDING;
    out->completed=complete?count:1;out->report.count=count;out->report.applied=out->completed;
    for(uint32_t i=0;i<out->completed;++i)out->report.slots[i]={copied[i].slot,SC_LOADOUT_APPLIED};
    if(malformed)out->report.slots[0].slot=999;
    if(report_mode==1)out->completed=count+1;
    if(report_mode==2)out->report.applied=out->completed+1;
    if(report_mode==3)out->report.count=count+1;
    if(report_mode==4)out->state=SC_LOADOUT_JOB_COMPLETE,out->completed=0;
    if(report_mode==5)out->state=static_cast<sc_loadout_job_state>(-1);
    if(report_mode==6)out->report.slots[0].status=static_cast<sc_loadout_status>(-1);
    return SCO_OK;
}
sco_result Cancel(sco_plugin*,uint64_t job){CHECK(job==current);++cancels;if(cancel_failed)return SCO_FAILED;current=0;return SCO_OK;}
sc_loadout_v1_2 loadout{sizeof(loadout),0,nullptr,Submit,Status,Cancel};
sco_result Query(const char* name,uint32_t version,const void** out){
    *out=nullptr;
    if(!std::strcmp(name,SCO_CONTENT_NAME))*out=&content;
    if(available&&!std::strcmp(name,SC_LOADOUT_SERVICE_NAME)&&version==SC_LOADOUT_VERSION_1_2)*out=&loadout;
    return *out?SCO_OK:SCO_NOT_FOUND;
}
sco_result Register(sco_plugin*,const sco_command* c){commands[c->name]=*c;return SCO_OK;}
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*){return SCO_OK;}
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn){return SCO_OK;}
int Has(const char*){return 1;}
void Log(sco_plugin*,sco_log_level,const char*){}
sco_result Call(const char* name,const char* selection=nullptr){
    const auto& c=commands.at(name);sco_arg arg{};arg.type=SCO_ARG_STRING;arg.v.s=selection;
    char reply[2048]{};return c.fn(selection?&arg:nullptr,selection?1:0,c.ctx,reply,sizeof(reply));
}
}
int main(int argc,char** argv){
    test_module::Open(argc,argv);sco_api api{};api.size=sizeof(api);api.major=SCO_API_MAJOR;api.minor=SCO_API_MINOR;
    api.query_service=Query;api.register_command=Register;api.subscribe=Subscribe;api.unsubscribe=Unsubscribe;api.has=Has;api.log=Log;
    CHECK(test_module::Load(&api,owner)==SCO_OK);
    CHECK(Call("loadout.job_status")==SCO_OK&&current==0);
    CHECK(Call("loadout.cancel")==SCO_OK&&cancels==0);
    CHECK(Call("loadout.equip","optic_fixture weapon_fixture battery_fixture")==SCO_OK);
    CHECK(submits==1&&count==3&&copied[0].slot==SC_LOADOUT_SLOT_PRIMARY&&copied[1].slot==SC_LOADOUT_SLOT_PRIMARY_OPTIC&&copied[2].slot==SC_LOADOUT_SLOT_SIDEARM_TOOL_BATTERY);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_FAILED&&submits==1);
    CHECK(Call("loadout.job_status")==SCO_OK&&current!=0);
    complete=true;CHECK(Call("loadout.job_status")==SCO_OK&&cancels==1&&current==0);
    complete=false;CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
    malformed=true;CHECK(Call("loadout.job_status")==SCO_FAILED&&cancels==2);malformed=false;
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
    CHECK(Call("loadout.cancel")==SCO_OK&&cancels==3);
    available=false;const auto before=submits;
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_UNAVAILABLE&&submits==before);available=true;
    loadout.size=static_cast<uint32_t>(offsetof(sc_loadout_v1_2,cancel)+sizeof(loadout.cancel)-1);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_UNAVAILABLE&&submits==before);loadout.size=sizeof(loadout);
    const auto saved_cancel=loadout.cancel;loadout.cancel=nullptr;
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_UNAVAILABLE&&submits==before);loadout.cancel=saved_cancel;
    for(report_mode=1;report_mode<=6;++report_mode){
        CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
        CHECK(Call("loadout.job_status")==SCO_FAILED&&current==0);
    }
    report_mode=0;CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
    cancel_failed=true;CHECK(Call("loadout.cancel")==SCO_FAILED&&current!=0);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_FAILED);
    cancel_failed=false;CHECK(Call("loadout.cancel")==SCO_OK&&current==0);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
    const auto retained_job=current;
    const auto retained_submits=submits,retained_cancels=cancels;
    available=false;
    CHECK(Call("loadout.job_status")==SCO_UNAVAILABLE&&current==retained_job);
    CHECK(Call("loadout.cancel")==SCO_UNAVAILABLE&&current==retained_job&&cancels==retained_cancels);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_FAILED&&submits==retained_submits);
    available=true;
    CHECK(Call("loadout.cancel")==SCO_OK&&current==0&&cancels==retained_cancels+1);
    CHECK(Call("loadout.equip","weapon_fixture")==SCO_OK);
    const auto before_unload=cancels;
    test_module::Unload();CHECK(cancels==before_unload+1&&current==0);test_module::Close();
    std::printf("loadout async: %d checks, %d failed\n",checks,failures);return failures?1:0;
}
