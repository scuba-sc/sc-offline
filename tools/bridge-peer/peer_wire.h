// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// This independent endpoint uses MIT wire declarations only, never sco_api/services.
#include <sc_ipc.h>
#include <sc_titanlink.h>
#include <sc_voxel_bridge.h>
#include <cmath>
#include <cstring>

inline bool PeerName(const char* text, size_t extent) {
    if (!text[0]) return false;
    for (size_t i = 0; i < extent; ++i) {
        const char c = text[i];
        if (!c) return true;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return false;
}
inline bool PeerStartValid(const tl_msg_start_match& m) {
    return PeerName(m.map, sizeof(m.map)) && PeerName(m.mode, sizeof(m.mode));
}
inline bool PeerTitanValid(const tl_msg_call_titan& m) {
    for (float v : m.drop) if (!std::isfinite(v) || std::fabs(v) > 1000000) return false;
    return std::isfinite(m.yaw) && std::fabs(m.yaw) <= 1000000;
}
inline bool PeerGroundValid(const void* bytes, uint32_t size) {
    if (size < sizeof(vx_msg_ground)) return false;
    vx_msg_ground m{};
    std::memcpy(&m, bytes, sizeof(m));
    if (m.count > 64 || size != sizeof(m) + m.count * sizeof(vx_ground)) return false;
    const auto* data = static_cast<const unsigned char*>(bytes) + sizeof(m);
    for (uint32_t i = 0; i < m.count; ++i) {
        vx_ground g{};
        std::memcpy(&g, data + i * sizeof(g), sizeof(g));
        if (!std::isfinite(g.top) || g.reserved) return false;
    }
    return true;
}
