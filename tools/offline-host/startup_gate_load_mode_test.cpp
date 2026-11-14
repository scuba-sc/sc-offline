// SPDX-License-Identifier: GPL-3.0-only
#include "startup_gate_load_mode.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lm = sc_offline::startup_gate_load_mode;

struct FakeOperations {
    bool imported = false;
    unsigned calls = 0;
    bool ProcessImageImportsModule() {
        ++calls;
        return imported;
    }
};

static bool ReservedMapsToLoadMode() {
    int marker = 0;
    return lm::FromAttachReserved(nullptr) == lm::LoadMode::Dynamic &&
           lm::FromAttachReserved(&marker) == lm::LoadMode::StaticImport;
}

static bool DynamicNeverCallsOperation() {
    FakeOperations operations;
    operations.imported = true;
    return lm::Evaluate(lm::LoadMode::Dynamic, operations) == lm::Decision::RefusedDynamicLoad &&
           operations.calls == 0;
}

static bool StaticImportDecisions() {
    FakeOperations imported;
    imported.imported = true;
    if (lm::Evaluate(lm::LoadMode::StaticImport, imported) != lm::Decision::Retained ||
        imported.calls != 1) return false;
    FakeOperations missing;
    return lm::Evaluate(lm::LoadMode::StaticImport, missing) == lm::Decision::RefusedNotImported &&
           missing.calls == 1;
}

static bool NameComparison() {
    return lm::EqualsAsciiNoCase("dinput8.dll", "DINPUT8.DLL") &&
           lm::EqualsAsciiNoCase("", "") &&
           !lm::EqualsAsciiNoCase("dinput8.dll", "dinput8.dl") &&
           !lm::EqualsAsciiNoCase("dinput8", "dinput8.dll") &&
           !lm::EqualsAsciiNoCase(nullptr, "a") && !lm::EqualsAsciiNoCase("a", nullptr) &&
           !lm::EqualsAsciiNoCase(nullptr, nullptr) &&
           !lm::EqualsAsciiNoCase("a[", "A{");  // folding stays inside A-Z
}

// Synthetic image: descriptors at directoryRva, then names appended after them.
struct Image {
    std::vector<unsigned char> bytes;
    std::uint32_t directoryRva = 0x20;
    std::uint32_t directorySize = 0;
};

static void PutU32(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
}

static Image BuildImage(const std::vector<const char*>& names) {
    Image image;
    image.directorySize = static_cast<std::uint32_t>((names.size() + 1) * lm::kImportDescriptorSize);
    std::size_t nameAt = image.directoryRva + image.directorySize;
    image.bytes.assign(nameAt, 0);
    for (std::size_t i = 0; i < names.size(); ++i) {
        PutU32(image.bytes, image.directoryRva + i * lm::kImportDescriptorSize + lm::kImportNameFieldOffset,
               static_cast<std::uint32_t>(nameAt));
        const std::size_t length = std::strlen(names[i]) + 1;
        image.bytes.insert(image.bytes.end(), names[i], names[i] + length);
        nameAt += length;
    }
    return image;
}

static bool Walk(const Image& image, const char* name) {
    return lm::ImportTableNames(image.bytes.data(), image.bytes.size(), image.directoryRva,
                                image.directorySize, name);
}

static bool WalkerFindsNameIgnoringCase() {
    const Image image = BuildImage({"KERNEL32.dll", "DINPUT8.dll"});
    return Walk(image, "dinput8.dll") && Walk(image, "kernel32.DLL");
}

static bool WalkerRejectsAbsentName() {
    const Image image = BuildImage({"KERNEL32.dll", "USER32.dll"});
    return !Walk(image, "dinput8.dll") && !Walk(BuildImage({}), "dinput8.dll");
}

static bool WalkerRejectsMalformedOffsets() {
    const Image image = BuildImage({"DINPUT8.dll"});
    // Directory beyond the image, and overflowing RVA/size.
    // A valid matching directory that lies wholly past the declared image size must not be
    // found: the buffer is larger than the size passed, so an out-of-bounds read would match.
    if (lm::ImportTableNames(image.bytes.data(), image.directoryRva, image.directoryRva,
                             image.directorySize, "dinput8.dll")) return false;
    if (lm::ImportTableNames(image.bytes.data(), image.directoryRva + image.directorySize - 1,
                             image.directoryRva, image.directorySize, "dinput8.dll")) return false;
    // Name inside the declared size, directory just past it: only the directory bound
    // can refuse this, and the bytes beyond the declared size would otherwise match.
    std::vector<unsigned char> split(0x80, 0);
    std::memcpy(split.data() + 4, "DINPUT8.dll", 12);
    PutU32(split, 0x40 + lm::kImportNameFieldOffset, 4);
    if (lm::ImportTableNames(split.data(), 0x40, 0x40, static_cast<std::uint32_t>(2 * lm::kImportDescriptorSize), "dinput8.dll")) return false;
    if (!lm::ImportTableNames(split.data(), split.size(), 0x40, static_cast<std::uint32_t>(2 * lm::kImportDescriptorSize), "dinput8.dll")) return false;
    Image beyond = image;
    beyond.directoryRva = static_cast<std::uint32_t>(image.bytes.size());
    if (Walk(beyond, "dinput8.dll")) return false;
    Image overflow = image;
    overflow.directoryRva = 0xFFFFFFF0u;
    overflow.directorySize = 0xFFFFFFF0u;
    if (Walk(overflow, "dinput8.dll")) return false;
    // Name RVA outside the image.
    Image badName = image;
    PutU32(badName.bytes, badName.directoryRva + lm::kImportNameFieldOffset,
           static_cast<std::uint32_t>(badName.bytes.size() + 8));
    if (Walk(badName, "dinput8.dll")) return false;
    PutU32(badName.bytes, badName.directoryRva + lm::kImportNameFieldOffset, 0xFFFFFFFFu);
    if (Walk(badName, "dinput8.dll")) return false;
    // Directory truncated mid-descriptor.
    Image truncated = image;
    truncated.directorySize = static_cast<std::uint32_t>(lm::kImportDescriptorSize - 4);
    if (Walk(truncated, "dinput8.dll")) return false;
    // Image truncated so the descriptor itself is cut off, and null arguments.
    return !lm::ImportTableNames(image.bytes.data(), image.directoryRva + 8, image.directoryRva,
                                 static_cast<std::uint32_t>(lm::kImportDescriptorSize), "dinput8.dll") &&
           !lm::ImportTableNames(nullptr, 0, 0, 20, "dinput8.dll") &&
           !lm::ImportTableNames(image.bytes.data(), image.bytes.size(), image.directoryRva,
                                 image.directorySize, nullptr);
}

static bool WalkerRejectsZeroSizeDirectory() {
    Image image = BuildImage({"DINPUT8.dll"});
    image.directorySize = 0;
    return !Walk(image, "dinput8.dll");
}

static bool WalkerRejectsUnterminatedNameAtImageEnd() {
    Image image = BuildImage({"DINPUT8.dll"});
    image.bytes.back() = 'x';  // drop the NUL; the name now runs to the end of the image
    return !Walk(image, "dinput8.dll") && !Walk(image, "DINPUT8.dllx");
}

static bool WalkerIsBoundedByDescriptorCap() {
    // A directory larger than the cap with no terminator and no match returns false,
    // and a match placed past the cap is not found.
    const std::size_t count = lm::kMaxImportDescriptors + 8;
    Image image;
    image.directorySize = static_cast<std::uint32_t>(count * lm::kImportDescriptorSize);
    const std::size_t nameAt = image.directoryRva + image.directorySize;
    image.bytes.assign(nameAt + 32, 0);
    std::memcpy(image.bytes.data() + nameAt, "OTHER.dll", 10);
    std::memcpy(image.bytes.data() + nameAt + 10, "LATE.dll", 9);
    for (std::size_t i = 0; i < count; ++i)
        PutU32(image.bytes, image.directoryRva + i * lm::kImportDescriptorSize + lm::kImportNameFieldOffset,
               static_cast<std::uint32_t>(nameAt));
    if (Walk(image, "dinput8.dll")) return false;
    PutU32(image.bytes,
           image.directoryRva + (lm::kMaxImportDescriptors + 1) * lm::kImportDescriptorSize +
               lm::kImportNameFieldOffset,
           static_cast<std::uint32_t>(nameAt + 10));
    return !Walk(image, "late.dll");
}

int main() {
    if (!ReservedMapsToLoadMode()) return 1;
    if (!DynamicNeverCallsOperation()) return 2;
    if (!StaticImportDecisions()) return 3;
    if (!NameComparison()) return 4;
    if (!WalkerFindsNameIgnoringCase()) return 5;
    if (!WalkerRejectsAbsentName()) return 6;
    if (!WalkerRejectsMalformedOffsets()) return 7;
    if (!WalkerRejectsZeroSizeDirectory()) return 8;
    if (!WalkerRejectsUnterminatedNameAtImageEnd()) return 9;
    if (!WalkerIsBoundedByDescriptorCap()) return 10;
    return 0;
}
