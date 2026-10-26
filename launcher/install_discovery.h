// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace sco::launcher::discovery {

struct DirectoryIdentity {
    bool valid = false;
    uint64_t volume = 0;
    std::array<uint8_t, 16> file{};
};

struct InstallIdentity {
    DirectoryIdentity channel;
    DirectoryIdentity bin;
};

struct Found {
    std::wstring bin;
    const char* how;
};

inline bool Equal(const DirectoryIdentity& a, const DirectoryIdentity& b) {
    return a.volume == b.volume && a.file == b.file;
}

inline bool SameInstall(const std::wstring& a, const InstallIdentity& x,
                        const std::wstring& b, const InstallIdentity& y) {
    if (a.empty() || b.empty()) return false;
    if (x.channel.valid && y.channel.valid && !Equal(x.channel, y.channel)) return false;
    if (x.bin.valid && y.bin.valid && !Equal(x.bin, y.bin)) return false;
    if (x.channel.valid && y.channel.valid && x.bin.valid && y.bin.valid) return true;
#ifdef _WIN32
    return ::_wcsicmp(a.c_str(), b.c_str()) == 0;
#else
    return ::wcscasecmp(a.c_str(), b.c_str()) == 0;
#endif
}

using IdentityReader = InstallIdentity (*)(const std::wstring&);

inline void AppendUnique(std::vector<Found>& out, const std::wstring& bin,
                         const char* how, IdentityReader read) {
    if (bin.empty()) return;
    const auto identity = read(bin);
    for (const auto& existing : out)
        if (SameInstall(existing.bin, read(existing.bin), bin, identity)) return;
    out.push_back({bin, how});
}

} // namespace sco::launcher::discovery
