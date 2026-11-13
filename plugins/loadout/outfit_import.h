#pragma once
#include "outfit_recipe.h"
#include <cstddef>
#include <string>
#include <vector>

// Copied, bounded bytes only: this module neither opens files nor activates a provider.
// ASCII text is supported, including NUL (terminates the current fgets chunk), CRLF
// translation and Ctrl-Z (text EOF). Other control bytes and non-ASCII bytes are
// refused: locale-dependent legacy comparisons are not reproduced.
namespace sco::outfit_import {
constexpr std::size_t max_bytes = 1024 * 1024;
// Both limits are deliberate audit bounds. More encountered sections or more than
// this many detailed records in one section refuse the whole import transaction.
constexpr std::size_t max_sections = 256, max_audit_events_per_section = 128;
constexpr std::size_t no_preview_index = static_cast<std::size_t>(-1);
struct Outfit { std::string name; std::vector<outfit_recipe::Input> inputs; outfit_recipe::Recipe recipe; };

enum class AuditLookup { not_attempted, not_required, present, absent };
enum class AuditEventKind { preview_input, skipped_unknown, ignored_line };
struct InputAudit {
    std::size_t source_input_index = 0;
    std::size_t source_line_index = 0;
    AuditEventKind kind = AuditEventKind::preview_input;
    AuditLookup lookup = AuditLookup::not_attempted;
    outfit_recipe::Input original;
    outfit_recipe::Input stored_preview;
    std::string ignored_suffix;
    bool role_truncated = false;
    bool value_truncated = false;
    std::size_t preview_input_index = no_preview_index;
    bool has_recipe_disposition = false;
    outfit_recipe::Disposition recipe_disposition = outfit_recipe::Disposition::unknown_class;
    std::string ignored_line;
};
struct UninspectedTail {
    bool present = false;
    std::size_t uninspected_line_count = 0;
    std::size_t uninspected_candidate_count = 0;
    std::size_t first_line_index = 0;
    bool has_first_line = false;
    std::string first_line;
    bool has_first_candidate = false;
    std::size_t first_candidate_index = no_preview_index;
    outfit_recipe::Input first_candidate;
};
enum class SectionDrop { none, empty, unknown_only };
struct SectionAudit {
    std::size_t source_section_index = 0;
    std::string original_name;
    std::string preview_name;
    std::string ignored_header_suffix;
    bool preview_retained = false;
    SectionDrop drop = SectionDrop::empty;
    std::size_t candidate_count = 0;
    std::size_t nonempty_line_count = 0;
    std::size_t inspected_candidate_count = 0;
    std::size_t unknown_count = 0;
    std::size_t ignored_line_count = 0;
    std::vector<InputAudit> events;
    UninspectedTail tail;
};
struct SourceAudit {
    std::size_t encountered_section_count = 0;
    std::size_t ignored_outside_section_count = 0;
    std::size_t nul_truncated_line_count = 0;
    std::size_t split_line_chunk_count = 0;
    std::size_t bytes_after_text_eof = 0;
};
struct Snapshot {
    std::vector<Outfit> outfits;
    std::size_t skipped_unknown = 0;
    std::vector<SectionAudit> sections;
    SourceAudit source;
};
struct IntegrityAssessment {
    // This only describes evidence completeness under the legacy parser. It is
    // never a complete-graph or safe-to-submit determination.
    bool source_intact = false;
    std::size_t preview_loss_count = 0;
    std::size_t unresolved_recipe_directive_count = 0;
};
enum class Result {
    ok, invalid, unsafe_byte, lookup_unavailable, planning_failed,
    resource_failure, audit_overflow, section_limit
};
Result import_bytes(const char* bytes, std::size_t size, bool sq42_lens,
                    outfit_recipe::LookupFn lookup, void* context, Snapshot& output) noexcept;
IntegrityAssessment assess_source_integrity(const Snapshot& snapshot) noexcept;
}
