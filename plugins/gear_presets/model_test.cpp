// SPDX-License-Identifier: GPL-3.0-only
#include "model.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace sco::features::gear_presets;
static unsigned checks;
static void Check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr,"model failure %u\n",checks); std::exit(1); } }
template<size_t N,size_t M> static void Copy(char (&destination)[N],const char (&source)[M]) {
    static_assert(M<=N); std::memcpy(destination,source,M);
}
int main() {
    sc_inventory_layout_snapshot info{}; info.size=sizeof(info); info.count=info.total_seen=3; info.flags=SC_INVENTORY_LAYOUT_COMPLETE;
    info.source_epoch=123; info.observation_generation=7; Copy(info.identity_domain,"dev.gear.v1"); Copy(info.layout_key,"human.v1");
    std::array<sc_inventory_layout_entry,3> rows{}; for(auto& r:rows) r.size=sizeof(r);
    Copy(rows[0].port_key,"helmet"); rows[0].role=1; rows[0].state=2; rows[0].flags=3; rows[0].raw_item_type=22; rows[0].entity_id=UINT64_MAX;
    Copy(rows[1].port_key,"pack"); rows[1].role=2; rows[1].state=2; rows[1].flags=1; rows[1].raw_item_type=100;
    Copy(rows[2].port_key,"holster"); rows[2].role=1; rows[2].state=1;
    Preset saved; Check(Capture("Test \xD0\x9F",info,rows,saved)); Check(saved.ports.size()==3 && saved.ports[0].key=="helmet");
    std::string blob; Check(Encode(saved,blob)); Preset decoded; Check(Decode(blob,decoded)); Check(decoded.name==saved.name && decoded.ports.size()==3);
    for(size_t i=0;i<blob.size();++i) { Preset untouched=decoded; Check(!Decode(std::string_view(blob).substr(0,i),untouched)); Check(untouched.name==decoded.name); }
    Preset untouched=decoded; Check(!Decode(blob+"x",untouched));
    std::swap(rows[0],rows[2]); rows[2].entity_id=123456; info.source_epoch=999; ++info.observation_generation;
    Preset current; Check(Capture("Current",info,rows,current)); std::string fresh; Check(Encode(current,fresh));
    std::vector<Difference> diff; Check(Compare(decoded,current,diff)); Check(diff.size()==1 && diff[0].advice==Advice::ExactClassUnknown);
    rows[1].raw_item_type=101; rows[2].raw_item_type=23; Check(Capture("Now",info,rows,current)); Check(Compare(saved,current,diff));
    Check(diff.size()==1 && diff[0].advice==Advice::ReplaceCategory && diff[0].wanted==22 && diff[0].current==23);
    rows[0].state=2; rows[0].flags=1; rows[0].raw_item_type=99; Check(Capture("Now",info,rows,current));
    Check(Compare(saved,current,diff)); Check(diff.size()==2 && diff[1].advice==Advice::RemoveItem);
    rows[1].role=1; Check(Capture("Now",info,rows,current)); const auto before=diff; Check(!Compare(saved,current,diff)); Check(diff.size()==before.size());
    rows[1].role=0; Check(Capture("Unknown role",info,rows,current));
    Preset unknownSaved=current; Check(Compare(unknownSaved,current,diff));
    Check(diff.size()==3 && diff[2].advice==Advice::Unclassified);
    info.flags=SC_INVENTORY_LAYOUT_LIMITED; Check(!Capture("partial",info,rows,current));
    info.flags=SC_INVENTORY_LAYOUT_COMPLETE; Copy(rows[1].port_key,"helmet"); Check(!Capture("duplicate",info,rows,current));
    Copy(rows[1].port_key,"pack");
    Check(!Capture("bad\xC0\x80",info,rows,current));
    info.count=info.total_seen=0; Check(Capture("Empty",info,rows,current) && current.ports.empty());
    Check(Encode(current,fresh) && Decode(fresh,untouched) && untouched.ports.empty());
    std::printf("gear preset model: %u checks, 0 failed\n",checks);
}
