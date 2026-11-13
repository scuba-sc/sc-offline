#include "outfit_import.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(false)
using namespace sco::outfit_import;
using namespace sco::outfit_recipe;
struct Context { int calls=0; std::string last; };
Lookup lookup(const std::string& s, void* p) { auto& c=*static_cast<Context*>(p); ++c.calls; c.last=s; if(s=="missing") return Lookup::absent; if(s=="offline") return Lookup::unavailable; if(s=="throw") throw 1; return Lookup::present; }
Lookup bad_lookup(const std::string&, void*) { return static_cast<Lookup>(99); }
int find(const Recipe& r,const char* role) { for(std::size_t i=0;i<r.count;++i) if(r.nodes[i].role==role) return static_cast<int>(i); return -1; }
int main() {
    Snapshot s; Context c;
    auto run=[&](const std::string& text, bool sq=false) { return import_bytes(text.data(),text.size(),sq,lookup,&c,s); };
    std::string text="ignored x\r\n[empty]\r\n[one] trailing\r\n";
    for(int i=0;i<40;++i) text+="foo missing\r\n";
    text+="Head_ItemPort head\r\nHair_ItemPort hair\r\nhAiR_cOlOr variant\r\nArmor_Helmet helmet\r\nClothing_Torso_1 shirt\r\nClothing_Torso2 vest\r\nHead_ItemPort duplicate\r\nunknown good\r\n";
    for(int i=0;i<24;++i) text+="unknown good\r\n";
    text+="ignored offline\r\n";
    CHECK(run(text,true)==sco::outfit_import::Result::ok && s.outfits.size()==1 && s.skipped_unknown==40);
    auto& o=s.outfits[0]; CHECK(o.name=="one" && o.inputs.size()==32 && o.recipe.accepted_count==32);
    CHECK(o.recipe.inputs[6]==Disposition::duplicate && o.recipe.inputs[7]==Disposition::unsupported);
    CHECK(o.recipe.nodes[find(o.recipe,"Lens_ItemPort")].directive==Value::lens_sq42);
    CHECK(o.recipe.nodes[find(o.recipe,"Material_Variant")].value=="variant");
    CHECK(o.recipe.nodes[find(o.recipe,"Clothing_Torso2")].parent==find(o.recipe,"Clothing_Torso_1"));
    CHECK(o.recipe.nodes[find(o.recipe,"Armor_Helmet")].parent==find(o.recipe,"Armor_Undersuit"));
    std::string longitem(100,'x'); c.calls=0;
    CHECK(run("[long]\nfoo "+longitem+"\n")==sco::outfit_import::Result::ok && c.calls==1 && c.last==longitem && s.outfits[0].inputs[0].value.size()==63);
    CHECK(run("[default]\nEyes_ItemPort eyes\n")==sco::outfit_import::Result::ok);
    CHECK(s.outfits[0].recipe.inputs[0]==Disposition::overridden_default);
    CHECK(s.outfits[0].recipe.nodes[find(s.outfits[0].recipe,"Lens_ItemPort")].directive==Value::lens_pu);
    CHECK(run("[x]\r\nfoo good # comment\r\n[y]\r\nfoo good")==sco::outfit_import::Result::ok && s.outfits.size()==2);
    std::string nul="[n]\nfoo good"; nul.push_back('\0'); nul+="ignored\nfoo good\n";
    CHECK(run(nul)==sco::outfit_import::Result::ok && s.outfits[0].inputs.size()==2);
    std::string eof="[e]\nfoo good\n"; eof.push_back(26); eof+="[ignored]\nfoo good\n";
    CHECK(run(eof)==sco::outfit_import::Result::ok && s.outfits.size()==1 && s.outfits[0].name=="e");
    // The second chunk of one long physical line is independently parsed.
    CHECK(run("[chunk]\n"+std::string(255,'x')+"foo good\n")==sco::outfit_import::Result::ok && s.outfits[0].inputs[0].role=="foo");
    std::string sections; for(int i=0;i<257;++i) sections+="[s"+std::to_string(i)+"]\nfoo good\n";
    CHECK(run(sections)==sco::outfit_import::Result::ok && s.outfits.size()==256 && s.outfits.back().name=="s255");
    CHECK(run("[same]\nfoo good\n[same]\nfoo good\n[empty]\n")==sco::outfit_import::Result::ok && s.outfits.size()==2 && s.outfits[0].name==s.outfits[1].name);
    sections.clear(); for(int i=0;i<255;++i) sections+="[s]\nfoo good\n";
    sections+="[empty]\n[final]\nfoo good\n[overflow]\nfoo good\n";
    CHECK(run(sections)==sco::outfit_import::Result::ok && s.outfits.size()==256 && s.outfits.back().name=="final");
    auto old=s.outfits.size(); auto oldname=s.outfits.back().name; auto oldskipped=s.skipped_unknown;
    CHECK(run("[x]\nfoo good\n[y]\nfoo offline\n")==sco::outfit_import::Result::lookup_unavailable);
    CHECK(s.outfits.size()==old && s.outfits.back().name==oldname && s.skipped_unknown==oldskipped);
    CHECK(import_bytes("[x]\nfoo good",12,false,bad_lookup,nullptr,s)==sco::outfit_import::Result::invalid && s.outfits.size()==old);
    CHECK(run("[x]\nfoo throw\n")==sco::outfit_import::Result::lookup_unavailable && s.outfits.size()==old);
    CHECK(run(std::string("[x]\nfoo good\x01",13))==sco::outfit_import::Result::unsafe_byte && s.outfits.size()==old);
    CHECK(import_bytes(nullptr,1,false,lookup,&c,s)==sco::outfit_import::Result::invalid && s.outfits.size()==old);
    CHECK(run(std::string(max_bytes+1,'a'))==sco::outfit_import::Result::invalid && s.outfits.size()==old);
    CHECK(import_bytes("[x]\nHair_Color red",18,false,nullptr,nullptr,s)==sco::outfit_import::Result::ok && s.outfits.size()==1);
}

