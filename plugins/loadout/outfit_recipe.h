#pragma once
#include <array>
#include <cstddef>
#include <string>

// Internal semantic recipe only; lookup is an injected synthetic seam, not a public service.
// Inputs are already-tokenized copied legacy port/value pairs, not raw file lines. The
// caller must handle parsing and the original loader's pre-storage truncation and
// 32 accepted-piece cutoff. This planner accepts at most 32 copied candidates; it
// refuses oversized tokens rather than silently truncating or parsing raw file lines.
namespace sco::outfit_recipe {
constexpr std::size_t max_inputs = 32, max_nodes = 64, max_token = 63;
enum class Lookup { present, absent, unavailable };
using LookupFn = Lookup (*)(const std::string&, void*);
struct Input { std::string role, value; };
enum class Disposition { emitted, unknown_class, duplicate, unsupported, orphan, overridden_default };
enum class Value { explicit_input, default_body, default_undersuit, default_head, default_eyes, explicit_head_default_eyes, default_teeth, explicit_head_default_teeth, default_hair, default_hair_color, lens_pu, lens_sq42, mobiglas, mobiglas_screen, legacy_mobiglas_screen };
enum class Tag { none, head_accessory, head_hair_male, mobiglas };
struct Node { std::string role; std::string value; Value directive = Value::explicit_input; Tag tag = Tag::none; int parent = -1; int input = -1; };
struct Recipe { std::array<Node, max_nodes> nodes{}; std::size_t count = 0; std::array<Disposition, max_inputs> inputs{}; std::size_t input_count = 0; std::size_t accepted_count = 0; };
enum class Result { ok, no_outfit, invalid, lookup_unavailable, overflow };
// For records already classified by the importer; no lookup is repeated.
Result plan_accepted(const Input* inputs, std::size_t count, bool sq42_lens, Recipe& output);
Result plan(const Input* inputs, std::size_t count, bool sq42_lens, LookupFn lookup, void* context, Recipe& output);
}
