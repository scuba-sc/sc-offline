// SPDX-License-Identifier: GPL-3.0-only
// Retained grouped Build/SQ42 picker. Only invokes public external build commands.
#include "builtins/build_menu.h"
#include "../plugins/build/build_state.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <share.h>
#endif
// Host filesystem/status integration only; no game pointer or feature implementation.
bool DataFilePath(char* path, unsigned long size, const char* name);
void SetMenuStatus(const char* fmt, ...);
namespace {
constexpr int kMaxBuild = 4096, kMaxCats = 24;
char names[kMaxBuild][512]{}, categories[kMaxCats][24]{};
int category[kMaxBuild]{}, count = -1, cats = 0, selected = 0;
float reach = 50;
const sco_api* api = nullptr; sco_plugin* self = nullptr;
#define HAS(t,type,m) ((t) && (t)->size >= offsetof(type,m)+sizeof((t)->m) && (t)->m)
void Reply(sco_result r,const char* message,void*) {
    if(message&&*message)SetMenuStatus("%s",message);
    else if(r==SCO_NOT_FOUND)SetMenuStatus("Build plugin unavailable: install/enable its SDK module; no host fallback.");
    else SetMenuStatus("Build request result %d (public command only).",static_cast<int>(r));
}
void Invoke(const char* command,const sco_arg* args=nullptr,uint32_t n=0) {
    if(!self || !HAS(api,sco_api,invoke)) { SetMenuStatus("Build command facade unavailable; no host fallback."); return; }
    const auto r=api->invoke(self,command,args,n,Reply,nullptr);
    if(r!=SCO_OK)Reply(r,nullptr,nullptr);
}
const sc_offline_build_state_v1* State() {
    const void* out=nullptr;
    if(!HAS(api,sco_api,query_service) || api->query_service(SC_OFFLINE_BUILD_STATE_NAME,SC_OFFLINE_BUILD_STATE_VERSION,&out)!=SCO_OK)return nullptr;
    return static_cast<const sc_offline_build_state_v1*>(out);
}
void LoadList() {
    char path[260];count=0;
    if(!DataFilePath(path,sizeof(path),"buildables.txt"))return;
#ifdef _WIN32
    FILE* file=_fsopen(path,"r",_SH_DENYNO);
#else
    FILE* file=std::fopen(path,"r");
#endif
    if(!file)return;
    char line[1024];int cat=-1;
    while(count<kMaxBuild&&fgets(line,sizeof(line),file)) {
        // Never silently turn an overlong path/class into another name.
        if(!strchr(line,'\n')&&!feof(file)){int c;while((c=fgetc(file))!='\n'&&c!=EOF){}continue;}
        line[strcspn(line,"\r\n#")]=0;char* name=line+strspn(line," \t");
        const size_t n=strlen(name);if(!n)continue;
        if(*name=='[') {name[strcspn(name,"]")]=0;if(cats<kMaxCats){std::snprintf(categories[cats],sizeof(categories[cats]),"%s",name+1);cat=cats++;}continue;}
        if(cat<0||n>=sizeof(names[0]))continue;
        std::memcpy(names[count],name,n+1);category[count++]=cat;
    }
    fclose(file);
}
}
void BuildMenuUseApi(const sco_api* next,sco_plugin* owner){api=next;self=owner;}
int Menu_BuildCount(){if(count<0)LoadList();return count;}
const char* Menu_BuildName(int i){return i>=0&&i<Menu_BuildCount()?names[i]:"";}
const char* Menu_BuildCategory(int i){return i>=0&&i<Menu_BuildCount()?categories[category[i]]:"";}
int Menu_BuildCategoryOf(int i){return i>=0&&i<Menu_BuildCount()?category[i]:-1;}
int Menu_BuildCategoryCount(){Menu_BuildCount();return cats;}
const char* Menu_BuildCategoryName(int c){return c>=0&&c<cats?categories[c]:"";}
void Menu_SetBuild(int index,float metres){
    if(index==selected&&metres==reach)return;
    selected=index;reach=metres;if(!*Menu_BuildName(index))return;
    sco_arg args[2]{};args[0].type=SCO_ARG_STRING;args[0].v.s=Menu_BuildName(index);args[1].type=SCO_ARG_FLOAT;args[1].v.f=metres;
    Invoke("build.select",args,2);
}
float Menu_BuildReach(){return reach;}
void Menu_ToggleBuildMode(){
    if(Menu_BuildModeActive()){Invoke("build.toggle");return;}
    if(*Menu_BuildName(selected)) {
        sco_arg args[2]{};args[0].type=SCO_ARG_STRING;args[0].v.s=Menu_BuildName(selected);args[1].type=SCO_ARG_FLOAT;args[1].v.f=reach;
        Invoke("build.select",args,2);
    }
    Invoke("build.toggle");
}
bool Menu_BuildModeActive(){const auto* state=State();return HAS(state,sc_offline_build_state_v1,active)&&state->active();}
int Menu_BuildPlacedCount(){const auto* state=State();return HAS(state,sc_offline_build_state_v1,placed_count)?static_cast<int>(state->placed_count()):0;}
void Menu_BuildUndo(){Invoke("build.undo");}
void Menu_BuildClear(){Invoke("build.clear");}
void Menu_RequestPlace(int index,bool inFront,float metres){
    if(!*Menu_BuildName(index))return;
    sco_arg args[2]{};args[0].type=SCO_ARG_STRING;args[0].v.s=Menu_BuildName(index);args[1].type=SCO_ARG_FLOAT;args[1].v.f=inFront?metres:0;
    Invoke("build.place",args,2);
}
#undef HAS
