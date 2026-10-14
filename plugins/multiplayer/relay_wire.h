#pragma once
// The relay contract: the messages sco-relay sends to its players and reads from them, besides
// relay.info (server/relay_info.h). Plain bytes, little-endian, written and read field by field.
// This header uses standard headers only, so a client plugin, the test rig and the tests build
// against exactly the code the server uses, without the server and without sco-core.
//
// Every message starts with a version byte. A reader ignores a message whose version it does not
// know, whose length is not the one that version has, or whose reserved bytes are not zero. A new
// layout gets a new version number; version 1 never changes.
//
// multiplayer.relay   reliable, FROM_HOST, at most 1024 bytes. Who is in the session and where.
//   Sent by the server right after each player joins and again every announce_interval_ms, to every
//   player that registered the channel by then (the same delivery as relay.info). Read it as "still true".
//     offset 0   u8   version      1
//     offset 1   u8   flags        bit 0: nearby-only is on (see below); other bits are 0
//     offset 2   u16  maxPlayers   how many players the server accepts; the server is not counted
//     offset 4   u16  count        how many players follow
//     offset 6   count x { u64 peer id, u16 slot, u8 generation }, 11 bytes each, in slot order
//   A slot is a small number the server hands out, 0 to maxPlayers - 1, the lowest free one. The
//   generation of a slot goes up by one each time someone new takes it, so a message addressed to the
//   slot's earlier occupant can be told apart from one for the present occupant.
//
// Nearby-only (the nearby_only setting, off by default). With it on, a client sends its pose to the
// server on multiplayer.near_up instead of multiplayer.pose, and listens on multiplayer.near.<slot>
// for its own slot instead of multiplayer.pose. The server forwards a pose only to players in the
// same area within nearby_distance_m (and to a player whose own position it does not know, for at
// most nearby_stale_ms). Spawns and despawns keep going to everyone, through the library's relay.
//
// multiplayer.near_up   TO_HOST, unreliable, at most 1048 bytes. Where I am, and my pose.
//     offset 0   u8   version      1
//     offset 1   7 bytes           zero
//     offset 8   u64  area         FNV-1a-64 of the name of the outermost zone of the pose
//     offset 16  f64  x            my position in that zone, in metres
//     offset 24  f64  y
//     offset 32  f64  z
//     offset 40  the pose bytes, 1 to 1008, exactly as the plugin writes them; the server never reads them
//
// multiplayer.near.<slot>   FROM_HOST, unreliable, at most 1024 bytes. Someone else's pose, for the
//   player that holds <slot> (decimal, no leading zero). A client registers only its own slot's channel.
//     offset 0   u8   version      1
//     offset 1   u8   generation   the generation of the receiving slot when the server sent it
//     offset 2   6 bytes           zero
//     offset 8   u64  sender       the peer id of the player whose pose this is
//     offset 16  the pose bytes, as sent
//   A client acts on it only after a multiplayer.relay listed it, and drops a message whose
//   generation is not the one that list gave for its own slot (DecodeNearDown says which).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace sco::relay::wire {

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "positions travel as 8-byte IEEE doubles, as the plugin writes them");

inline constexpr uint8_t kVersion = 1;
inline constexpr uint8_t kFlagNearbyOnly = 0x01;

inline constexpr const char* kRelayChannel = "multiplayer.relay";
inline constexpr const char* kNearUpChannel = "multiplayer.near_up";
inline constexpr const char* kNearDownPrefix = "multiplayer.near.";

// The longest pose the plugin sends (kMaxPose in the plugin's wire header at the pin). The server does
// not read a pose, but it must know how long one can be to register its channels.
inline constexpr size_t kPoseMaxBytes = 1008;

inline constexpr size_t kRelayHeaderBytes = 6;
inline constexpr size_t kRelayEntryBytes = 11;
inline constexpr size_t kRelayMaxBytes = 1024;
inline constexpr size_t kRelayMaxPlayers = (kRelayMaxBytes - kRelayHeaderBytes) / kRelayEntryBytes;

inline constexpr size_t kNearUpHeaderBytes = 40;
inline constexpr size_t kNearUpMaxBytes = kNearUpHeaderBytes + kPoseMaxBytes;
inline constexpr size_t kNearDownHeaderBytes = 16;
inline constexpr size_t kNearDownMaxBytes = kNearDownHeaderBytes + kPoseMaxBytes;
inline constexpr size_t kNearDownGenerationOffset = 1;

// A position farther out than this is not a position (the plugin refuses it too).
inline constexpr double kMaxCoordinateM = 1.0e13;

// The plugin shows players within this distance. nearby_distance_m below it would cut players off
// that the plugin would still show, so the server warns. (unverified: from the plugin's README at the pin)
inline constexpr uint32_t kPluginShowDistanceM = 10000;

// Channel names the relay uses besides the players' own: relay.info, multiplayer.relay,
// multiplayer.near_up, and one multiplayer.near.<slot> per slot. A session holds 256 names in all; a
// plugin may own 32 of them. net_boundary.cpp checks that the two fit.
inline constexpr uint32_t kRelayFixedChannelNames = 3;
inline constexpr uint32_t kPlayerChannelNamesReserved = 32;

// ---- small helpers ---------------------------------------------------------------------------------

namespace detail {

inline void PutU16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFF));
    out->push_back(static_cast<uint8_t>(value >> 8));
}

inline void PutU64(std::vector<uint8_t>* out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out->push_back(static_cast<uint8_t>(value >> (8 * i)));
}

inline void PutDouble(std::vector<uint8_t>* out, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    PutU64(out, bits);
}

inline uint16_t GetU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

inline uint64_t GetU64(const uint8_t* p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
}

inline double GetDouble(const uint8_t* p) {
    const uint64_t bits = GetU64(p);
    double value = 0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

inline bool AllZero(const uint8_t* p, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (p[i] != 0) return false;
    return true;
}

}  // namespace detail

inline bool CoordinateOk(double value) { return std::isfinite(value) && std::fabs(value) <= kMaxCoordinateM; }

// FNV-1a, 64 bits, over the bytes of a name: the area a pose is in. Both ends compute it the same way.
inline constexpr uint64_t Fnv1a64(const char* text, size_t length) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

// "multiplayer.near.7" for slot 7.
inline std::string NearDownChannel(uint32_t slot) { return std::string(kNearDownPrefix) + std::to_string(slot); }

// ---- multiplayer.relay -----------------------------------------------------------------------------

struct RelayEntry {
    uint64_t peer = 0;
    uint16_t slot = 0;
    uint8_t  generation = 0;
};

struct RelayRoster {
    bool nearbyOnly = false;
    uint16_t maxPlayers = 0;
    std::vector<RelayEntry> players;   // in slot order
};

// The message bytes; empty when the roster holds more players than the message can.
inline std::vector<uint8_t> EncodeRelay(const RelayRoster& roster) {
    std::vector<uint8_t> out;
    if (roster.players.size() > kRelayMaxPlayers) return out;
    out.reserve(kRelayHeaderBytes + kRelayEntryBytes * roster.players.size());
    out.push_back(kVersion);
    out.push_back(static_cast<uint8_t>(roster.nearbyOnly ? kFlagNearbyOnly : 0));
    detail::PutU16(&out, roster.maxPlayers);
    detail::PutU16(&out, static_cast<uint16_t>(roster.players.size()));
    for (const RelayEntry& entry : roster.players) {
        detail::PutU64(&out, entry.peer);
        detail::PutU16(&out, entry.slot);
        out.push_back(entry.generation);
    }
    return out;
}

// false for anything that is not a version 1 message: a wrong length (or more than kRelayMaxBytes), an
// unknown flag, a slot beyond maxPlayers, a slot or a peer listed twice, peer 0.
inline bool DecodeRelay(const uint8_t* data, size_t len, RelayRoster* out) {
    if (!data || !out || len < kRelayHeaderBytes || len > kRelayMaxBytes || data[0] != kVersion) return false;
    if ((data[1] & ~kFlagNearbyOnly) != 0) return false;
    RelayRoster roster;
    roster.nearbyOnly = (data[1] & kFlagNearbyOnly) != 0;
    roster.maxPlayers = detail::GetU16(data + 2);
    const size_t count = detail::GetU16(data + 4);
    if (len != kRelayHeaderBytes + kRelayEntryBytes * count || count > roster.maxPlayers) return false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* at = data + kRelayHeaderBytes + kRelayEntryBytes * i;
        RelayEntry entry;
        entry.peer = detail::GetU64(at);
        entry.slot = detail::GetU16(at + 8);
        entry.generation = at[10];
        if (entry.peer == 0 || entry.slot >= roster.maxPlayers) return false;
        for (const RelayEntry& earlier : roster.players)
            if (earlier.peer == entry.peer || earlier.slot == entry.slot) return false;
        roster.players.push_back(entry);
    }
    *out = std::move(roster);
    return true;
}

// ---- multiplayer.near_up ---------------------------------------------------------------------------

struct NearUp {
    uint64_t area = 0;
    double x = 0, y = 0, z = 0;
    const uint8_t* pose = nullptr;   // points into the bytes that were decoded
    size_t poseLen = 0;
};

// The message bytes; empty when the pose is empty or too long, or a coordinate is not a position.
inline std::vector<uint8_t> EncodeNearUp(uint64_t area, double x, double y, double z, const uint8_t* pose,
                                         size_t poseLen) {
    std::vector<uint8_t> out;
    if (!pose || poseLen == 0 || poseLen > kPoseMaxBytes) return out;
    if (!CoordinateOk(x) || !CoordinateOk(y) || !CoordinateOk(z)) return out;
    out.reserve(kNearUpHeaderBytes + poseLen);
    out.push_back(kVersion);
    out.resize(8, 0);
    detail::PutU64(&out, area);
    detail::PutDouble(&out, x);
    detail::PutDouble(&out, y);
    detail::PutDouble(&out, z);
    out.insert(out.end(), pose, pose + poseLen);
    return out;
}

// false for anything that is not a version 1 message with a usable position and a pose of 1 to 1008 bytes.
inline bool DecodeNearUp(const uint8_t* data, size_t len, NearUp* out) {
    if (!data || !out || len <= kNearUpHeaderBytes || len > kNearUpMaxBytes) return false;
    if (data[0] != kVersion || !detail::AllZero(data + 1, 7)) return false;
    NearUp up;
    up.area = detail::GetU64(data + 8);
    up.x = detail::GetDouble(data + 16);
    up.y = detail::GetDouble(data + 24);
    up.z = detail::GetDouble(data + 32);
    if (!CoordinateOk(up.x) || !CoordinateOk(up.y) || !CoordinateOk(up.z)) return false;
    up.pose = data + kNearUpHeaderBytes;
    up.poseLen = len - kNearUpHeaderBytes;
    *out = up;
    return true;
}

// ---- multiplayer.near.<slot> -----------------------------------------------------------------------

struct NearDown {
    uint64_t sender = 0;
    const uint8_t* pose = nullptr;   // points into the bytes that were decoded
    size_t poseLen = 0;
};

// The message bytes; empty when the pose is empty or too long.
inline std::vector<uint8_t> EncodeNearDown(uint8_t generation, uint64_t sender, const uint8_t* pose,
                                           size_t poseLen) {
    std::vector<uint8_t> out;
    if (!pose || poseLen == 0 || poseLen > kPoseMaxBytes) return out;
    out.reserve(kNearDownHeaderBytes + poseLen);
    out.push_back(kVersion);
    out.push_back(generation);
    out.resize(8, 0);
    detail::PutU64(&out, sender);
    out.insert(out.end(), pose, pose + poseLen);
    return out;
}

enum class NearDownVerdict : uint8_t {
    Accepted,
    Malformed,          // not a version 1 message
    OtherGeneration,    // well formed, but for another occupant of the slot: drop it
};

// What a client does with a message on its own slot's channel. ownGeneration is the generation that
// the latest multiplayer.relay gave for the client's own slot.
inline NearDownVerdict DecodeNearDown(const uint8_t* data, size_t len, uint8_t ownGeneration, NearDown* out) {
    if (!data || !out || len <= kNearDownHeaderBytes || len > kNearDownMaxBytes) return NearDownVerdict::Malformed;
    if (data[0] != kVersion || !detail::AllZero(data + 2, 6)) return NearDownVerdict::Malformed;
    if (data[kNearDownGenerationOffset] != ownGeneration) return NearDownVerdict::OtherGeneration;
    NearDown down;
    down.sender = detail::GetU64(data + 8);
    down.pose = data + kNearDownHeaderBytes;
    down.poseLen = len - kNearDownHeaderBytes;
    *out = down;
    return NearDownVerdict::Accepted;
}

}  // namespace sco::relay::wire
