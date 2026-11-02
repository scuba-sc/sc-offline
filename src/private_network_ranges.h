#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>

// An entire firewall exception must fit within one explicitly private range.
// Shared address space includes Tailscale's 100.64.0.0/10; it is not public IPv4.
namespace offline_network {
inline constexpr bool PrivateRange(uint32_t first, uint32_t last) {
    return first <= last &&
        ((first >= 0x0A000000u && last <= 0x0AFFFFFFu) ||
         (first >= 0x64400000u && last <= 0x647FFFFFu) ||
         (first >= 0x7F000000u && last <= 0x7FFFFFFFu) ||
         (first >= 0xA9FE0000u && last <= 0xA9FEFFFFu) ||
         (first >= 0xAC100000u && last <= 0xAC1FFFFFu) ||
         (first >= 0xC0A80000u && last <= 0xC0A8FFFFu));
}
}
