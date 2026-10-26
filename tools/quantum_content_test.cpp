// SPDX-License-Identifier: GPL-3.0-only
// Actual module/public records; synthetic pack snapshots, no game providers.
#include <sco_api.h>
#include <sco_content.h>
#include <sco_settings.h>
#include <sco_widgets.h>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned checks=0;
std::map<std::string,sco_command> commands;
std::vector<std::string> destinations;
std::string root,reply;
struct File { std::string pack,name,bytes; };
std::vector<File> files;
bool present=true,endless=false;
int bad_metadata=0,bad_read=0;
unsigned item_calls=0,read_calls=0;
const void* publication=nullptr;
sco_result content_query_result=SCO_OK;
sco_plugin* owner=nullptr;
void Check(bool condition,const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr,"quantum content: %s; reply=%s\n",message,reply.c_str()); std::exit(1); }
}
sco_result Register(sco_plugin* self,const sco_command* c) { Check(self==owner,"command owner"); commands[c->name]=*c; return SCO_OK; }
sco_result Subscribe(sco_plugin*,const char*,sco_event_fn,void*) { return SCO_OK; }
sco_result Unsubscribe(sco_plugin*,const char*,sco_event_fn) { return SCO_OK; }
int Has(const char*) { return 0; }
sco_result GetString(sco_plugin* self,const char* name,char* out,uint32_t* size) {
    Check(self==owner,"settings owner");
    if (std::strcmp(name,"data_root")) return SCO_NOT_FOUND;
    if (*size<=root.size()) { *size=static_cast<uint32_t>(root.size()+1); return SCO_TOO_MANY; }
    std::memcpy(out,root.c_str(),root.size()+1); *size=static_cast<uint32_t>(root.size()+1); return SCO_OK;
}
sco_settings_v1 settings{};
sco_result Item(sco_plugin* self,sco_content_kind kind,uint32_t ordinal,sco_content_item* out) {
    ++item_calls;
    Check(self==owner && kind==SCO_CONTENT_LIST,"content owner/kind");
    if (ordinal>=files.size() && !endless) return SCO_NOT_FOUND;
    if(endless) { *out={}; out->size=sizeof(*out); out->kind=kind; out->id=ordinal+1; std::snprintf(out->name,sizeof(out->name),"lists/other.txt"); return SCO_OK; }
    *out={}; out->size=sizeof(*out); out->kind=kind; out->id=ordinal+1;
    std::snprintf(out->pack,sizeof(out->pack),"%s",files[ordinal].pack.c_str());
    std::snprintf(out->name,sizeof(out->name),"%s",files[ordinal].name.c_str());
    switch(bad_metadata) {
        case 1: --out->size; break; case 2: out->kind=SCO_CONTENT_SCRIPT; break;
        case 3: out->id=0; break; case 4: std::memset(out->name,'x',sizeof(out->name)); break;
        case 5: std::memset(out->pack,'x',sizeof(out->pack)); break; default: break;
    }
    return SCO_OK;
}
sco_result Read(sco_plugin* self,uint64_t id,void* out,uint32_t* size) {
    ++read_calls;
    Check(self==owner && id && id<=files.size(),"copied read owner/id");
    const auto& bytes=files[static_cast<size_t>(id-1)].bytes;
    if(!out && bad_read==1) { *size=SCO_CONTENT_MAX_FILE_BYTES+1; return SCO_TOO_MANY; }
    if(!out && bad_read==2) { *size=static_cast<uint32_t>(bytes.size()); return SCO_OK; }
    if(out && bad_read==3) return SCO_UNAVAILABLE;
    if (*size<bytes.size()) { *size=static_cast<uint32_t>(bytes.size()); return SCO_TOO_MANY; }
    if (!bytes.empty()) std::memcpy(out,bytes.data(),bytes.size());
    *size=static_cast<uint32_t>(bytes.size())-(bad_read==4 && !bytes.empty()?1u:0u); return SCO_OK;
}
sco_content_v1 content{};
sco_result Panel(sco_plugin*,const char*,const char*,const sco_widget_node* nodes,uint32_t count,uint32_t,sco_widget_event_fn,void*) {
    for(uint32_t i=0;i<count;++i) if(nodes[i].id==2 && nodes[i].kind==SCO_WIDGET_LIST) {
        destinations.clear(); for(uint32_t j=0;j<nodes[i].item_count;++j) destinations.emplace_back(nodes[i].items[j]);
    }
    return SCO_OK;
}
sco_result Remove(sco_plugin*,const char*) { return SCO_OK; }
sco_widgets_v1 widgets{};
sco_result Query(const char* name,uint32_t version,const void** out) {
    Check(version==0x10000u,"public1.0 query"); *out=nullptr;
    if (!std::strcmp(name,SCO_CONTENT_NAME) && present) { *out=publication; return content_query_result; }
    if (!std::strcmp(name,SCO_SETTINGS_NAME)) *out=&settings;
    if (!std::strcmp(name,SCO_WIDGETS_NAME)) *out=&widgets;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Call(const char* name) {
    Check(commands.count(name)!=0,"command present"); char text[1024]{}; const auto& c=commands.at(name);
    const auto r=c.fn(nullptr,0,c.ctx,text,sizeof(text)); reply=text; return r;
}
}
int main(int argc,char** argv) {
    Check(argc==2,"module path");
#ifdef _WIN32
    auto module=LoadLibraryA(argv[1]); Check(module!=nullptr,"load module");
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(GetProcAddress(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(GetProcAddress(module,"sco_plugin_unload"));
#else
    auto module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!module)std::fprintf(stderr,"%s\n",dlerror()); Check(module!=nullptr,"load module");
    const auto load=reinterpret_cast<sco_result(*)(const sco_api*,sco_plugin*)>(dlsym(module,"sco_plugin_load"));
    const auto unload=reinterpret_cast<void(*)()>(dlsym(module,"sco_plugin_unload"));
#endif
    Check(load && unload,"public exports");
    const auto path=std::filesystem::temp_directory_path()/"sco-quantum-content-test"; std::filesystem::remove_all(path); std::filesystem::create_directories(path); root=path.string();
    int token=0; owner=reinterpret_cast<sco_plugin*>(&token);
    sco_api api{}; api.size=sizeof(api); api.major=SCO_API_MAJOR; api.minor=SCO_API_MINOR; api.register_command=Register; api.query_service=Query; api.has=Has; api.subscribe=Subscribe; api.unsubscribe=Unsubscribe;
    settings.size=sizeof(settings); settings.get_string=GetString;
    content.size=sizeof(content); content.item=Item; content.read=Read;
    publication=&content;
    widgets.size=sizeof(widgets); widgets.set_panel=Panel; widgets.remove_panel=Remove;
    files={{"curated","lists/locations.txt","Synthetic | Public place | public_zone | 10\n"}};
    Check(load(&api,owner)==SCO_OK,"consumer loads");
    Check(Call("quantum.status")==SCO_OK && reply.find("1 destinations")!=reply.npos,"curated list arrives through public content");
    Check(destinations.size()==1 && destinations[0]=="Synthetic / Public place","actual copied widget destination");
    auto reload=[&] { unload(); commands.clear(); destinations.clear(); item_calls=read_calls=0; Check(load(&api,owner)==SCO_OK,"consumer reload"); Check(Call("quantum.status")==SCO_OK,"status remains callable"); };
    { std::ofstream(path/"locations.txt")<<"Raw | Ignored raw base | raw_zone | 0\n"; }
    files.clear(); reload(); Check(destinations.empty() && reply.find("No enabled lists/locations.txt")!=reply.npos,"absent pack never imports raw curated file");
    present=false; reload(); Check(destinations.empty() && reply.find("Unavailable:")!=reply.npos,"missing/old content provider explicit refusal"); present=true;
    publication=nullptr; reload(); Check(destinations.empty() && !item_calls,"OK query with null table refuses"); publication=&content;
    content_query_result=SCO_FAILED; reload(); Check(destinations.empty() && !item_calls,"failed query does not use returned table"); content_query_result=SCO_OK;
    for(const char* wrong:{"locations.txt","/lists/locations.txt","Lists/locations.txt","lists/locations.TXT","lists/nested/locations.txt"}) {
        files={{"curated",wrong,"S | Wrong namespace | zone | 0\n"}}; reload(); Check(destinations.empty(),"only exact canonical namespace selected");
    }
    files={{"first","lists/locations.txt","S | First | first_zone | 1\n"},{"second","lists/locations.txt","S | Second | second_zone | 2\n"}};
    reload(); Check(destinations.empty() && !read_calls && reply.find("Ambiguous")!=reply.npos,"duplicate canonical sources across packs refused without guessing");
    files.resize(1);
    files[0].bytes.clear(); reload(); Check(destinations.empty() && reply.find("Loaded 0 curated")!=reply.npos,"explicit empty snapshot is valid"); files[0].bytes="S | First | first_zone | 1\n";
    for(bad_metadata=1;bad_metadata<=5;++bad_metadata) { reload(); Check(destinations.empty() && !read_calls && reply.find("Invalid copied")!=reply.npos,"malformed metadata refused before read"); } bad_metadata=0;
    for(uint32_t bytes=4;bytes<sizeof(content);++bytes) {
        auto table=std::make_unique<unsigned char[]>(bytes); std::memcpy(table.get(),&content,bytes); std::memcpy(table.get(),&bytes,4); publication=table.get();
        reload(); Check(destinations.empty() && !item_calls && !read_calls,"physically short item/read callback extent refuses");
    }
    publication=&content; content.item=nullptr; reload(); Check(!item_calls && destinations.empty(),"null item callback"); content.item=Item;
    content.read=nullptr; reload(); Check(!item_calls && destinations.empty(),"null read callback"); content.read=Read;
    for(bad_read=1;bad_read<=4;++bad_read) { reload(); Check(destinations.empty(),"oversized/inconsistent/refused copied read has no partial list"); } bad_read=0;
    endless=true; reload(); Check(destinations.empty() && item_calls==SCO_CONTENT_MAX_ITEMS+1 && reply.find("4096")!=reply.npos,"unterminated enumeration is finite"); endless=false;
    { std::ofstream(path/"locations_found.txt")<<"Found | Generated | generated_zone | 0\n"; }
    const std::string valid="S | Good | good_zone | 0\n";
    for(const auto& bad:std::vector<std::string>{"bad row\n","S | Missing entity | | 0\n","S | Bad radius | z | nan\n","S | Bad radius | z | -1\n","S | Bad radius | z | 1junk\n",std::string(513,'x')+"\n",std::string("S | Nul | z | 0\n")+'\0',std::string(32,'S')+" | Name | z | 0\n","S | "+std::string(64,'N')+" | z | 0\n","S | Name | "+std::string(96,'E')+" | 0\n"}) {
        files[0].bytes=valid+bad; reload(); Check(destinations.size()==1 && destinations[0]=="Found / Generated","malformed second row refuses atomic curated parse; generated persistence remains");
    }
    files[0].bytes=std::string(1024*1024+1,'x'); reload(); Check(destinations.size()==1 && reply.find("1MiB")!=reply.npos,"consumer file byte bound");
    files[0].bytes.clear(); for(unsigned i=0;i<3001;++i) files[0].bytes+="S | Place | z"+std::to_string(i)+" | 0\n";
    reload(); Check(destinations.size()==1 && reply.find("3000")!=reply.npos,"unique destination count refuses atomically");
    files[0].bytes.erase(files[0].bytes.rfind("S | Place | z3000")); reload(); Check(reply.find("3000 destinations")!=reply.npos,"exact3000 curated destination bound accepted");
    files[0].bytes=" S | Alpha | same_zone | 10\r\nS | Duplicate | SAME_ZONE | 99\nS | Beta | other_zone | \n";
    { std::ofstream(path/"locations_found.txt")<<"Found | Duplicate generated | same_zone | 999\nFound | Generated | generated_zone | 0\n"; }
    reload(); Check(destinations==std::vector<std::string>{"S / Alpha","S / Beta","Found / Generated"},"row order and first entity win; curated precedes generated");
    const auto calls=item_calls+read_calls; files[0].bytes="S | Not observed while loaded | changed | 0\n";
    Check(Call("quantum.status")==SCO_OK && item_calls+read_calls==calls && destinations[0]=="S / Alpha","loaded consumer retains copies without requery/retained IDs");
    reload(); Check(destinations[0]=="S / Not observed while loaded","reload reads provider snapshot again");
    unload(); unload(); const auto after_unload=item_calls+read_calls; Check(Call("quantum.status")==SCO_UNAVAILABLE && item_calls+read_calls==after_unload,"stale command after unload cannot use content service");
    for(uint32_t bytes=4;bytes<offsetof(sco_api,query_service)+sizeof(api.query_service);++bytes) {
        auto short_api=std::make_unique<unsigned char[]>(bytes); std::memcpy(short_api.get(),&api,bytes); std::memcpy(short_api.get(),&bytes,4);
        Check(load(reinterpret_cast<const sco_api*>(short_api.get()),owner)==SCO_UNAVAILABLE,"physically short root query callback extent refused");
    }
    std::filesystem::remove_all(path);
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("quantum public content: %u checks PASS; synthetic snapshots only\n",checks);
}
