#include "outfit_recipe.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); std::abort(); } } while (false)
#include <string>
using namespace sco::outfit_recipe;
Lookup lookup(const std::string& s, void*) { return s == "missing" ? Lookup::absent : s == "offline" ? Lookup::unavailable : Lookup::present; }
Lookup throwing(const std::string&, void*) { throw 1; }
Lookup invalid_lookup(const std::string&, void*) { return static_cast<Lookup>(99); }
int find(const Recipe& r, const char* role) { for (std::size_t i=0;i<r.count;++i) if(r.nodes[i].role==role) return static_cast<int>(i); return -1; }
int main() {
    Recipe r;
    CHECK(plan(nullptr,0,false,lookup,nullptr,r)==Result::no_outfit && r.count==0);
    Input unknown_role[]={{"unsupported", "ok"}};
    CHECK(plan(unknown_role,1,false,lookup,nullptr,r)==Result::ok && r.accepted_count==1 && r.inputs[0]==Disposition::unsupported);
    CHECK(r.count==10 && r.nodes[0].directive==Value::default_body);
    int h=find(r,"Head_ItemPort"), e=find(r,"Eyes_ItemPort"), hair=find(r,"Hair_ItemPort"), mobi=find(r,"mobiglas_attach");
    CHECK(r.nodes[h].directive==Value::default_head && r.nodes[e].parent==h);
    CHECK(r.nodes[find(r,"Lens_ItemPort")].directive==Value::lens_pu);
    CHECK(r.nodes[find(r,"Teeth_ItemPort")].tag==Tag::head_accessory);
    CHECK(r.nodes[hair].tag==Tag::head_hair_male && r.nodes[find(r,"Material_Variant")].parent==hair);
    CHECK(r.nodes[find(r,"mobiglas_screen_attach")].parent==mobi && r.nodes[find(r,"mobiglas_screen_attach")].tag==Tag::mobiglas);
    Input pieces[]={{"Head_ItemPort","head"},{"Eyes_ItemPort","eyes"},{"Hair_ItemPort","hair"},{"Hair_Color","offline"},{"Clothing_Torso_1","shirt"},{"Clothing_Torso2","vest"},{"Armor_Helmet","helmet"},{"Hat_ItemPort","hat"}};
    CHECK(plan(pieces,8,true,lookup,nullptr,r)==Result::ok);
    CHECK(r.nodes[find(r,"Lens_ItemPort")].directive==Value::lens_sq42);
    CHECK(r.nodes[find(r,"Material_Variant")].value=="offline");
    CHECK(r.nodes[find(r,"Clothing_Torso2")].parent==find(r,"Clothing_Torso_1"));
    CHECK(r.nodes[find(r,"Armor_Undersuit")].directive==Value::default_undersuit);
    CHECK(r.nodes[find(r,"Armor_Helmet")].parent==find(r,"Armor_Undersuit"));
    CHECK(r.nodes[find(r,"Teeth_ItemPort")].directive==Value::explicit_head_default_teeth);
    Input ignored[]={{"Hair_Color","red"},{"Eyes_ItemPort","eyes"},{"Clothing_Torso2","vest"},{"foo","ok"},{"head_itemport","missing"},{"HEAD_ITEMPORT","first"},{"Head_ItemPort","second"},{"Hair_Color","blue"}};
    CHECK(plan(ignored,8,false,lookup,nullptr,r)==Result::ok);
    CHECK(r.inputs[0]==Disposition::orphan && r.inputs[2]==Disposition::orphan && r.inputs[3]==Disposition::unsupported);
    CHECK(r.inputs[4]==Disposition::unknown_class && r.inputs[5]==Disposition::emitted && r.inputs[6]==Disposition::duplicate && r.inputs[7]==Disposition::duplicate);
    Input defaults[]={{"Eyes_ItemPort","eyes"},{"Hair_Color","red"},{"Hat_ItemPort","hat"}};
    CHECK(plan(defaults,3,false,lookup,nullptr,r)==Result::ok && r.inputs[0]==Disposition::overridden_default && r.inputs[1]==Disposition::overridden_default && r.inputs[2]==Disposition::emitted);
    CHECK(r.accepted_count==3);
    Recipe before=r; Input fail[]={{"Head_ItemPort","offline"}};
    CHECK(plan(fail,1,false,lookup,nullptr,r)==Result::lookup_unavailable && r.count==before.count && r.nodes[0].role==before.nodes[0].role);
    CHECK(plan(fail,1,false,nullptr,nullptr,r)==Result::lookup_unavailable);
    CHECK(plan(fail,1,false,throwing,nullptr,r)==Result::lookup_unavailable && r.count==before.count);
    CHECK(plan(fail,1,false,invalid_lookup,nullptr,r)==Result::invalid && r.count==before.count);
    fail[0].value="missing"; CHECK(plan(fail,1,false,lookup,nullptr,r)==Result::no_outfit && r.count==before.count);
    fail[0].value=std::string(64,'x'); CHECK(plan(fail,1,false,lookup,nullptr,r)==Result::invalid);
    CHECK(plan(fail,33,false,lookup,nullptr,r)==Result::invalid);
    Input explicit_defaults[]={{"Head_ItemPort","head"},{"Armor_Undersuit","suit"},{"Eyebrow_ItemPort","brow"},{"Hair_Color","red"}};
    CHECK(plan(explicit_defaults,4,false,lookup,nullptr,r)==Result::ok);
    CHECK(r.nodes[find(r,"Eyes_ItemPort")].directive==Value::explicit_head_default_eyes);
    CHECK(r.nodes[find(r,"Armor_Undersuit")].value=="suit" && r.inputs[3]==Disposition::orphan);
    CHECK(find(r,"Eyebrow_ItemPort")>=0 && find(r,"Material_Variant")<0);
    Input only_color[]={{"Hair_Color","red"}};
    CHECK(plan(only_color,1,false,nullptr,nullptr,r)==Result::ok && r.inputs[0]==Disposition::overridden_default);
    Input all[]={{"Body_ItemPort","body"},{"Clothing_Feet","a"},{"Clothing_Legs","a"},{"Clothing_Torso_0","a"},{"Clothing_Hands","a"},{"Clothing_Torso_1","a"},{"Clothing_Torso2","a"},{"Armor_Undersuit","a"},{"Armor_Helmet","a"},{"Armor_Torso","a"},{"Armor_Arms","a"},{"Armor_Legs","a"},{"Head_ItemPort","a"},{"Eyes_ItemPort","a"},{"Teeth_ItemPort","a"},{"Eyebrow_ItemPort","a"},{"Hair_ItemPort","a"},{"Hair_Color","a"},{"Hat_ItemPort","a"},{"Eye_Accessories_ItemPort","a"},{"Head_Accessory_ItemPort","a"},{"Head_Horn_ItemPort","a"},{"Jewellery_ItemPort","a"}};
    CHECK(plan(all,23,false,lookup,nullptr,r)==Result::ok && r.count==27);
    for (int i=0;i<23;++i) CHECK(r.inputs[i]==Disposition::emitted);
    for (std::size_t i=0;i<r.count;++i) CHECK(r.nodes[i].parent < static_cast<int>(i));
    Input full[32]; for(auto& i:full) i={"foo","ok"};
    CHECK(plan(full,32,false,lookup,nullptr,r)==Result::ok);
    for(std::size_t i=0;i<r.count;++i) CHECK(r.nodes[i].parent < static_cast<int>(i));
}
