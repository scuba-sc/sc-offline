// SPDX-License-Identifier: GPL-3.0-only
// Run on Windows against a temporary fixed data root; no game or installation files.
#include "../src/asop_selection_file.h"
#include <sc_asop_selection.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace S=sco::game::asop_selection;
static std::string root;
bool DataFilePath(char* out,DWORD size,const char* file) {
    const auto p=root+"\\"+file;
    if(p.size()+1>size)return false;
    std::memcpy(out,p.c_str(),p.size()+1);return true;
}
static bool allow(void*) {return true;}
static bool deny(void*) {return false;}
static int fails=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++fails;}}while(0)
static bool write_external(const std::string& text) {
    const auto path=root+"\\asop_ships.txt";
    HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD count=0;bool ok=WriteFile(f,text.data(),static_cast<DWORD>(text.size()),&count,nullptr) && count==text.size() && FlushFileBuffers(f);
    CloseHandle(f);return ok;
}
#ifdef SCO_ASOP_SELECTION_TESTING
static int fault_mode=0;
static const char* fail_stage=nullptr;
static bool stage_hit=false;
bool AsopSelectionTestPoint(const char* stage) {
    if(fail_stage && !std::strcmp(fail_stage,stage)){fail_stage=nullptr;stage_hit=true;return false;}
    if ((fault_mode==5 && !std::strcmp(stage,"prepared")) || (fault_mode==6 && !std::strcmp(stage,"replaced"))) {
        TerminateProcess(GetCurrentProcess(),86);return false;
    }
    if ((fault_mode==1 && !std::strcmp(stage,"prepared")) || (fault_mode==2 && !std::strcmp(stage,"replaced"))) {
        fault_mode=0;return false;
    }
    if (fault_mode==3 && !std::strcmp(stage,"prepared")) {fault_mode=0;return write_external("Racing\n");}
    if (fault_mode==4 && !std::strcmp(stage,"replaced")) {fault_mode=0;return write_external("After\n");}
    return true;
}
static std::string read_path(const std::string& path) {
    HANDLE f=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return {};
    char data[512]{};DWORD n=0;const BOOL ok=ReadFile(f,data,sizeof(data),&n,nullptr);CloseHandle(f);
    return ok?std::string(data,n):std::string();
}
static bool check_and_remove_artifacts(const std::string& expected_backup,const std::string& expected_candidate) {
    WIN32_FIND_DATAA entry{};HANDLE find=FindFirstFileA((root+"\\asop_ships.txt.sco-*").c_str(),&entry);
    if(find==INVALID_HANDLE_VALUE)return false;
    std::vector<std::string> paths;bool backup=false,candidate=false,marker=false;
    do {
        const std::string name=entry.cFileName,path=root+"\\"+name;paths.push_back(path);
        if(name.ends_with(".bak"))backup=read_path(path)==expected_backup;
        if(name.ends_with(".candidate"))candidate=read_path(path)==expected_candidate;
        if(name.ends_with(".pending"))marker=true;
    } while(FindNextFileA(find,&entry));
    FindClose(find);
    if(!backup || !candidate || !marker)return false;
    bool ok=true;for(const auto& path:paths)if(!DeleteFileA(path.c_str()))ok=false;return ok;
}
static bool run_crash_child(const char* mode) {
    char exe[32768]{};const DWORD n=GetModuleFileNameA(nullptr,exe,sizeof(exe));
    if(!n || n>=sizeof(exe))return false;
    std::string command="\""+std::string(exe)+"\" "+mode+" \""+root+"\"";
    STARTUPINFOA startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))return false;
    const DWORD wait=WaitForSingleObject(process.hProcess,30000);DWORD exit_code=0;
    const bool ok=wait==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&exit_code) && exit_code==86;
    if(wait==WAIT_TIMEOUT)TerminateProcess(process.hProcess,87);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return ok;
}
#endif
int main(int argc,char** argv) {
#ifdef SCO_ASOP_SELECTION_TESTING
    if(argc==3) {
        root=argv[2];offline::asop_selection::FixedFile child;
        if(!child.Initialize())return 90;
        auto adapter=child.Adapter();S::Document current,out;
        if(adapter.read(adapter.context,current)!=S::FileResult::Ok)return 91;
        fault_mode=!std::strcmp(argv[1],"--crash-prepared")?5:!std::strcmp(argv[1],"--crash-replaced")?6:0;
        if(!fault_mode)return 92;
        S::CommitPermit yes{nullptr,allow};
        (void)adapter.replace(adapter.context,current,"ProcessRecovered\n",out,yes);return 93;
    }
#else
    (void)argc;(void)argv;
#endif
    char tmp[MAX_PATH]{};CHECK(GetTempPathA(MAX_PATH,tmp)>0);
    root=std::string(tmp)+"sco-asop-file-test-"+std::to_string(GetCurrentProcessId());
    CHECK(CreateDirectoryA(root.c_str(),nullptr)!=0);
    offline::asop_selection::FixedFile file;CHECK(file.Initialize());auto adapter=file.Adapter();
    S::Document absent,saved,current;
    CHECK(adapter.read(adapter.context,absent)==S::FileResult::NotFound && !absent.present);
    S::CommitPermit yes{nullptr,allow},no{nullptr,deny};
    CHECK(write_external("Created_Externally\n"));
    CHECK(adapter.replace(adapter.context,absent,"A\n",saved,yes)==S::FileResult::Conflict);
    const auto path=root+"\\asop_ships.txt";CHECK(DeleteFileA(path.c_str())!=0);
    CHECK(adapter.replace(adapter.context,absent,"A\n",saved,yes)==S::FileResult::Ok && saved.present && saved.bytes=="A\n");
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="A\n");
    CHECK(adapter.replace(adapter.context,current,"B\n",saved,no)==S::FileResult::Cancelled);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="A\n");
    const auto stale=current;
    CHECK(write_external("External\n"));
    CHECK(adapter.replace(adapter.context,stale,"B\n",saved,yes)==S::FileResult::Conflict);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="External\n");
    CHECK(adapter.replace(adapter.context,current,"B\n",saved,yes)==S::FileResult::Ok && saved.bytes=="B\n");
#ifdef SCO_ASOP_SELECTION_TESTING
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="B\n");
    for(const char* kind:{"temp","candidate","marker"})for(const char* step:{"create","write","flush","close"}) {
        const std::string stage=std::string(kind)+"."+step;fail_stage=stage.c_str();stage_hit=false;
        CHECK(adapter.replace(adapter.context,current,"Unwritten\n",saved,yes)==S::FileResult::Error && stage_hit);
        CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="B\n");
    }
    fault_mode=1;CHECK(adapter.replace(adapter.context,current,"NotCommitted\n",saved,yes)==S::FileResult::OutcomeUnknown);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="B\n");
    fault_mode=2;CHECK(adapter.replace(adapter.context,current,"Recovered\n",saved,yes)==S::FileResult::OutcomeUnknown);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="Recovered\n");
    CHECK(run_crash_child("--crash-prepared"));
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="Recovered\n");
    CHECK(run_crash_child("--crash-replaced"));
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="ProcessRecovered\n");
    fault_mode=3;CHECK(adapter.replace(adapter.context,current,"Candidate\n",saved,yes)==S::FileResult::Conflict);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::OutcomeUnknown && !file.Diagnostic().empty());
    CHECK(check_and_remove_artifacts("Racing\n","Candidate\n"));
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="Candidate\n");
    fault_mode=4;CHECK(adapter.replace(adapter.context,current,"Next\n",saved,yes)==S::FileResult::Conflict);
    CHECK(adapter.read(adapter.context,current)==S::FileResult::OutcomeUnknown && !file.Diagnostic().empty());
    CHECK(check_and_remove_artifacts("Candidate\n","Next\n"));
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Ok && current.bytes=="After\n");
#endif
    CHECK(write_external(std::string(SC_ASOP_SELECTION_MAX_FILE_BYTES+1,'x')));
    CHECK(adapter.read(adapter.context,current)==S::FileResult::Invalid);
    CHECK(DeleteFileA(path.c_str())!=0);CHECK(RemoveDirectoryA(root.c_str())!=0);
    CHECK(adapter.read(adapter.context,current)!=S::FileResult::NotFound);
    root="relative";offline::asop_selection::FixedFile relative;CHECK(!relative.Initialize());
    root="C:\\wild*card";offline::asop_selection::FixedFile wildcard;CHECK(!wildcard.Initialize());
    std::printf("ASOP fixed-file Windows test: %d failures\n",fails);return fails?1:0;
}
