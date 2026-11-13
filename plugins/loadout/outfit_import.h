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
constexpr std::size_t max_bytes = 1024 * 1024, max_sections = 256;
struct Outfit { std::string name; std::vector<outfit_recipe::Input> inputs; outfit_recipe::Recipe recipe; };
struct Snapshot { std::vector<Outfit> outfits; std::size_t skipped_unknown = 0; };
enum class Result { ok, invalid, unsafe_byte, lookup_unavailable, planning_failed, resource_failure };
Result import_bytes(const char* bytes, std::size_t size, bool sq42_lens,
                    outfit_recipe::LookupFn lookup, void* context, Snapshot& output) noexcept;
}
