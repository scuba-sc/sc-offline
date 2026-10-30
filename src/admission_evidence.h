#pragma once
// Admission evidence decision core (admission-design.md sections 2.2, 2.3, 2.5, 3, 4).
//
// Header-only, standard library only: no Windows, no sco includes, so it builds and is unit-tested on any
// host. The Windows probe (admission_probe.cpp) fills the plain views below; every decision lives here.
//
// Invariant, enforced everywhere in this file: a field is Clear only when a branch holds positive proof.
// A failed, missing, unreadable or unparseable input yields Unknown, never Clear.
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace admission {

// Clear   = positively checked.
// Present = positive evidence of the bad condition (refuses).
// Unknown = not proven (refuses).
// For Evidence::session: Clear means "Offline" and Present means "Online".
enum class Tri : uint8_t { Unknown, Clear, Present };

inline constexpr long kProfileDomain = 1;   // NET_FW_PROFILE2_DOMAIN
inline constexpr long kProfilePrivate = 2;  // NET_FW_PROFILE2_PRIVATE
inline constexpr long kProfilePublic = 4;   // NET_FW_PROFILE2_PUBLIC
inline constexpr long kProfileMask = 7;
inline constexpr long kProtocolAny = 256;   // NET_FW_IP_PROTOCOL_ANY
inline constexpr int kDirIn = 1;            // NET_FW_RULE_DIR_IN
inline constexpr int kDirOut = 2;           // NET_FW_RULE_DIR_OUT

// One firewall rule as read through INetFwRule. propsOk is false when any property read failed.
struct FwRuleView {
    bool propsOk = false;
    bool enabled = false;
    bool block = false;           // action == NET_FW_ACTION_BLOCK
    int direction = 0;            // kDirIn / kDirOut
    long profiles = 0;            // bitmask; NET_FW_PROFILE2_ALL (0x7FFFFFFF) covers every bit
    long protocol = -1;           // kProtocolAny when unrestricted
    std::wstring appName;
    std::wstring serviceName;
    std::wstring localPorts;
    std::wstring remotePorts;
    std::wstring localAddrs;
    std::wstring remoteAddrs;
    std::wstring interfaceTypes;  // "All" when unrestricted
    bool interfacesEmpty = false; // Interfaces property is empty
};

struct FwSnapshot {
    bool profilesOk = false;      // get_CurrentProfileTypes and every needed get_FirewallEnabled succeeded
    long activeProfiles = 0;      // bitmask of active profiles; 0 = unknown
    long enabledProfiles = 0;     // active profiles whose firewall is positively enabled
    bool rulesOk = false;         // rule enumeration ran to completion
    std::vector<FwRuleView> rules;
    size_t rulesSeen = 0;         // rules enumerated, including ones the probe skipped as irrelevant
    long hr = 0;                  // HRESULT of the first failure, for logging
    const char* stage = "";       // name of the failing (or last) stage, for logging
};

// MIB_TCP_STATE values the decision core cares about.
inline constexpr uint32_t kTcpClosed = 1;
inline constexpr uint32_t kTcpListen = 2;
inline constexpr uint32_t kTcpTimeWait = 11;

struct TcpRow {
    bool v6 = false;
    uint8_t remote[16] = {};      // IPv4: first 4 bytes in network order; IPv6: all 16
    uint32_t state = 0;           // MIB_TCP_STATE
    uint32_t pid = 0;
};

struct TcpSnapshot {
    bool ok = false;              // both address families queried successfully
    long error = 0;               // Win32 error of the first failure, for logging
    const char* stage = "";
    std::vector<TcpRow> rows;     // rows of every process; Session() filters by pid
};

struct V4Range { uint32_t first = 0, last = 0; };
struct U128 {
    uint64_t hi = 0, lo = 0;
    friend bool operator==(const U128& a, const U128& b) { return a.hi == b.hi && a.lo == b.lo; }
    friend bool operator<(const U128& a, const U128& b) { return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo; }
    friend bool operator<=(const U128& a, const U128& b) { return !(b < a); }
};
struct V6Range { U128 first, last; };

struct AddrSet {
    std::vector<V4Range> v4;
    std::vector<V6Range> v6;
};

namespace detail {

inline constexpr U128 kV6Max{~0ull, ~0ull};

inline bool ToAscii(const std::wstring& w, std::string& out) {
    out.clear();
    out.reserve(w.size());
    for (wchar_t c : w) {
        if (static_cast<uint32_t>(c) > 127u) return false;
        out.push_back(static_cast<char>(c));
    }
    return true;
}

inline std::string Trim(const std::string& s) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0, e = s.size();
    while (b < e && ws(s[b])) ++b;
    while (e > b && ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

inline int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 1..3 decimal digits, value <= max.
inline bool ParseSmallUInt(const std::string& s, unsigned max, unsigned& out) {
    if (s.empty() || s.size() > 3) return false;
    unsigned v = 0;
    for (char c : s) {
        if (!IsDigit(c)) return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v > max) return false;
    out = v;
    return true;
}

inline bool ParseV4(const std::string& s, uint32_t& out) {
    uint32_t v = 0;
    int parts = 0;
    size_t i = 0;
    for (;;) {
        const size_t start = i;
        while (i < s.size() && IsDigit(s[i])) ++i;
        unsigned octet = 0;
        if (!ParseSmallUInt(s.substr(start, i - start), 255, octet)) return false;
        v = (v << 8) | octet;
        if (++parts > 4) return false;
        if (i == s.size()) break;
        if (s[i] != '.') return false;
        ++i;
    }
    if (parts != 4) return false;
    out = v;
    return true;
}

// Appends the 16-bit groups of one side of an IPv6 literal. The last piece may be a dotted IPv4 tail.
inline bool ParseV6Groups(const std::string& s, bool allowV4Tail, std::vector<uint16_t>& groups) {
    if (s.empty()) return true;
    size_t pos = 0;
    for (;;) {
        const size_t colon = s.find(':', pos);
        const bool last = colon == std::string::npos;
        const std::string piece = s.substr(pos, last ? std::string::npos : colon - pos);
        if (piece.empty()) return false;
        if (piece.find('.') != std::string::npos) {
            uint32_t v4 = 0;
            if (!last || !allowV4Tail || !ParseV4(piece, v4)) return false;
            groups.push_back(static_cast<uint16_t>(v4 >> 16));
            groups.push_back(static_cast<uint16_t>(v4 & 0xFFFF));
        } else {
            if (piece.size() > 4) return false;
            unsigned v = 0;
            for (char c : piece) {
                const int h = HexVal(c);
                if (h < 0) return false;
                v = v * 16 + static_cast<unsigned>(h);
            }
            groups.push_back(static_cast<uint16_t>(v));
        }
        if (last) break;
        pos = colon + 1;
    }
    return true;
}

inline bool ParseV6(const std::string& s, U128& out) {
    if (s.empty()) return false;
    const size_t dbl = s.find("::");
    uint16_t g[8] = {};
    if (dbl == std::string::npos) {
        std::vector<uint16_t> all;
        if (!ParseV6Groups(s, true, all) || all.size() != 8) return false;
        for (size_t i = 0; i < 8; ++i) g[i] = all[i];
    } else {
        if (s.find("::", dbl + 1) != std::string::npos) return false;  // second "::" (also ":::")
        std::vector<uint16_t> head, tail;
        if (!ParseV6Groups(s.substr(0, dbl), false, head)) return false;
        if (!ParseV6Groups(s.substr(dbl + 2), true, tail)) return false;
        if (head.size() + tail.size() > 7) return false;
        for (size_t i = 0; i < head.size(); ++i) g[i] = head[i];
        for (size_t i = 0; i < tail.size(); ++i) g[8 - tail.size() + i] = tail[i];
    }
    U128 v;
    for (int i = 0; i < 4; ++i) v.hi = (v.hi << 16) | g[i];
    for (int i = 4; i < 8; ++i) v.lo = (v.lo << 16) | g[i];
    out = v;
    return true;
}

inline V6Range V6Prefix(const U128& addr, unsigned len) {
    U128 maskv;
    if (len == 0) {
    } else if (len <= 64) {
        maskv.hi = ~0ull << (64 - len);
    } else {
        maskv.hi = ~0ull;
        maskv.lo = ~0ull << (128 - len);
    }
    V6Range r;
    r.first = {addr.hi & maskv.hi, addr.lo & maskv.lo};
    r.last = {r.first.hi | ~maskv.hi, r.first.lo | ~maskv.lo};
    return r;
}

inline bool ParseToken(const std::string& tok, AddrSet& out) {
    if (tok.empty()) return false;
    if (tok == "*") {
        out.v4.push_back({0u, 0xFFFFFFFFu});
        out.v6.push_back({U128{}, kV6Max});
        return true;
    }
    const size_t dash = tok.find('-');
    const size_t slash = tok.find('/');
    if (dash != std::string::npos && slash != std::string::npos) return false;
    if (tok.find(':') != std::string::npos) {
        if (dash != std::string::npos) {
            U128 a, b;
            if (!ParseV6(tok.substr(0, dash), a) || !ParseV6(tok.substr(dash + 1), b) || b < a) return false;
            out.v6.push_back({a, b});
        } else if (slash != std::string::npos) {
            U128 a;
            unsigned len = 0;
            if (!ParseV6(tok.substr(0, slash), a) || !ParseSmallUInt(tok.substr(slash + 1), 128, len)) return false;
            out.v6.push_back(V6Prefix(a, len));
        } else {
            U128 a;
            if (!ParseV6(tok, a)) return false;
            out.v6.push_back({a, a});
        }
        return true;
    }
    if (dash != std::string::npos) {
        uint32_t a = 0, b = 0;
        if (!ParseV4(tok.substr(0, dash), a) || !ParseV4(tok.substr(dash + 1), b) || b < a) return false;
        out.v4.push_back({a, b});
    } else if (slash != std::string::npos) {
        uint32_t a = 0;
        if (!ParseV4(tok.substr(0, slash), a)) return false;
        const std::string m = tok.substr(slash + 1);
        uint32_t mask = 0;
        if (m.find('.') != std::string::npos) {
            if (!ParseV4(m, mask)) return false;
            const uint32_t inv = ~mask;
            if ((inv & (inv + 1u)) != 0) return false;  // non-contiguous mask
        } else {
            unsigned len = 0;
            if (!ParseSmallUInt(m, 32, len)) return false;
            mask = len == 0 ? 0u : (~0u << (32 - len));
        }
        const uint32_t first = a & mask;
        out.v4.push_back({first, first | ~mask});
    } else {
        uint32_t a = 0;
        if (!ParseV4(tok, a)) return false;
        out.v4.push_back({a, a});
    }
    return true;
}

inline std::wstring NormalizePathImpl(const std::wstring& p) {
    std::wstring s;
    size_t b = 0, e = p.size();
    while (b < e && (p[b] == L' ' || p[b] == L'\t')) ++b;
    while (e > b && (p[e - 1] == L' ' || p[e - 1] == L'\t')) --e;
    for (size_t i = b; i < e; ++i) s.push_back(p[i] == L'/' ? L'\\' : p[i]);
    if (s.size() >= 4 && s.compare(0, 4, L"\\\\?\\") == 0) s.erase(0, 4);
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    return s;
}

inline bool IsAnyPorts(const std::wstring& p) { return p.empty() || p == L"*"; }

inline bool EqualsNoCase(const std::wstring& a, const wchar_t* lit) {
    std::wstring b(lit);
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(static_cast<wint_t>(a[i])) != std::towlower(static_cast<wint_t>(b[i]))) return false;
    }
    return true;
}

}

// Parses a Windows Firewall RemoteAddresses string: a comma list of "*", a single address, "a-b" range,
// "addr/len" or "addr/mask" subnet, for IPv4 and IPv6. Any other token (LocalSubnet, Internet, ...), an empty
// token, or a malformed one makes the WHOLE string fail; on failure `out` is left untouched.
inline bool ParseRemoteAddresses(const std::wstring& text, AddrSet& out) {
    std::string s;
    if (!detail::ToAscii(text, s)) return false;
    AddrSet tmp;
    size_t pos = 0;
    for (;;) {
        const size_t comma = s.find(',', pos);
        const std::string tok = detail::Trim(s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (!detail::ParseToken(tok, tmp)) return false;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    out.v4.insert(out.v4.end(), tmp.v4.begin(), tmp.v4.end());
    out.v6.insert(out.v6.end(), tmp.v6.begin(), tmp.v6.end());
    return true;
}

// Only launcher-shaped declarations can exempt additional IPv4 space. Parse atomically:
// no wildcards, ranges, IPv6, dotted masks, host bits or partial-list acceptance.
inline bool ParseConfiguredAllow(const std::wstring& text, std::vector<V4Range>& out) {
    if (text.size() > 255) return false;
    std::string s;
    if (!detail::ToAscii(text, s)) return false;
    std::vector<V4Range> parsed;
    if (!s.empty()) {
        size_t pos = 0;
        for (;;) {
            const size_t comma = s.find(',', pos);
            const std::string tok = detail::Trim(s.substr(pos, comma == std::string::npos ? comma : comma - pos));
            const size_t slash = tok.find('/');
            uint32_t ip = 0;
            unsigned len = 0;
            if (slash == std::string::npos || !detail::ParseV4(tok.substr(0, slash), ip) ||
                !detail::ParseSmallUInt(tok.substr(slash + 1), 32, len) || len < 8 || parsed.size() >= 8)
                return false;
            const uint32_t mask = ~0u << (32 - len);
            if (ip & ~mask) return false;
            const std::string canonical = std::to_string(ip >> 24) + "." +
                std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." +
                std::to_string(ip & 255) + "/" + std::to_string(len);
            if (s.substr(pos, comma == std::string::npos ? comma : comma - pos) != canonical) return false;
            parsed.push_back({ip, ip | ~mask});
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    out = std::move(parsed);
    return true;
}

// True iff blocked IPv4 plus explicit allowed ranges leave gaps only in
// {10/8, 127/8, 169.254/16, 172.16/12, 192.168/16}, AND IPv6 is covered in full.
inline bool CoverageOk(const AddrSet& s, const std::vector<V4Range>& allowed = {}) {
    static constexpr V4Range kPrivate[] = {{0x0A000000u, 0x0AFFFFFFu}, {0x7F000000u, 0x7FFFFFFFu},
                                           {0xA9FE0000u, 0xA9FEFFFFu}, {0xAC100000u, 0xAC1FFFFFu},
                                           {0xC0A80000u, 0xC0A8FFFFu}};
    auto gapInPrivate = [](uint64_t lo, uint64_t hi) {
        for (const V4Range& p : kPrivate) {
            if (p.first <= lo && hi <= p.last) return true;
        }
        return false;
    };
    std::vector<V4Range> v4 = s.v4;
    // Treat only the declared exceptions as covered, then retain the original
    // private-gap and full-IPv6 proof. Sorting handles overlapping/adjacent CIDRs.
    v4.insert(v4.end(), allowed.begin(), allowed.end());
    std::sort(v4.begin(), v4.end(), [](const V4Range& a, const V4Range& b) { return a.first < b.first; });
    uint64_t next = 0;
    for (const V4Range& r : v4) {
        if (r.first > next && !gapInPrivate(next, static_cast<uint64_t>(r.first) - 1)) return false;
        next = std::max<uint64_t>(next, static_cast<uint64_t>(r.last) + 1);
    }
    if (next <= 0xFFFFFFFFull && !gapInPrivate(next, 0xFFFFFFFFull)) return false;

    std::vector<V6Range> v6 = s.v6;
    if (v6.empty()) return false;
    std::sort(v6.begin(), v6.end(), [](const V6Range& a, const V6Range& b) { return a.first < b.first; });
    U128 need{};
    for (const V6Range& r : v6) {
        if (need < r.first) return false;  // gap before this range
        if (r.last == detail::kV6Max) return true;
        if (need <= r.last) {
            need = r.last;
            if (++need.lo == 0) ++need.hi;
        }
    }
    return false;
}

inline std::wstring NormalizePath(const std::wstring& p) { return detail::NormalizePathImpl(p); }

// Case-insensitive, slash-normalised comparison. An empty path never matches.
inline bool SamePath(const std::wstring& a, const std::wstring& b) {
    const std::wstring x = detail::NormalizePathImpl(a);
    return !x.empty() && x == detail::NormalizePathImpl(b);
}

struct FwDetail {
    int countingIn = 0, countingOut = 0;
    bool coveredIn = false, coveredOut = false;
};

// A rule counts only if it positively blocks everything for this exact exe in `direction`.
inline bool RuleCounts(const FwRuleView& r, const FwSnapshot& snap, const std::wstring& exePath, int direction) {
    if (!r.propsOk || !r.enabled || !r.block) return false;
    if (r.direction != direction) return false;
    if (!SamePath(r.appName, exePath)) return false;
    if ((r.profiles & snap.activeProfiles) != snap.activeProfiles) return false;
    if (r.protocol != kProtocolAny) return false;
    if (!detail::IsAnyPorts(r.localPorts) || !detail::IsAnyPorts(r.remotePorts)) return false;
    if (!r.serviceName.empty()) return false;
    if (!detail::EqualsNoCase(r.interfaceTypes, L"All") || !r.interfacesEmpty) return false;
    // A local-address restriction would block only part of the traffic; the launcher never sets one.
    if (!detail::IsAnyPorts(r.localAddrs)) return false;
    return true;
}

// Clear iff the firewall is on for every active profile and BOTH directions are covered for this exe.
// Otherwise Unknown (never Present: this contract proves a block, it does not prove reachability).
inline Tri OfficialNetwork(const FwSnapshot& snap, const std::wstring& exePath, FwDetail* detail = nullptr,
                           const std::wstring& configuredAllow = {}) {
    FwDetail local;
    FwDetail& d = detail ? *detail : local;
    d = FwDetail{};
    std::vector<V4Range> allowed;
    if (!ParseConfiguredAllow(configuredAllow, allowed)) return Tri::Unknown;
    if (!snap.profilesOk || !snap.rulesOk || exePath.empty()) return Tri::Unknown;
    if ((snap.activeProfiles & kProfileMask) == 0 || (snap.activeProfiles & ~kProfileMask) != 0) return Tri::Unknown;
    if ((snap.enabledProfiles & snap.activeProfiles) != snap.activeProfiles) return Tri::Unknown;
    AddrSet in, out;
    for (const FwRuleView& r : snap.rules) {
        for (int dir : {kDirIn, kDirOut}) {
            if (!RuleCounts(r, snap, exePath, dir)) continue;
            AddrSet parsed;
            if (!ParseRemoteAddresses(r.remoteAddrs, parsed)) continue;  // unparseable: ignored, not assumed
            AddrSet& target = dir == kDirIn ? in : out;
            target.v4.insert(target.v4.end(), parsed.v4.begin(), parsed.v4.end());
            target.v6.insert(target.v6.end(), parsed.v6.begin(), parsed.v6.end());
            ++(dir == kDirIn ? d.countingIn : d.countingOut);
        }
    }
    d.coveredIn = CoverageOk(in, allowed);
    d.coveredOut = CoverageOk(out, allowed);
    return d.coveredIn && d.coveredOut ? Tri::Clear : Tri::Unknown;
}

inline bool IsPrivateV4(uint32_t a) {
    return (a >> 24) == 10 || (a >> 24) == 127 || (a >> 16) == 0xA9FE || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8;
}

inline bool RemoteIsPrivate(const TcpRow& r) {
    auto be32 = [](const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | p[3];
    };
    if (!r.v6) return IsPrivateV4(be32(r.remote));
    bool zeros10 = true;
    for (int i = 0; i < 10; ++i) zeros10 = zeros10 && r.remote[i] == 0;
    if (zeros10 && r.remote[10] == 0xFF && r.remote[11] == 0xFF) return IsPrivateV4(be32(r.remote + 12));  // v4-mapped
    bool zeros15 = true;
    for (int i = 0; i < 15; ++i) zeros15 = zeros15 && r.remote[i] == 0;
    if (zeros15 && r.remote[15] == 1) return true;                                // ::1
    if (r.remote[0] == 0xFE && (r.remote[1] & 0xC0) == 0x80) return true;         // fe80::/10
    return false;  // every other IPv6 remote, including :: and ULA, is treated as not private
}

// Clear (Offline) iff officialNetwork is Clear, the TCP table was read, and this process owns no live TCP
// connection to a non-private remote. Present (Online) is positive evidence of such a connection. Anything
// else is Unknown. UDP has no remote endpoint in the table; the firewall block is its backstop (design 2.3).
inline Tri Session(const TcpSnapshot& tcp, Tri officialNetwork, uint32_t pid) {
    if (!tcp.ok || pid == 0) return Tri::Unknown;
    for (const TcpRow& r : tcp.rows) {
        if (r.pid != pid) continue;
        if (r.state == kTcpListen || r.state == kTcpTimeWait || r.state == kTcpClosed) continue;
        if (!RemoteIsPrivate(r)) return Tri::Present;
    }
    return officialNetwork == Tri::Clear ? Tri::Clear : Tri::Unknown;
}

struct Evidence {
    Tri session = Tri::Unknown;
    Tri officialNetwork = Tri::Unknown;
    Tri antiCheat = Tri::Unknown;
    Tri targetProcess = Tri::Unknown;
    bool admitted() const {
        return session == Tri::Clear && officialNetwork == Tri::Clear && antiCheat == Tri::Clear &&
               targetProcess == Tri::Clear;
    }
};

// antiCheat and targetProcess come from the shell's existing inspections; session and officialNetwork are
// derived here. admitted() holds only when all four are positively Clear.
inline Evidence Decide(Tri antiCheat, Tri targetProcess, const FwSnapshot& fw, const std::wstring& exePath,
                       const TcpSnapshot& tcp, uint32_t pid, FwDetail* detail = nullptr,
                       const std::wstring& configuredAllow = {}) {
    Evidence e;
    e.antiCheat = antiCheat;
    e.targetProcess = targetProcess;
    e.officialNetwork = OfficialNetwork(fw, exePath, detail, configuredAllow);
    e.session = Session(tcp, e.officialNetwork, pid);
    return e;
}

inline constexpr uint64_t kDefaultTtlMs = 1000;

// Positive-only cache of the composite evidence. Only an admitted() result is stored, for ttlMs on the
// injected monotonic clock (aged from the moment its probe started). Unknown/Present/Online are never cached,
// so a refusal is never sticky. One mutex, single-flight refresh, thread_local re-entry guard.
class EvidenceCache {
public:
    using Clock = std::function<uint64_t()>;  // monotonic milliseconds
    using Probe = std::function<Evidence()>;

    EvidenceCache(Clock clock, Probe probe, uint64_t ttlMs = kDefaultTtlMs)
        : clock_(std::move(clock)), probe_(std::move(probe)), ttl_(ttlMs) {}
    EvidenceCache(const EvidenceCache&) = delete;
    EvidenceCache& operator=(const EvidenceCache&) = delete;

    Evidence Get() { return Run(false, Tri::Unknown); }

    // targetProcess is cheap and read fresh on every call by the caller: a cached entry is used only while its
    // targetProcess equals `freshTarget`, otherwise it is dropped; the result carries `freshTarget`.
    Evidence Get(Tri freshTarget) { return Run(true, freshTarget); }

    void Invalidate() {
        std::lock_guard<std::mutex> lk(m_);
        ++epoch_;
        valid_ = false;
    }

    // Test seam: called under the lock when a caller starts waiting for an in-flight probe.
    void SetWaitObserver(std::function<void()> f) {
        std::lock_guard<std::mutex> lk(m_);
        onWait_ = std::move(f);
    }

private:
    static bool& InProbe() {
        static thread_local bool v = false;
        return v;
    }
    static Evidence WithTarget(Evidence e, bool have, Tri t) {
        if (have) e.targetProcess = t;
        return e;
    }

    Evidence Run(bool haveTarget, Tri target) {
        if (InProbe()) return Evidence{};  // same-thread re-entry: refuse instead of deadlocking
        if (!clock_ || !probe_) return Evidence{};
        std::unique_lock<std::mutex> lk(m_);
        for (;;) {
            const uint64_t now = clock_();
            if (valid_) {
                if (now >= at_ && now - at_ < ttl_ && !(haveTarget && target != cached_.targetProcess)) {
                    return WithTarget(cached_, haveTarget, target);
                }
                valid_ = false;
            }
            if (inflight_) {
                const uint64_t id = started_;
                if (onWait_) onWait_();
                cv_.wait(lk, [&] { return completed_ >= id; });
                if (!flightStale_) return WithTarget(flightResult_, haveTarget, target);
                continue;  // invalidated while that probe ran: its result may predate the invalidation
            }
            const uint64_t id = ++started_;
            const uint64_t epoch = epoch_;
            inflight_ = true;
            lk.unlock();
            Evidence ev;
            InProbe() = true;
            try {
                ev = probe_();
            } catch (...) {
                ev = Evidence{};
            }
            InProbe() = false;
            lk.lock();
            inflight_ = false;
            completed_ = id;
            flightStale_ = epoch_ != epoch;
            flightResult_ = ev;
            if (ev.admitted() && !flightStale_) {
                valid_ = true;
                cached_ = ev;
                at_ = now;
            } else {
                valid_ = false;
            }
            cv_.notify_all();
            return WithTarget(ev, haveTarget, target);
        }
    }

    Clock clock_;
    Probe probe_;
    uint64_t ttl_;
    std::mutex m_;
    std::condition_variable cv_;
    std::function<void()> onWait_;
    bool valid_ = false;
    Evidence cached_;
    uint64_t at_ = 0;
    bool inflight_ = false;
    uint64_t started_ = 0, completed_ = 0, epoch_ = 0;
    bool flightStale_ = false;
    Evidence flightResult_;
};

}
