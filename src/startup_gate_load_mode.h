#pragma once

#include <cstddef>
#include <cstdint>

// Startup gate load-mode policy. A module that the process executable imports is
// retained by the loader before DllMain runs, so an unbalanced FreeLibrary cannot
// unmap it; nothing DLL-resident is published while the module could still be
// released. A dynamically loaded copy (injection, late LoadLibrary) has no such
// retention and is refused before any patch, event, thread or callback exists.
// This does not by itself establish native reader/worker exclusion or drain.
namespace sc_offline::startup_gate_load_mode {

enum class LoadMode {
    StaticImport,
    Dynamic,
};

enum class Decision {
    Retained,
    RefusedDynamicLoad,
    RefusedNotImported,
};

// DllMain's lpvReserved is non-null for a static (import table) load and null for
// a dynamic load.
constexpr LoadMode FromAttachReserved(const void* reserved) noexcept {
    return reserved != nullptr ? LoadMode::StaticImport : LoadMode::Dynamic;
}

// Operations supplies ProcessImageImportsModule(). It is not consulted for a
// dynamic load, so a refused load does no further work.
template <class Operations>
Decision Evaluate(LoadMode mode, Operations& operations) {
    if (mode == LoadMode::Dynamic) return Decision::RefusedDynamicLoad;
    return operations.ProcessImageImportsModule() ? Decision::Retained
                                                  : Decision::RefusedNotImported;
}

constexpr char FoldAscii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// ASCII-only, null-safe. Two null pointers are not a match.
constexpr bool EqualsAsciiNoCase(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) return false;
    for (;; ++a, ++b) {
        if (FoldAscii(*a) != FoldAscii(*b)) return false;
        if (*a == '\0') return true;
    }
}

constexpr std::size_t kMaxImportDescriptors = 4096;
constexpr std::size_t kImportDescriptorSize = 20;  // IMAGE_IMPORT_DESCRIPTOR
constexpr std::size_t kImportNameFieldOffset = 12; // its Name RVA

namespace detail {
constexpr std::uint32_t ReadU32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
} // namespace detail

// Walks the import directory of an in-memory (mapped) image, where RVA equals byte
// offset. Never reads at or beyond imageSize. Malformed or overflowing offsets, a
// missing terminator within the directory, and unterminated names return false.
inline bool ImportTableNames(const unsigned char* image, std::size_t imageSize,
                             std::uint32_t directoryRva, std::uint32_t directorySize,
                             const char* moduleFileName) noexcept {
    if (image == nullptr || moduleFileName == nullptr || directorySize == 0) return false;
    const std::uint64_t begin = directoryRva;
    const std::uint64_t end = begin + directorySize;
    if (end > imageSize) return false;

    for (std::size_t i = 0; i < kMaxImportDescriptors; ++i) {
        const std::uint64_t at = begin + static_cast<std::uint64_t>(i) * kImportDescriptorSize;
        if (at + kImportDescriptorSize > end) return false;
        const std::uint32_t nameRva = detail::ReadU32(image + at + kImportNameFieldOffset);
        if (nameRva == 0) return false; // terminating descriptor
        if (nameRva >= imageSize) return false;
        std::size_t length = 0;
        while (nameRva + length < imageSize && image[nameRva + length] != 0) ++length;
        if (nameRva + length >= imageSize) return false; // unterminated at image end
        if (EqualsAsciiNoCase(reinterpret_cast<const char*>(image + nameRva), moduleFileName))
            return true;
    }
    return false;
}

} // namespace sc_offline::startup_gate_load_mode
