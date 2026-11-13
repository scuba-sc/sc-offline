#include "outfit_import.h"
#include <algorithm>
#include <utility>

namespace sco::outfit_import {
namespace {
bool hair_color(const std::string& s) {
    static const char name[] = "hair_color";
    if (s.size() != sizeof(name)-1) return false;
    for (std::size_t i=0; i<s.size(); ++i) {
        char c=s[i]; if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c != name[i]) return false;
    }
    return true;
}

std::string prefix(const std::string& value) {
    return value.substr(0, (std::min)(value.size(), outfit_recipe::max_token));
}

bool append_event(SectionAudit& section, InputAudit&& event) {
    if (section.events.size() >= max_audit_events_per_section) return false;
    section.events.push_back(std::move(event));
    return true;
}

void add_loss(std::size_t amount, std::size_t& count) noexcept {
    const std::size_t maximum = static_cast<std::size_t>(-1);
    count = amount > maximum - count ? maximum : count + amount;
}

bool matches_prefix(const std::string& original, const std::string& stored,
                    std::size_t maximum) noexcept {
    const std::size_t expected=(std::min)(original.size(),maximum);
    if (stored.size()!=expected) return false;
    for (std::size_t i=0;i<expected;++i) if (stored[i]!=original[i]) return false;
    return true;
}

std::string material_suffix(const std::string& line, std::size_t start) {
    if (line.find_first_not_of(" \t",start)==std::string::npos) return {};
    return line.substr(start);
}
}

Result import_bytes(const char* bytes, std::size_t size, bool sq42,
                    outfit_recipe::LookupFn lookup, void* context, Snapshot& output) noexcept {
    using namespace outfit_recipe;
    if (size > max_bytes || (size && !bytes)) return Result::invalid;
    try {
        // Validate the entire copy, even bytes after text EOF, so unsupported input is explicit.
        for (std::size_t i=0;i<size;++i) {
            unsigned char c=static_cast<unsigned char>(bytes[i]);
            if ((c < 32 && c != 0 && c != 9 && c != 10 && c != 13 && c != 26) || c >= 127)
                return Result::unsafe_byte;
        }

        Snapshot next;
        Outfit current;
        bool open=false, stop=false;
        std::size_t current_section = no_preview_index;
        auto finish = [&]() -> Result {
            if (!open) return Result::ok;
            SectionAudit& audit = next.sections[current_section];
            if (current.inputs.empty()) {
                audit.preview_retained = false;
                audit.drop = audit.candidate_count != 0 && audit.unknown_count == audit.candidate_count
                    ? SectionDrop::unknown_only : SectionDrop::empty;
                return Result::ok;
            }
            auto result=plan_accepted(current.inputs.data(),current.inputs.size(),sq42,current.recipe);
            if (result != outfit_recipe::Result::ok) return Result::planning_failed;
            for (auto& event : audit.events) {
                if (event.kind != AuditEventKind::preview_input) continue;
                if (event.preview_input_index >= current.recipe.input_count) return Result::planning_failed;
                event.recipe_disposition = current.recipe.inputs[event.preview_input_index];
                event.has_recipe_disposition = true;
            }
            audit.preview_retained = true;
            audit.drop = SectionDrop::none;
            next.outfits.push_back(std::move(current));
            return Result::ok;
        };

        std::size_t pos=0;
        while (pos<size && !stop) {
            std::string line;
            while (pos<size && line.size()<255) {
                char c=bytes[pos++];
                if (c==26) { stop=true; break; }
                if (c=='\r' && pos<size && bytes[pos]=='\n') { ++pos; c='\n'; }
                line.push_back(c);
                if (c=='\n') break;
            }
            if (line.size()==255 && !line.empty() && line.back()!='\n' && pos<size)
                ++next.source.split_line_chunk_count;

            const std::string raw_line=line;
            if (raw_line.find('\0') != std::string::npos)
                ++next.source.nul_truncated_line_count;
            auto end=line.find_first_of(std::string("\0\r\n#",4));
            if (end!=std::string::npos) line.resize(end);
            auto start=line.find_first_not_of(" \t");
            if (start==std::string::npos) continue;
            if (line[start]=='[') {
                auto status=finish(); if (status!=Result::ok) return status;
                open=false;
                if (next.sections.size()>=max_sections) return Result::section_limit;
                current=Outfit{};
                auto close=line.find(']',start+1);
                current.name=line.substr(start+1, close==std::string::npos ? close : close-start-1);
                if (current.name.size()>127) current.name.resize(127);
                SectionAudit audit;
                audit.source_section_index=next.sections.size()+1;
                audit.preview_name=current.name;
                audit.original_name=line.substr(start+1,
                    close==std::string::npos ? close : close-start-1);
                if (close!=std::string::npos)
                    audit.ignored_header_suffix=material_suffix(line,close+1);
                next.sections.push_back(std::move(audit));
                next.source.encountered_section_count=next.sections.size();
                current_section=next.sections.size()-1;
                open=true;
                continue;
            }

            if (!open) {
                ++next.source.ignored_outside_section_count;
                continue;
            }
            SectionAudit& audit=next.sections[current_section];
            const bool at_input_limit=current.inputs.size()>=max_inputs;
            ++audit.nonempty_line_count;
            const std::size_t source_line_index=audit.nonempty_line_count;
            if (at_input_limit) {
                if (!audit.tail.present) audit.tail.present=true;
                if (audit.tail.uninspected_line_count==0) {
                    audit.tail.has_first_line=true;
                    audit.tail.first_line_index=source_line_index;
                    audit.tail.first_line=raw_line;
                }
                ++audit.tail.uninspected_line_count;
            }

            auto sep=line.find_first_of(" \t",start);
            if (sep==std::string::npos) {
                if (!at_input_limit) {
                    InputAudit event;
                    event.kind=AuditEventKind::ignored_line;
                    event.source_input_index=audit.candidate_count;
                    event.source_line_index=source_line_index;
                    event.ignored_line=raw_line;
                    if (!append_event(audit,std::move(event))) return Result::audit_overflow;
                    ++audit.ignored_line_count;
                }
                continue;
            }
            auto item_start=line.find_first_not_of(" \t",sep);
            if (item_start==std::string::npos) {
                if (!at_input_limit) {
                    InputAudit event;
                    event.kind=AuditEventKind::ignored_line;
                    event.source_input_index=audit.candidate_count;
                    event.source_line_index=source_line_index;
                    event.ignored_line=raw_line;
                    if (!append_event(audit,std::move(event))) return Result::audit_overflow;
                    ++audit.ignored_line_count;
                }
                continue;
            }
            auto item_end=line.find_first_of(" \t\r\n",item_start);
            std::string role=line.substr(start,sep-start);
            std::string item=line.substr(item_start,item_end-item_start);
            std::string suffix=item_end==std::string::npos ? std::string{} : material_suffix(line,item_end);
            const std::size_t candidate_index=audit.candidate_count++;
            if (at_input_limit) {
                ++audit.tail.uninspected_candidate_count;
                if (!audit.tail.has_first_candidate) {
                    audit.tail.has_first_candidate=true;
                    audit.tail.first_candidate_index=candidate_index;
                    audit.tail.first_candidate={std::move(role),std::move(item)};
                }
                continue;
            }

            ++audit.inspected_candidate_count;
            InputAudit event;
            event.source_input_index=candidate_index;
            event.source_line_index=source_line_index;
            event.original={role,item};
            event.ignored_suffix=std::move(suffix);
            event.role_truncated=role.size()>max_token;
            event.value_truncated=item.size()>max_token;
            if (hair_color(role)) {
                event.lookup=AuditLookup::not_required;
            } else {
                if (!lookup) return Result::lookup_unavailable;
                Lookup found;
                try { found=lookup(item,context); }
                catch (...) { return Result::lookup_unavailable; }
                if (found==Lookup::unavailable) return Result::lookup_unavailable;
                if (found==Lookup::absent) {
                    event.kind=AuditEventKind::skipped_unknown;
                    event.lookup=AuditLookup::absent;
                    event.has_recipe_disposition=true;
                    event.recipe_disposition=Disposition::unknown_class;
                    if (!append_event(audit,std::move(event))) return Result::audit_overflow;
                    ++audit.unknown_count;
                    ++next.skipped_unknown;
                    continue;
                }
                if (found!=Lookup::present) return Result::invalid;
                event.lookup=AuditLookup::present;
            }

            event.kind=AuditEventKind::preview_input;
            event.preview_input_index=current.inputs.size();
            event.stored_preview={prefix(role),prefix(item)};
            current.inputs.push_back(event.stored_preview);
            if (!append_event(audit,std::move(event))) return Result::audit_overflow;
        }
        if (stop) next.source.bytes_after_text_eof=size-pos;
        auto status=finish(); if (status!=Result::ok) return status;
        output.outfits.swap(next.outfits);
        output.skipped_unknown=next.skipped_unknown;
        output.sections.swap(next.sections);
        output.source=next.source;
        return Result::ok;
    } catch (...) { return Result::resource_failure; }
}

IntegrityAssessment assess_source_integrity(const Snapshot& snapshot) noexcept {
    IntegrityAssessment assessment;
    assessment.source_intact=true;
    auto loss=[&](std::size_t amount=1) { add_loss(amount,assessment.preview_loss_count); };
    auto incomplete=[&](std::size_t amount=1) {
        assessment.source_intact=false;
        loss(amount);
    };

    if (snapshot.source.ignored_outside_section_count)
        incomplete(snapshot.source.ignored_outside_section_count);
    if (snapshot.source.nul_truncated_line_count)
        incomplete(snapshot.source.nul_truncated_line_count);
    if (snapshot.source.split_line_chunk_count)
        incomplete(snapshot.source.split_line_chunk_count);
    if (snapshot.source.bytes_after_text_eof)
        incomplete(snapshot.source.bytes_after_text_eof);
    if (snapshot.source.encountered_section_count!=snapshot.sections.size()) incomplete();

    std::size_t retained_sections=0, total_unknown=0;
    for (std::size_t section_index=0; section_index<snapshot.sections.size(); ++section_index) {
        const SectionAudit& section=snapshot.sections[section_index];
        if (section.source_section_index!=section_index+1) incomplete();
        if (section.events.size()>max_audit_events_per_section) incomplete();
        if (!matches_prefix(section.original_name,section.preview_name,127)) incomplete();
        if (section.original_name.size()>127) loss();
        if (!section.ignored_header_suffix.empty()) loss();
        if (section.unknown_count>section.candidate_count) incomplete();
        if (section.preview_retained) {
            ++retained_sections;
            if (section.drop!=SectionDrop::none || section.candidate_count==section.unknown_count ||
                retained_sections>snapshot.outfits.size()) incomplete();
            if (retained_sections<=snapshot.outfits.size()) {
                const Outfit& outfit=snapshot.outfits[retained_sections-1];
                const outfit_recipe::Recipe& recipe=outfit.recipe;
                if (outfit.inputs.size()>outfit_recipe::max_inputs ||
                    recipe.count>outfit_recipe::max_nodes || recipe.input_count>outfit_recipe::max_inputs ||
                    recipe.input_count!=outfit.inputs.size() || recipe.accepted_count!=recipe.input_count)
                    incomplete();
                const std::size_t node_count=(std::min)(recipe.count,outfit_recipe::max_nodes);
                for (std::size_t node=0;node<node_count;++node)
                    if (recipe.nodes[node].directive!=outfit_recipe::Value::explicit_input)
                        add_loss(1,assessment.unresolved_recipe_directive_count);
            }
        } else {
            if (section.drop==SectionDrop::none) incomplete();
            if (section.drop==SectionDrop::empty && section.candidate_count!=0) incomplete();
            if (section.drop==SectionDrop::unknown_only &&
                (section.candidate_count==0 || section.unknown_count!=section.candidate_count)) incomplete();
            loss();
        }
        if (section.tail.present) incomplete(section.tail.uninspected_line_count ? section.tail.uninspected_line_count : 1);
        if (section.tail.present!=(section.tail.uninspected_line_count!=0) ||
            section.tail.uninspected_candidate_count>section.tail.uninspected_line_count ||
            (section.tail.uninspected_line_count!=0)!=section.tail.has_first_line ||
            (section.tail.uninspected_candidate_count!=0)!=section.tail.has_first_candidate ||
            (section.tail.has_first_candidate &&
             section.tail.first_candidate_index!=section.inspected_candidate_count) ||
            section.candidate_count!=section.inspected_candidate_count+section.tail.uninspected_candidate_count)
            incomplete();
        if (section.tail.has_first_line &&
            (section.tail.first_line_index==0 || section.tail.first_line_index>section.nonempty_line_count)) incomplete();

        std::size_t observed_candidates=0, observed_unknown=0, observed_ignored=0;
        std::size_t expected_candidate_index=0, expected_preview_index=0;
        for (const auto& event : section.events) {
            if (event.source_line_index==0 || event.source_line_index>section.nonempty_line_count)
                incomplete();
            if (event.kind==AuditEventKind::preview_input || event.kind==AuditEventKind::skipped_unknown) {
                ++observed_candidates;
                if (event.source_input_index!=expected_candidate_index) incomplete();
                ++expected_candidate_index;
                if (event.role_truncated!=(event.original.role.size()>outfit_recipe::max_token) ||
                    event.value_truncated!=(event.original.value.size()>outfit_recipe::max_token)) incomplete();
            } else if (event.source_input_index!=expected_candidate_index) {
                incomplete();
            }
            if (event.kind==AuditEventKind::skipped_unknown) {
                ++observed_unknown;
                add_loss(1,total_unknown);
                loss();
                if (!event.ignored_suffix.empty()) loss();
                if (event.lookup!=AuditLookup::absent || !event.has_recipe_disposition ||
                    event.recipe_disposition!=outfit_recipe::Disposition::unknown_class ||
                    event.preview_input_index!=no_preview_index ||
                    !event.stored_preview.role.empty() || !event.stored_preview.value.empty()) incomplete();
            } else if (event.kind==AuditEventKind::ignored_line) {
                ++observed_ignored;
                loss();
                if (event.ignored_line.empty() || event.has_recipe_disposition ||
                    event.preview_input_index!=no_preview_index) incomplete();
            } else {
                if (!event.ignored_suffix.empty()) loss();
                if (event.role_truncated) loss();
                if (event.value_truncated) loss();
                if (!event.has_recipe_disposition) incomplete();
                else if (event.recipe_disposition!=outfit_recipe::Disposition::emitted) loss();
                if (!matches_prefix(event.original.role,event.stored_preview.role,outfit_recipe::max_token) ||
                    !matches_prefix(event.original.value,event.stored_preview.value,outfit_recipe::max_token)) incomplete();
                const bool skips_lookup=hair_color(event.original.role);
                if (event.lookup!=(skips_lookup ? AuditLookup::not_required : AuditLookup::present)) incomplete();
                if (!section.preview_retained || retained_sections==0 || retained_sections>snapshot.outfits.size() ||
                    event.preview_input_index>=snapshot.outfits[retained_sections-1].inputs.size()) {
                    incomplete();
                } else {
                    const auto& stored=snapshot.outfits[retained_sections-1].inputs[event.preview_input_index];
                    if (stored.role!=event.stored_preview.role || stored.value!=event.stored_preview.value)
                        incomplete();
                    const auto& recipe=snapshot.outfits[retained_sections-1].recipe;
                    if (event.preview_input_index>=recipe.input_count ||
                        event.preview_input_index>=outfit_recipe::max_inputs ||
                        (event.has_recipe_disposition &&
                         event.recipe_disposition!=recipe.inputs[event.preview_input_index])) incomplete();
                }
                if (event.preview_input_index!=expected_preview_index) incomplete();
                ++expected_preview_index;
            }
        }
        if (observed_candidates!=section.inspected_candidate_count ||
            observed_unknown!=section.unknown_count || observed_ignored!=section.ignored_line_count ||
            expected_candidate_index!=section.inspected_candidate_count ||
            section.nonempty_line_count!=section.inspected_candidate_count+
                section.ignored_line_count+section.tail.uninspected_line_count)
            incomplete();
        if (section.preview_retained && retained_sections<=snapshot.outfits.size()) {
            if (section.preview_name!=snapshot.outfits[retained_sections-1].name ||
                expected_preview_index!=snapshot.outfits[retained_sections-1].inputs.size()) incomplete();
        }
    }
    if (retained_sections!=snapshot.outfits.size() || total_unknown!=snapshot.skipped_unknown) incomplete();
    return assessment;
}
}
