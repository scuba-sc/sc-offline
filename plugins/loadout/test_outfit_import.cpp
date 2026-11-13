#include "outfit_import.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(false)
using namespace sco::outfit_import;
using namespace sco::outfit_recipe;
using ImportResult = sco::outfit_import::Result;

struct Context { int calls=0; std::string last; };
Lookup lookup(const std::string& s, void* p) {
    auto& c=*static_cast<Context*>(p); ++c.calls; c.last=s;
    if(s=="missing") return Lookup::absent;
    if(s=="offline") return Lookup::unavailable;
    if(s=="throw") throw 1;
    return Lookup::present;
}
Lookup bad_lookup(const std::string&, void*) { return static_cast<Lookup>(99); }
int find(const Recipe& r,const char* role) {
    for(std::size_t i=0;i<r.count;++i) if(r.nodes[i].role==role) return static_cast<int>(i);
    return -1;
}
const InputAudit& evidence(const SectionAudit& section, const std::string& role,
                           const std::string& value, AuditEventKind kind) {
    for (const auto& event : section.events)
        if (event.kind==kind && event.original.role==role && event.original.value==value) return event;
    std::abort();
}
bool same_recipe(const Recipe& a, const Recipe& b) {
    if (a.count!=b.count || a.input_count!=b.input_count || a.accepted_count!=b.accepted_count) return false;
    for (std::size_t i=0;i<a.count;++i) {
        const auto& x=a.nodes[i]; const auto& y=b.nodes[i];
        if (x.role!=y.role || x.value!=y.value || x.directive!=y.directive || x.tag!=y.tag ||
            x.parent!=y.parent || x.input!=y.input) return false;
    }
    for (std::size_t i=0;i<a.input_count;++i) if (a.inputs[i]!=b.inputs[i]) return false;
    return true;
}
bool same_snapshot(const Snapshot& a, const Snapshot& b) {
    if (a.skipped_unknown!=b.skipped_unknown || a.outfits.size()!=b.outfits.size() ||
        a.sections.size()!=b.sections.size() ||
        a.source.encountered_section_count!=b.source.encountered_section_count ||
        a.source.ignored_outside_section_count!=b.source.ignored_outside_section_count ||
        a.source.nul_truncated_line_count!=b.source.nul_truncated_line_count ||
        a.source.split_line_chunk_count!=b.source.split_line_chunk_count ||
        a.source.bytes_after_text_eof!=b.source.bytes_after_text_eof) return false;
    for (std::size_t i=0;i<a.outfits.size();++i) {
        if (a.outfits[i].name!=b.outfits[i].name || a.outfits[i].inputs.size()!=b.outfits[i].inputs.size() ||
            !same_recipe(a.outfits[i].recipe,b.outfits[i].recipe)) return false;
        for (std::size_t j=0;j<a.outfits[i].inputs.size();++j)
            if (a.outfits[i].inputs[j].role!=b.outfits[i].inputs[j].role ||
                a.outfits[i].inputs[j].value!=b.outfits[i].inputs[j].value) return false;
    }
    for (std::size_t i=0;i<a.sections.size();++i) {
        const auto& x=a.sections[i]; const auto& y=b.sections[i];
        if (x.source_section_index!=y.source_section_index || x.original_name!=y.original_name ||
            x.preview_name!=y.preview_name || x.ignored_header_suffix!=y.ignored_header_suffix ||
            x.preview_retained!=y.preview_retained || x.drop!=y.drop ||
            x.candidate_count!=y.candidate_count || x.nonempty_line_count!=y.nonempty_line_count ||
            x.inspected_candidate_count!=y.inspected_candidate_count || x.unknown_count!=y.unknown_count ||
            x.ignored_line_count!=y.ignored_line_count || x.events.size()!=y.events.size() ||
            x.tail.present!=y.tail.present || x.tail.uninspected_line_count!=y.tail.uninspected_line_count ||
            x.tail.uninspected_candidate_count!=y.tail.uninspected_candidate_count ||
            x.tail.first_line_index!=y.tail.first_line_index || x.tail.has_first_line!=y.tail.has_first_line ||
            x.tail.first_line!=y.tail.first_line || x.tail.has_first_candidate!=y.tail.has_first_candidate ||
            x.tail.first_candidate_index!=y.tail.first_candidate_index ||
            x.tail.first_candidate.role!=y.tail.first_candidate.role ||
            x.tail.first_candidate.value!=y.tail.first_candidate.value) return false;
        for (std::size_t j=0;j<x.events.size();++j) {
            const auto& p=x.events[j]; const auto& q=y.events[j];
            if (p.source_input_index!=q.source_input_index || p.source_line_index!=q.source_line_index ||
                p.kind!=q.kind || p.lookup!=q.lookup || p.original.role!=q.original.role ||
                p.original.value!=q.original.value || p.stored_preview.role!=q.stored_preview.role ||
                p.stored_preview.value!=q.stored_preview.value || p.ignored_suffix!=q.ignored_suffix ||
                p.role_truncated!=q.role_truncated ||
                p.value_truncated!=q.value_truncated || p.preview_input_index!=q.preview_input_index ||
                p.has_recipe_disposition!=q.has_recipe_disposition || p.recipe_disposition!=q.recipe_disposition ||
                p.ignored_line!=q.ignored_line) return false;
        }
    }
    return true;
}

int main() {
    Snapshot s; Context c;
    auto run=[&](const std::string& text, bool sq=false) {
        return import_bytes(text.data(),text.size(),sq,lookup,&c,s);
    };

    std::string text="ignored x\r\n[empty]\r\n[one] trailing\r\n";
    for(int i=0;i<40;++i) text+="foo missing\r\n";
    text+="Head_ItemPort head\r\nHair_ItemPort hair\r\nhAiR_cOlOr variant\r\nArmor_Helmet helmet\r\nClothing_Torso_1 shirt\r\nClothing_Torso2 vest\r\nHead_ItemPort duplicate\r\nunknown good\r\n";
    for(int i=0;i<24;++i) text+="unknown good\r\n";
    text+="ignored offline\r\n";
    CHECK(run(text,true)==ImportResult::ok && s.outfits.size()==1 && s.skipped_unknown==40);
    CHECK(s.sections.size()==2 && s.sections[0].drop==SectionDrop::empty &&
          s.sections[1].preview_retained && s.sections[1].original_name=="one" &&
          s.sections[1].ignored_header_suffix==" trailing");
    auto& o=s.outfits[0]; CHECK(o.name=="one" && o.inputs.size()==32 && o.recipe.accepted_count==32);
    CHECK(o.recipe.inputs[6]==Disposition::duplicate && o.recipe.inputs[7]==Disposition::unsupported);
    CHECK(o.recipe.nodes[find(o.recipe,"Lens_ItemPort")].directive==Value::lens_sq42);
    CHECK(o.recipe.nodes[find(o.recipe,"Material_Variant")].value=="variant");
    CHECK(o.recipe.nodes[find(o.recipe,"Clothing_Torso2")].parent==find(o.recipe,"Clothing_Torso_1"));
    CHECK(o.recipe.nodes[find(o.recipe,"Armor_Helmet")].parent==find(o.recipe,"Armor_Undersuit"));
    CHECK(s.sections[1].unknown_count==40 && s.sections[1].events.size()==72);
    CHECK(s.sections[1].tail.present && s.sections[1].tail.uninspected_candidate_count==1 &&
          s.sections[1].tail.first_candidate.role=="ignored" && s.sections[1].tail.first_candidate.value=="offline");

    // Unknown lookups remain attributable to their exact source section.
    CHECK(run("[left]\nLeftRole missing\n[right]\nRightRole missing\nHead_ItemPort good\n")==ImportResult::ok);
    CHECK(s.sections.size()==2 && s.sections[0].drop==SectionDrop::unknown_only &&
          s.sections[0].unknown_count==1 && s.sections[1].preview_retained && s.sections[1].unknown_count==1);
    CHECK(evidence(s.sections[0],"LeftRole","missing",AuditEventKind::skipped_unknown).lookup==AuditLookup::absent);
    CHECK(evidence(s.sections[1],"RightRole","missing",AuditEventKind::skipped_unknown).lookup==AuditLookup::absent &&
          s.sections[1].source_section_index==2);
    CHECK(run("[empty]\n[unknown-only]\nfoo missing\n")==ImportResult::ok);
    CHECK(s.outfits.empty() && s.sections.size()==2 && s.sections[0].drop==SectionDrop::empty &&
          s.sections[1].drop==SectionDrop::unknown_only);

    // Exact original identifiers survive alongside the old 63-byte preview prefixes.
    std::string long_role(80,'R'), long_item(100,'x'); c.calls=0;
    CHECK(run("[long]\n"+long_role+" "+long_item+"\n")==ImportResult::ok && c.calls==1 && c.last==long_item);
    CHECK(s.outfits[0].inputs[0].role==long_role.substr(0,max_token));
    CHECK(s.outfits[0].inputs[0].value==long_item.substr(0,max_token));
    const auto& long_evidence=evidence(s.sections[0],long_role,long_item,AuditEventKind::preview_input);
    CHECK(long_evidence.role_truncated && long_evidence.value_truncated &&
          long_evidence.stored_preview.role==s.outfits[0].inputs[0].role &&
          long_evidence.stored_preview.value==s.outfits[0].inputs[0].value &&
          long_evidence.recipe_disposition==Disposition::unsupported);
    auto long_assessment=assess_source_integrity(s);
    CHECK(long_assessment.source_intact && long_assessment.preview_loss_count>=3);

    // The 33rd accepted preview candidate is retained as explicitly uninspected evidence;
    // its item is never sent to lookup and is not mislabeled as known or accepted.
    std::string over_limit="[limit]\n";
    for (int i=0;i<32;++i) over_limit+="foo good\n";
    over_limit+="tail_role tail_class\n";
    c.calls=0;
    CHECK(run(over_limit)==ImportResult::ok && s.outfits.size()==1 && s.outfits[0].inputs.size()==32 && c.calls==32);
    CHECK(s.sections[0].candidate_count==33 && s.sections[0].inspected_candidate_count==32);
    CHECK(s.sections[0].tail.present && s.sections[0].tail.uninspected_candidate_count==1 &&
          s.sections[0].tail.has_first_candidate && s.sections[0].tail.first_candidate.role=="tail_role" &&
          s.sections[0].tail.first_candidate.value=="tail_class");
    CHECK(!assess_source_integrity(s).source_intact);

    // Recipe dispositions remain associated with their exact original input records.
    CHECK(run("[dispositions]\nHead_ItemPort head\nHead_ItemPort duplicate\nEyes_ItemPort eyes\n"
              "Hair_Color red\nHat_ItemPort hat\nClothing_Torso2 vest\nunsupported_slot thing\n")==ImportResult::ok);
    CHECK(evidence(s.sections[0],"Head_ItemPort","head",AuditEventKind::preview_input).recipe_disposition==Disposition::emitted);
    CHECK(evidence(s.sections[0],"Head_ItemPort","duplicate",AuditEventKind::preview_input).recipe_disposition==Disposition::duplicate);
    CHECK(evidence(s.sections[0],"Eyes_ItemPort","eyes",AuditEventKind::preview_input).recipe_disposition==Disposition::emitted);
    CHECK(evidence(s.sections[0],"Hair_Color","red",AuditEventKind::preview_input).recipe_disposition==Disposition::orphan);
    CHECK(evidence(s.sections[0],"Hat_ItemPort","hat",AuditEventKind::preview_input).recipe_disposition==Disposition::emitted);
    CHECK(evidence(s.sections[0],"Clothing_Torso2","vest",AuditEventKind::preview_input).recipe_disposition==Disposition::orphan);
    CHECK(evidence(s.sections[0],"unsupported_slot","thing",AuditEventKind::preview_input).recipe_disposition==Disposition::unsupported);
    CHECK(run("[default]\nEyes_ItemPort eyes\nHair_Color red\n")==ImportResult::ok);
    CHECK(evidence(s.sections[0],"Eyes_ItemPort","eyes",AuditEventKind::preview_input).recipe_disposition==Disposition::overridden_default);
    CHECK(evidence(s.sections[0],"Hair_Color","red",AuditEventKind::preview_input).recipe_disposition==Disposition::overridden_default);

    // Non-comment suffixes after an input token or closing section bracket remain in
    // bounded audit evidence and count as preview losses. Comments remain stripped.
    CHECK(run("[comment] # header note\nHead_ItemPort head # input note\n")==ImportResult::ok);
    CHECK(s.sections[0].ignored_header_suffix.empty() &&
          evidence(s.sections[0],"Head_ItemPort","head",AuditEventKind::preview_input).ignored_suffix.empty());
    auto comment_assessment=assess_source_integrity(s);
    CHECK(comment_assessment.source_intact && comment_assessment.preview_loss_count==0);
    CHECK(run("[header-extra] unexpected\nHead_ItemPort head\n")==ImportResult::ok);
    CHECK(s.sections[0].ignored_header_suffix==" unexpected" &&
          assess_source_integrity(s).preview_loss_count==1);
    CHECK(run("[input-extra]\nHead_ItemPort head unexpected\n")==ImportResult::ok);
    CHECK(evidence(s.sections[0],"Head_ItemPort","head",AuditEventKind::preview_input).ignored_suffix==" unexpected" &&
          assess_source_integrity(s).preview_loss_count==1);

    // Fully represented normal records are source-intact, while that says nothing about
    // default values, provider schemas, revalidation or whether a graph is safe to apply.
    CHECK(run("[normal]\nHead_ItemPort head\nEyes_ItemPort eyes\nHair_ItemPort hair\n"
              "Hair_Color brown\nHat_ItemPort hat\n")==ImportResult::ok);
    auto normal_assessment=assess_source_integrity(s);
    CHECK(normal_assessment.source_intact && normal_assessment.preview_loss_count==0 &&
          normal_assessment.unresolved_recipe_directive_count>0);

    // Preserve historical CRLF/comment parsing, text EOF, NUL handling and 255-byte chunks.
    CHECK(run("[x]\r\nfoo good # comment\r\n[y]\r\nfoo good")==ImportResult::ok && s.outfits.size()==2);
    std::string nul="[n]\nfoo good"; nul.push_back('\0'); nul+="ignored\nfoo good\n";
    CHECK(run(nul)==ImportResult::ok && s.outfits[0].inputs.size()==2 &&
          !assess_source_integrity(s).source_intact);
    std::string eof="[e]\nfoo good\n"; eof.push_back(26); eof+="[ignored]\nfoo good\n";
    CHECK(run(eof)==ImportResult::ok && s.outfits.size()==1 && s.outfits[0].name=="e" &&
          s.source.bytes_after_text_eof>0 && !assess_source_integrity(s).source_intact);
    CHECK(run("[chunk]\n"+std::string(255,'x')+"foo good\n")==ImportResult::ok &&
          s.outfits[0].inputs[0].role=="foo" && s.source.split_line_chunk_count==1);
    CHECK(run("[same]\nfoo good\n[same]\nfoo good\n[empty]\n")==ImportResult::ok &&
          s.outfits.size()==2 && s.sections.size()==3 && s.outfits[0].name==s.outfits[1].name);

    // The new hard section bound counts every encountered header, including empty ones.
    std::string sections;
    for(int i=0;i<256;++i) sections+="[empty"+std::to_string(i)+"]\n";
    CHECK(run(sections)==ImportResult::ok && s.sections.size()==256 && s.outfits.empty());
    sections+="[257th-empty]\n";
    const Snapshot before_section_limit=s;
    CHECK(run(sections)==ImportResult::section_limit && same_snapshot(s,before_section_limit));

    // Detailed audit overflow also refuses atomically instead of dropping evidence.
    sections="[many-unknowns]\n";
    for(std::size_t i=0;i<=max_audit_events_per_section;++i) sections+="foo missing\n";
    const Snapshot before_audit_limit=s;
    CHECK(run(sections)==ImportResult::audit_overflow && same_snapshot(s,before_audit_limit));

    // Failed lookups, invalid results and malformed buffers never change any snapshot field.
    CHECK(run("[one]\nHead_ItemPort good\n")==ImportResult::ok);
    const Snapshot before_failure=s;
    CHECK(run("[x]\nfoo good\n[y]\nfoo offline\n")==ImportResult::lookup_unavailable);
    CHECK(same_snapshot(s,before_failure));
    CHECK(import_bytes("[x]\nfoo good",12,false,bad_lookup,nullptr,s)==ImportResult::invalid &&
          same_snapshot(s,before_failure));
    CHECK(run("[x]\nfoo throw\n")==ImportResult::lookup_unavailable && same_snapshot(s,before_failure));
    CHECK(run(std::string("[x]\nfoo good\x01",13))==ImportResult::unsafe_byte && same_snapshot(s,before_failure));
    CHECK(import_bytes(nullptr,1,false,lookup,&c,s)==ImportResult::invalid && same_snapshot(s,before_failure));
    CHECK(run(std::string(max_bytes+1,'a'))==ImportResult::invalid && same_snapshot(s,before_failure));
    CHECK(import_bytes("[x]\nHair_Color red",18,false,nullptr,nullptr,s)==ImportResult::ok &&
          s.outfits.size()==1 && s.sections[0].events[0].lookup==AuditLookup::not_required);
}
