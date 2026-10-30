#include "admission_evidence.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace admission;

namespace {

int g_checks = 0;
int g_failed = 0;
const char* g_case = "";

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failed;                                                                   \
            std::fprintf(stderr, "FAIL [%s] line %d: %s\n", g_case, __LINE__, #cond);     \
        }                                                                                 \
    } while (0)

struct CaseEntry {
    const char* name;
    void (*fn)();
};
std::vector<CaseEntry>& Cases() {
    static std::vector<CaseEntry> v;
    return v;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { Cases().push_back({n, f}); }
};
#define CASE(name)                          \
    void name();                            \
    const Registrar registrar_##name(#name, name); \
    void name()

const std::wstring kExe = L"C:\\Games\\StarCitizen\\Bin64\\StarCitizen.exe";

const std::wstring kLauncherVector =
    L"0.0.0.0-9.255.255.255,11.0.0.0-126.255.255.255,128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,"
    L"172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff";

const V4Range kComplement[] = {{0x00000000u, 0x09FFFFFFu}, {0x0B000000u, 0x7EFFFFFFu}, {0x80000000u, 0xA9FDFFFFu},
                               {0xA9FF0000u, 0xAC0FFFFFu}, {0xAC200000u, 0xC0A7FFFFu}, {0xC0A90000u, 0xFFFFFFFFu}};

std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool Parse(const std::wstring& s, AddrSet& a) { return ParseRemoteAddresses(s, a); }
bool ParseOk(const std::wstring& s) {
    AddrSet a;
    return Parse(s, a);
}
bool Covered(const std::wstring& s) {
    AddrSet a;
    return Parse(s, a) && CoverageOk(a);
}

std::string Dotted(uint32_t v) {
    return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "." + std::to_string((v >> 8) & 255) + "." +
           std::to_string(v & 255);
}

std::vector<std::pair<uint32_t, unsigned>> RangeToBlocks(uint32_t lo, uint32_t hi) {
    std::vector<std::pair<uint32_t, unsigned>> out;
    uint64_t cur = lo;
    while (cur <= hi) {
        uint64_t size = cur == 0 ? (1ull << 32) : (cur & (~cur + 1));
        while (size > hi - cur + 1) size >>= 1;
        unsigned len = 32;
        for (uint64_t s = size; s > 1; s >>= 1) --len;
        out.push_back({static_cast<uint32_t>(cur), len});
        cur += size;
    }
    return out;
}

std::wstring ComplementAsCidr(bool dotted, bool withV6) {
    std::string s;
    for (const V4Range& r : kComplement) {
        for (const auto& b : RangeToBlocks(r.first, r.last)) {
            if (!s.empty()) s += ",";
            const uint32_t mask = b.second == 0 ? 0u : (~0u << (32 - b.second));
            s += Dotted(b.first) + "/" + (dotted ? Dotted(mask) : std::to_string(b.second));
        }
    }
    if (withV6) s += ",::/0";
    return W(s);
}

FwRuleView Rule(int dir, const std::wstring& remote = kLauncherVector) {
    FwRuleView r;
    r.propsOk = true;
    r.enabled = true;
    r.block = true;
    r.direction = dir;
    r.profiles = 0x7FFFFFFF;
    r.protocol = kProtocolAny;
    r.appName = kExe;
    r.localPorts = L"*";
    r.remotePorts = L"*";
    r.localAddrs = L"*";
    r.remoteAddrs = remote;
    r.interfaceTypes = L"All";
    r.interfacesEmpty = true;
    return r;
}

FwSnapshot Snap(std::vector<FwRuleView> rules, long active = kProfilePublic) {
    FwSnapshot s;
    s.profilesOk = true;
    s.activeProfiles = active;
    s.enabledProfiles = active;
    s.rulesOk = true;
    s.rules = std::move(rules);
    return s;
}

FwSnapshot GoodSnap() { return Snap({Rule(kDirOut), Rule(kDirIn)}); }

TcpRow Row(uint32_t pid, uint32_t state, uint32_t addr, bool v6 = false) {
    TcpRow r;
    r.v6 = v6;
    r.pid = pid;
    r.state = state;
    r.remote[0] = static_cast<uint8_t>(addr >> 24);
    r.remote[1] = static_cast<uint8_t>(addr >> 16);
    r.remote[2] = static_cast<uint8_t>(addr >> 8);
    r.remote[3] = static_cast<uint8_t>(addr);
    return r;
}

TcpRow Mapped(uint32_t pid, uint32_t state, uint32_t addr) {
    TcpRow r;
    r.v6 = true;
    r.pid = pid;
    r.state = state;
    r.remote[10] = 0xFF;
    r.remote[11] = 0xFF;
    r.remote[12] = static_cast<uint8_t>(addr >> 24);
    r.remote[13] = static_cast<uint8_t>(addr >> 16);
    r.remote[14] = static_cast<uint8_t>(addr >> 8);
    r.remote[15] = static_cast<uint8_t>(addr);
    return r;
}

TcpSnapshot Tcp(std::vector<TcpRow> rows) {
    TcpSnapshot t;
    t.ok = true;
    t.rows = std::move(rows);
    return t;
}

constexpr uint32_t kPid = 4242;
constexpr uint32_t kEstab = 5;
constexpr uint32_t kSynSent = 3;
constexpr uint32_t kPublic = 0x08080808u;
constexpr uint32_t kLan = 0xC0A80105u;

Evidence DecideAll(Tri ac, Tri target, const FwSnapshot& fw, const TcpSnapshot& tcp) {
    return Decide(ac, target, fw, kExe, tcp, kPid);
}

CASE(decide_all_positive_is_admitted) {
    const Evidence e = DecideAll(Tri::Clear, Tri::Clear, GoodSnap(), Tcp({Row(kPid, kEstab, kLan)}));
    CHECK(e.session == Tri::Clear);
    CHECK(e.officialNetwork == Tri::Clear);
    CHECK(e.antiCheat == Tri::Clear);
    CHECK(e.targetProcess == Tri::Clear);
    CHECK(e.admitted());
}

CASE(decide_default_evidence_is_all_unknown) {
    const Evidence e;
    CHECK(e.session == Tri::Unknown && e.officialNetwork == Tri::Unknown);
    CHECK(e.antiCheat == Tri::Unknown && e.targetProcess == Tri::Unknown);
    CHECK(!e.admitted());
}

CASE(decide_each_missing_input_refuses) {
    const TcpSnapshot okTcp = Tcp({});
    const FwSnapshot okFw = GoodSnap();

    Evidence e = DecideAll(Tri::Unknown, Tri::Clear, okFw, okTcp);
    CHECK(e.antiCheat == Tri::Unknown && !e.admitted());
    e = DecideAll(Tri::Present, Tri::Clear, okFw, okTcp);
    CHECK(e.antiCheat == Tri::Present && !e.admitted());
    e = DecideAll(Tri::Clear, Tri::Unknown, okFw, okTcp);
    CHECK(e.targetProcess == Tri::Unknown && !e.admitted());
    e = DecideAll(Tri::Clear, Tri::Present, okFw, okTcp);
    CHECK(e.targetProcess == Tri::Present && !e.admitted());

    TcpSnapshot badTcp = Tcp({});
    badTcp.ok = false;
    e = DecideAll(Tri::Clear, Tri::Clear, okFw, badTcp);
    CHECK(e.session == Tri::Unknown && e.officialNetwork == Tri::Clear && !e.admitted());

    FwSnapshot badFw = GoodSnap();
    badFw.rulesOk = false;
    e = DecideAll(Tri::Clear, Tri::Clear, badFw, okTcp);
    CHECK(e.officialNetwork == Tri::Unknown && e.session == Tri::Unknown && !e.admitted());

    badFw = GoodSnap();
    badFw.profilesOk = false;
    e = DecideAll(Tri::Clear, Tri::Clear, badFw, okTcp);
    CHECK(e.officialNetwork == Tri::Unknown && !e.admitted());

    e = DecideAll(Tri::Clear, Tri::Clear, FwSnapshot{}, TcpSnapshot{});
    CHECK(e.officialNetwork == Tri::Unknown && e.session == Tri::Unknown && !e.admitted());

    e = DecideAll(Tri::Clear, Tri::Clear, okFw, Tcp({Row(kPid, kEstab, kPublic)}));
    CHECK(e.session == Tri::Present && !e.admitted());
}

CASE(decide_empty_exe_path_never_clear) {
    const Evidence e = Decide(Tri::Clear, Tri::Clear, GoodSnap(), L"", Tcp({}), kPid);
    CHECK(e.officialNetwork == Tri::Unknown && !e.admitted());
}

CASE(decide_zero_pid_never_clear) {
    const Evidence e = Decide(Tri::Clear, Tri::Clear, GoodSnap(), kExe, Tcp({}), 0);
    CHECK(e.session == Tri::Unknown && !e.admitted());
}

CASE(parse_launcher_vector_is_covered) {
    AddrSet a;
    CHECK(Parse(kLauncherVector, a));
    CHECK(a.v4.size() == 6 && a.v6.size() == 1);
    CHECK(CoverageOk(a));
}

CASE(parse_launcher_vector_minus_any_one_range_is_not_covered) {
    const std::vector<std::wstring> tokens = {
        L"0.0.0.0-9.255.255.255",       L"11.0.0.0-126.255.255.255",   L"128.0.0.0-169.253.255.255",
        L"169.255.0.0-172.15.255.255",  L"172.32.0.0-192.167.255.255", L"192.169.0.0-255.255.255.255",
        L"::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"};
    for (size_t skip = 0; skip < tokens.size(); ++skip) {
        std::wstring s;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i == skip) continue;
            if (!s.empty()) s += L",";
            s += tokens[i];
        }
        CHECK(ParseOk(s));
        CHECK(!Covered(s));
    }
}

CASE(parse_shrunk_range_by_one_address_is_not_covered) {
    std::wstring s = kLauncherVector;
    const std::wstring from = L"11.0.0.0-126.255.255.255";
    s.replace(s.find(from), from.size(), L"11.0.0.0-126.255.255.254");
    CHECK(ParseOk(s));
    CHECK(!Covered(s));
    s = kLauncherVector;
    s.replace(s.find(from), from.size(), L"11.0.0.1-126.255.255.255");
    CHECK(!Covered(s));
}

CASE(parse_keyword_tokens_fail_the_whole_string) {
    for (const wchar_t* t : {L"LocalSubnet", L"Internet", L"DNS", L"DHCP", L"DefaultGateway", L"Intranet"}) {
        CHECK(!ParseOk(t));
        CHECK(!ParseOk(std::wstring(t) + L"," + kLauncherVector));
        CHECK(!ParseOk(kLauncherVector + L"," + t));
    }
}

CASE(parse_failure_leaves_output_untouched) {
    AddrSet a;
    CHECK(Parse(L"10.0.0.1", a));
    CHECK(a.v4.size() == 1);
    CHECK(!Parse(L"1.2.3.4,Internet", a));
    CHECK(a.v4.size() == 1 && a.v6.empty());
}

CASE(parse_ipv6_missing_is_not_covered) {
    std::wstring v4only;
    for (const V4Range& r : kComplement) {
        if (!v4only.empty()) v4only += L",";
        v4only += W(Dotted(r.first) + "-" + Dotted(r.last));
    }
    CHECK(ParseOk(v4only));
    CHECK(!Covered(v4only));
    CHECK(Covered(v4only + L",::/0"));
}

CASE(parse_ipv6_partial_is_not_covered) {
    CHECK(!Covered(kLauncherVector.substr(0, kLauncherVector.find(L"::-")) + L"::-fe7f:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
    CHECK(!Covered(kLauncherVector.substr(0, kLauncherVector.find(L"::-")) + L"::1-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
    const std::wstring v4 = kLauncherVector.substr(0, kLauncherVector.find(L",::-"));
    CHECK(Covered(v4 + L",::/1,8000::/1"));
    CHECK(!Covered(v4 + L",::/1,8000::/2"));
    CHECK(Covered(v4 + L",::-7fff:ffff:ffff:ffff:ffff:ffff:ffff:ffff,8000::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));
}

CASE(parse_cidr_prefix_form_matches_range_form) {
    CHECK(Covered(ComplementAsCidr(false, true)));
    AddrSet cidr, range;
    CHECK(Parse(ComplementAsCidr(false, true), cidr));
    CHECK(Parse(kLauncherVector, range));
    CHECK(!cidr.v4.empty());
    CHECK(CoverageOk(cidr) && CoverageOk(range));
}

CASE(parse_dotted_mask_form_matches_prefix_form) {
    CHECK(Covered(ComplementAsCidr(true, true)));
    AddrSet a, b;
    CHECK(Parse(L"10.1.2.3/8", a));
    CHECK(Parse(L"10.1.2.3/255.0.0.0", b));
    CHECK(a.v4.size() == 1 && b.v4.size() == 1);
    CHECK(a.v4[0].first == 0x0A000000u && a.v4[0].last == 0x0AFFFFFFu);
    CHECK(b.v4[0].first == a.v4[0].first && b.v4[0].last == a.v4[0].last);
}

CASE(parse_cidr_off_by_one_is_not_covered) {
    std::wstring s = ComplementAsCidr(false, true);
    const size_t at = s.find(L"11.0.0.0/");
    CHECK(at != std::wstring::npos);
    s.replace(at, 0, L"");
    s.erase(at, s.find(L',', at) - at + 1);
    CHECK(ParseOk(s));
    CHECK(!Covered(s));
}

CASE(parse_single_and_wildcard_forms) {
    AddrSet a;
    CHECK(Parse(L"192.168.1.5", a) && a.v4.size() == 1 && a.v4[0].first == a.v4[0].last);
    AddrSet star;
    CHECK(Parse(L"*", star));
    CHECK(CoverageOk(star));
    CHECK(Covered(L" * "));
    AddrSet v6;
    CHECK(Parse(L"fe80::1,::ffff:1.2.3.4,2001:db8::/32", v6) && v6.v6.size() == 3);
    CHECK(!CoverageOk(v6));
}

CASE(parse_garbage_is_rejected) {
    for (const wchar_t* g : {L"", L",", L" ", L"abc", L"1.2.3", L"1.2.3.4.5", L"256.0.0.1", L"1.2.3.4-", L"-1.2.3.4",
                             L"5.0.0.0-1.0.0.0", L"10.0.0.0/33", L"10.0.0.0/", L"10.0.0.0/-1", L"1.2.3.4/255.0.255.0",
                             L"1.2.3.4 5.6.7.8", L"::1::2", L":::", L"1:2:3:4:5:6:7", L"1:2:3:4:5:6:7:8:9",
                             L"fe80::/129", L"fe80::g", L"12345::", L"1.2.3.4-5.6.7.8/8", L"1.2.3.04x", L"1..2.3",
                             L"::1,", L",::1", L"fe80::1%5", L"1.2.3.4\x00e9"}) {
        CHECK(!ParseOk(g));
    }
}

CASE(official_network_both_directions_required) {
    CHECK(OfficialNetwork(GoodSnap(), kExe) == Tri::Clear);
    CHECK(OfficialNetwork(Snap({Rule(kDirOut)}), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({Rule(kDirIn)}), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({}), kExe) == Tri::Unknown);
    FwDetail d;
    CHECK(OfficialNetwork(Snap({Rule(kDirOut)}), kExe, &d) == Tri::Unknown);
    CHECK(d.countingOut == 1 && d.countingIn == 0 && d.coveredOut && !d.coveredIn);
}

CASE(official_network_profile_coverage) {
    FwRuleView privateOnly = Rule(kDirOut);
    privateOnly.profiles = kProfilePrivate;
    FwRuleView privateOnlyIn = Rule(kDirIn);
    privateOnlyIn.profiles = kProfilePrivate;
    CHECK(OfficialNetwork(Snap({privateOnly, privateOnlyIn}, kProfilePublic), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({privateOnly, privateOnlyIn}, kProfilePrivate), kExe) == Tri::Clear);
    CHECK(OfficialNetwork(Snap({privateOnly, privateOnlyIn}, kProfilePrivate | kProfilePublic), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({Rule(kDirOut), Rule(kDirIn)}, kProfileDomain | kProfilePrivate | kProfilePublic), kExe) ==
          Tri::Clear);
}

CASE(official_network_profile_split_across_rules_does_not_count) {
    FwRuleView a = Rule(kDirOut), b = Rule(kDirOut), c = Rule(kDirIn);
    a.profiles = kProfilePrivate;
    b.profiles = kProfilePublic;
    c.profiles = kProfilePrivate | kProfilePublic;
    CHECK(OfficialNetwork(Snap({a, b, c}, kProfilePrivate | kProfilePublic), kExe) == Tri::Unknown);
}

CASE(official_network_firewall_state_gates) {
    FwSnapshot s = GoodSnap();
    s.enabledProfiles = 0;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    s = Snap({Rule(kDirOut), Rule(kDirIn)}, kProfilePrivate | kProfilePublic);
    s.enabledProfiles = kProfilePrivate;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    s.enabledProfiles = kProfilePrivate | kProfilePublic;
    CHECK(OfficialNetwork(s, kExe) == Tri::Clear);
    s = GoodSnap();
    s.activeProfiles = 0;
    s.enabledProfiles = 0;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    s = GoodSnap();
    s.profilesOk = false;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    s = GoodSnap();
    s.rulesOk = false;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    s = GoodSnap();
    s.activeProfiles = 0x40;
    s.enabledProfiles = 0x40;
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
}

template <class Mut>
void ExpectIgnored(Mut mut) {
    FwRuleView out = Rule(kDirOut);
    FwRuleView in = Rule(kDirIn);
    mut(out);
    mut(in);
    CHECK(OfficialNetwork(Snap({out, in}), kExe) == Tri::Unknown);
    FwRuleView good = Rule(kDirOut);
    FwRuleView bad = Rule(kDirOut);
    mut(bad);
    CHECK(OfficialNetwork(Snap({bad, good, Rule(kDirIn)}), kExe) == Tri::Clear);
}

CASE(official_network_ignores_disabled_rule) {
    ExpectIgnored([](FwRuleView& r) { r.enabled = false; });
}
CASE(official_network_ignores_allow_action) {
    ExpectIgnored([](FwRuleView& r) { r.block = false; });
}
CASE(official_network_ignores_wrong_exe) {
    ExpectIgnored([](FwRuleView& r) { r.appName = L"C:\\Other\\StarCitizen.exe"; });
    ExpectIgnored([](FwRuleView& r) { r.appName = L""; });
    ExpectIgnored([](FwRuleView& r) { r.appName = L"%ProgramFiles%\\x\\StarCitizen.exe"; });
}
CASE(official_network_ignores_restricted_protocol) {
    ExpectIgnored([](FwRuleView& r) { r.protocol = 6; });
    ExpectIgnored([](FwRuleView& r) { r.protocol = 17; });
}
CASE(official_network_ignores_restricted_ports) {
    ExpectIgnored([](FwRuleView& r) { r.remotePorts = L"443"; });
    ExpectIgnored([](FwRuleView& r) { r.localPorts = L"64000-64999"; });
}
CASE(official_network_ignores_service_and_interface_restrictions) {
    ExpectIgnored([](FwRuleView& r) { r.serviceName = L"SomeService"; });
    ExpectIgnored([](FwRuleView& r) { r.interfaceTypes = L"Wireless"; });
    ExpectIgnored([](FwRuleView& r) { r.interfacesEmpty = false; });
}
CASE(official_network_ignores_local_address_restriction) {
    ExpectIgnored([](FwRuleView& r) { r.localAddrs = L"10.0.0.5"; });
}
CASE(official_network_ignores_unreadable_rule) {
    ExpectIgnored([](FwRuleView& r) { r.propsOk = false; });
}
CASE(official_network_ignores_wrong_direction_rule) {
    const FwSnapshot s = Snap({Rule(kDirOut), Rule(kDirOut)});
    CHECK(OfficialNetwork(s, kExe) == Tri::Unknown);
    FwRuleView bogus = Rule(kDirIn);
    bogus.direction = 3;
    CHECK(OfficialNetwork(Snap({Rule(kDirOut), bogus}), kExe) == Tri::Unknown);
}

CASE(official_network_ignores_unparseable_remote_addresses) {
    FwRuleView bad = Rule(kDirOut, L"LocalSubnet");
    CHECK(OfficialNetwork(Snap({bad, Rule(kDirIn)}), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({bad, Rule(kDirOut), Rule(kDirIn)}), kExe) == Tri::Clear);
    FwRuleView mixed = Rule(kDirOut, kLauncherVector + L",Internet");
    CHECK(OfficialNetwork(Snap({mixed, Rule(kDirIn)}), kExe) == Tri::Unknown);
}

CASE(official_network_rule_remote_set_must_cover) {
    std::wstring lessOne = kLauncherVector;
    const std::wstring from = L"11.0.0.0-126.255.255.255";
    lessOne.replace(lessOne.find(from), from.size() + 1, L"");
    CHECK(OfficialNetwork(Snap({Rule(kDirOut, lessOne), Rule(kDirIn)}), kExe) == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({Rule(kDirOut), Rule(kDirIn, lessOne)}), kExe) == Tri::Unknown);
}

CASE(official_network_unions_rules_in_one_direction) {
    const std::wstring a = L"0.0.0.0-9.255.255.255,11.0.0.0-126.255.255.255,128.0.0.0-169.253.255.255";
    const std::wstring b =
        L"169.255.0.0-172.15.255.255,172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::/0";
    CHECK(OfficialNetwork(Snap({Rule(kDirOut, a), Rule(kDirOut, b), Rule(kDirIn)}), kExe) == Tri::Clear);
    CHECK(OfficialNetwork(Snap({Rule(kDirOut, a), Rule(kDirIn)}), kExe) == Tri::Unknown);
}

CASE(official_network_exe_path_normalisation) {
    FwRuleView out = Rule(kDirOut);
    FwRuleView in = Rule(kDirIn);
    out.appName = L"c:/games/starcitizen/bin64/STARCITIZEN.EXE";
    in.appName = L"\\\\?\\C:\\Games\\StarCitizen\\Bin64\\StarCitizen.exe";
    CHECK(OfficialNetwork(Snap({out, in}), kExe) == Tri::Clear);
    CHECK(OfficialNetwork(Snap({out, in}), L"C:\\Games\\StarCitizen\\Bin64\\Other.exe") == Tri::Unknown);
    CHECK(OfficialNetwork(Snap({out, in}), L"") == Tri::Unknown);
    CHECK(!SamePath(L"", L""));
}

CASE(official_network_public_multiplayer_peer_is_unknown) {
    const std::wstring withHole =
        L"0.0.0.0-9.255.255.255,11.0.0.0-126.255.255.255,128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,"
        L"172.32.0.0-192.167.255.255,192.169.0.0-203.0.113.4,203.0.113.6-255.255.255.255,::/0";
    CHECK(OfficialNetwork(Snap({Rule(kDirOut, withHole), Rule(kDirIn)}), kExe) == Tri::Unknown);
}

CASE(official_network_allows_extra_private_peer_in_allow_set) {
    const std::wstring wider =
        L"0.0.0.0-9.255.255.255,11.0.0.0-126.255.255.255,128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,"
        L"172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::/0,10.0.0.0/8,192.168.0.0/16";
    CHECK(OfficialNetwork(Snap({Rule(kDirOut, wider), Rule(kDirIn, wider)}), kExe) == Tri::Clear);
}

CASE(session_public_live_connection_is_online) {
    CHECK(Session(Tcp({Row(kPid, kEstab, kPublic)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kSynSent, kPublic)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kEstab, kLan), Row(kPid, kEstab, kPublic)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kEstab, kPublic)}), Tri::Unknown, kPid) == Tri::Present);
}

CASE(session_failed_query_is_unknown) {
    TcpSnapshot t = Tcp({});
    t.ok = false;
    CHECK(Session(t, Tri::Clear, kPid) == Tri::Unknown);
    t.rows.push_back(Row(kPid, kEstab, kLan));
    CHECK(Session(t, Tri::Clear, kPid) == Tri::Unknown);
    CHECK(Session(TcpSnapshot{}, Tri::Clear, kPid) == Tri::Unknown);
}

CASE(session_private_only_is_offline_only_with_clear_network) {
    const TcpSnapshot t = Tcp({Row(kPid, kEstab, kLan), Row(kPid, kEstab, 0x7F000001u), Row(kPid, kEstab, 0x0A000001u),
                               Row(kPid, kEstab, 0xAC100001u), Row(kPid, kEstab, 0xA9FE0001u)});
    CHECK(Session(t, Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(t, Tri::Unknown, kPid) == Tri::Unknown);
    CHECK(Session(t, Tri::Present, kPid) == Tri::Unknown);
    CHECK(Session(Tcp({}), Tri::Clear, kPid) == Tri::Clear);
}

CASE(session_private_range_edges) {
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xAC1FFFFFu)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xAC200000u)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xAC0FFFFFu)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xC0A8FFFFu)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xC0A90000u)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xA9FEFFFFu)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kEstab, 0xA9FF0000u)}), Tri::Clear, kPid) == Tri::Present);
}

CASE(session_ignores_other_pids_and_idle_states) {
    CHECK(Session(Tcp({Row(kPid + 1, kEstab, kPublic)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kTcpListen, 0)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kTcpTimeWait, kPublic)}), Tri::Clear, kPid) == Tri::Clear);
    CHECK(Session(Tcp({Row(kPid, kTcpClosed, kPublic)}), Tri::Clear, kPid) == Tri::Clear);
}

CASE(session_ipv6_remotes) {
    CHECK(Session(Tcp({Mapped(kPid, kEstab, kPublic)}), Tri::Clear, kPid) == Tri::Present);
    CHECK(Session(Tcp({Mapped(kPid, kEstab, kLan)}), Tri::Clear, kPid) == Tri::Clear);

    TcpRow loop = Row(kPid, kEstab, 0, true);
    loop.remote[15] = 1;
    CHECK(Session(Tcp({loop}), Tri::Clear, kPid) == Tri::Clear);

    TcpRow linkLocal = Row(kPid, kEstab, 0, true);
    linkLocal.remote[0] = 0xFE;
    linkLocal.remote[1] = 0x80;
    linkLocal.remote[15] = 7;
    CHECK(Session(Tcp({linkLocal}), Tri::Clear, kPid) == Tri::Clear);

    TcpRow global = Row(kPid, kEstab, 0, true);
    global.remote[0] = 0x20;
    global.remote[1] = 0x01;
    global.remote[15] = 1;
    CHECK(Session(Tcp({global}), Tri::Clear, kPid) == Tri::Present);

    TcpRow ula = Row(kPid, kEstab, 0, true);
    ula.remote[0] = 0xFD;
    CHECK(Session(Tcp({ula}), Tri::Clear, kPid) == Tri::Present);

    TcpRow unspecified = Row(kPid, kEstab, 0, true);
    CHECK(Session(Tcp({unspecified}), Tri::Clear, kPid) == Tri::Present);
}

struct FakeClock {
    uint64_t now = 100;
    EvidenceCache::Clock fn() {
        return [this] { return now; };
    }
};

Evidence AllClear() {
    Evidence e;
    e.session = e.officialNetwork = e.antiCheat = e.targetProcess = Tri::Clear;
    return e;
}

CASE(cache_positive_is_cached_within_ttl) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); });
    CHECK(c.Get().admitted());
    CHECK(c.Get().admitted());
    clk.now += 999;
    CHECK(c.Get().admitted());
    CHECK(probes == 1);
}

CASE(cache_expires_at_ttl_boundary) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); });
    c.Get();
    clk.now += 1000;
    c.Get();
    CHECK(probes == 2);
    clk.now += 999;
    c.Get();
    CHECK(probes == 2);
}

CASE(cache_age_counts_from_probe_start) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] {
        ++probes;
        clk.now += 900;
        return AllClear();
    });
    c.Get();
    clk.now += 150;
    c.Get();
    CHECK(probes == 2);
}

CASE(cache_custom_ttl) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); }, 50);
    c.Get();
    clk.now += 49;
    c.Get();
    CHECK(probes == 1);
    clk.now += 1;
    c.Get();
    CHECK(probes == 2);
}

CASE(cache_never_caches_unknown_or_present) {
    FakeClock clk;
    int probes = 0;
    Evidence next;
    EvidenceCache c(clk.fn(), [&] { ++probes; return next; });
    next = Evidence{};
    for (int i = 0; i < 3; ++i) CHECK(!c.Get().admitted());
    CHECK(probes == 3);
    next = AllClear();
    next.officialNetwork = Tri::Present;
    for (int i = 0; i < 3; ++i) CHECK(!c.Get().admitted());
    CHECK(probes == 6);
    next = AllClear();
    next.session = Tri::Present;
    CHECK(!c.Get().admitted());
    CHECK(probes == 7);
}

CASE(cache_recovers_immediately_and_refusal_is_not_sticky) {
    FakeClock clk;
    int probes = 0;
    Evidence next = Evidence{};
    EvidenceCache c(clk.fn(), [&] { ++probes; return next; });
    CHECK(!c.Get().admitted());
    next = AllClear();
    CHECK(c.Get().admitted());
    CHECK(c.Get().admitted());
    CHECK(probes == 2);
}

CASE(cache_non_clear_probe_drops_the_cache) {
    FakeClock clk;
    int probes = 0;
    Evidence next = AllClear();
    EvidenceCache c(clk.fn(), [&] { ++probes; return next; });
    CHECK(c.Get().admitted());
    clk.now += 1000;
    next = Evidence{};
    CHECK(!c.Get().admitted());
    next = AllClear();
    CHECK(c.Get().admitted());
    CHECK(probes == 3);
}

CASE(cache_invalidate_forces_a_probe) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); });
    c.Get();
    c.Invalidate();
    c.Get();
    CHECK(probes == 2);
    c.Get();
    CHECK(probes == 2);
}

CASE(cache_invalidate_during_probe_prevents_caching) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache* self = nullptr;
    EvidenceCache c(clk.fn(), [&] {
        ++probes;
        if (probes == 1) self->Invalidate();
        return AllClear();
    });
    self = &c;
    CHECK(c.Get().admitted());
    CHECK(c.Get().admitted());
    CHECK(probes == 2);
    CHECK(c.Get().admitted());
    CHECK(probes == 2);
}

CASE(cache_reentry_returns_unknown_without_deadlock) {
    FakeClock clk;
    int probes = 0;
    Evidence inner = AllClear();
    EvidenceCache* self = nullptr;
    EvidenceCache c(clk.fn(), [&] {
        ++probes;
        inner = self->Get();
        return AllClear();
    });
    self = &c;
    const Evidence outer = c.Get();
    CHECK(outer.admitted());
    CHECK(inner.session == Tri::Unknown && inner.officialNetwork == Tri::Unknown);
    CHECK(inner.antiCheat == Tri::Unknown && inner.targetProcess == Tri::Unknown);
    CHECK(!inner.admitted());
    CHECK(probes == 1);
    CHECK(c.Get().admitted());
    CHECK(probes == 1);
}

CASE(cache_reentry_guard_is_per_thread) {
    FakeClock clk;
    EvidenceCache c(clk.fn(), [&] { return AllClear(); });
    CHECK(c.Get().admitted());
    bool otherThreadAdmitted = false;
    std::thread t([&] { otherThreadAdmitted = c.Get().admitted(); });
    t.join();
    CHECK(otherThreadAdmitted);
}

CASE(cache_probe_exception_is_unknown_and_not_cached) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&]() -> Evidence {
        if (++probes == 1) throw 7;
        return AllClear();
    });
    CHECK(!c.Get().admitted());
    CHECK(c.Get().admitted());
    CHECK(probes == 2);
}

CASE(cache_clock_going_backwards_expires_the_entry) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); });
    c.Get();
    clk.now -= 1;
    c.Get();
    CHECK(probes == 2);
}

CASE(cache_missing_probe_or_clock_is_unknown) {
    EvidenceCache noProbe([] { return uint64_t{0}; }, EvidenceCache::Probe{});
    CHECK(!noProbe.Get().admitted());
    EvidenceCache noClock(EvidenceCache::Clock{}, [] { return AllClear(); });
    CHECK(!noClock.Get().admitted());
}

CASE(cache_fresh_target_is_read_every_call) {
    FakeClock clk;
    int probes = 0;
    EvidenceCache c(clk.fn(), [&] { ++probes; return AllClear(); });
    CHECK(c.Get(Tri::Clear).admitted());
    CHECK(c.Get(Tri::Clear).admitted());
    CHECK(probes == 1);
    const Evidence bad = c.Get(Tri::Present);
    CHECK(!bad.admitted() && bad.targetProcess == Tri::Present);
    CHECK(probes == 2);
    CHECK(c.Get(Tri::Clear).admitted());
    CHECK(!c.Get(Tri::Unknown).admitted());
}

CASE(cache_single_flight_overlapping_callers_share_one_probe) {
    FakeClock clk;
    std::atomic<int> probes{0};
    std::promise<void> entered, release, waiting;
    std::future<void> releaseF = release.get_future();
    EvidenceCache c(clk.fn(), [&] {
        if (probes.fetch_add(1) == 0) {
            entered.set_value();
            releaseF.wait();
        }
        return AllClear();
    });
    std::atomic<bool> waiterSignalled{false};
    c.SetWaitObserver([&] {
        if (!waiterSignalled.exchange(true)) waiting.set_value();
    });
    bool leaderOk = false, followerOk = false;
    std::thread leader([&] { leaderOk = c.Get().admitted(); });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    std::thread follower([&] { followerOk = c.Get().admitted(); });
    CHECK(waiting.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    release.set_value();
    leader.join();
    follower.join();
    CHECK(leaderOk && followerOk);
    CHECK(probes.load() == 1);
}

// Firewall snapshot equivalent to the launcher leaving only Tailscale plus LAN open.
CASE(configured_vpn_gap_requires_explicit_valid_scope) {
    const std::wstring vpn =
        L"0.0.0.0-9.255.255.255,11.0.0.0-100.63.255.255,100.128.0.0-126.255.255.255,"
        L"128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,"
        L"172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::/0";
    auto fw = Snap({Rule(kDirIn, vpn), Rule(kDirOut, vpn)});
    auto decide = [&](const std::wstring& allow) {
        return Decide(Tri::Clear, Tri::Clear, fw, kExe, Tcp({}), kPid, nullptr, allow);
    };
    CHECK(decide(L"100.64.0.0/10").admitted());
    CHECK(!decide(L"").admitted());
    CHECK(!decide(L"100.64.0.0/11").admitted());
    CHECK(!decide(L"100.0.0.0/10").admitted());
    for (const auto* bad : {L"*", L"::/0", L"0.0.0.0/0", L"100.64.0.1/10",
                            L"100.64.0.0/33", L"100.64.0.0/7", L"100.64.0.0",
                            L"100.64.0.0/255.192.0.0", L"100.64.0.0/10,",
                            L",100.64.0.0/10", L"100.64.0.0/10,,10.0.0.0/8",
                            L"100.64.0.0/10,bad", L"100.64.0.0-100.127.255.255",
                            L"256.0.0.0/8", L"１００.64.0.0/10"}) {
        CHECK(!decide(bad).admitted());
        CHECK(!Decide(Tri::Clear, Tri::Clear, GoodSnap(), kExe, Tcp({}), kPid, nullptr, bad).admitted());
    }
    CHECK(decide(L"100.64.0.0/11,100.96.0.0/11").admitted());
    CHECK(decide(L" 100.96.0.0/11 , 100.64.0.0/10 ").admitted());
    CHECK(!decide(L"100.64.0.0/32").admitted());
    CHECK(!decide(L"100.64.0.0/10,10.0.0.0/8,10.0.0.0/8,10.0.0.0/8,10.0.0.0/8,"
                  L"10.0.0.0/8,10.0.0.0/8,10.0.0.0/8,10.0.0.0/8").admitted());
    CHECK(!decide(std::wstring(256, L' ')).admitted());
    // Exact endpoints: one uncovered address on either side must still refuse.
    for (const auto* wider : {L"11.0.0.0-100.63.255.254", L"100.128.0.1-126.255.255.255"}) {
        auto gap = vpn;
        const std::wstring original = wider[0] == L'1' && wider[1] == L'1'
            ? L"11.0.0.0-100.63.255.255" : L"100.128.0.0-126.255.255.255";
        gap.replace(gap.find(original), original.size(), wider);
        fw = Snap({Rule(kDirIn, gap), Rule(kDirOut, gap)});
        CHECK(!decide(L"100.64.0.0/10").admitted());
    }
    fw = Snap({Rule(kDirIn, vpn)});
    CHECK(!decide(L"100.64.0.0/10").admitted());
    fw = Snap({Rule(kDirIn, vpn), Rule(kDirOut, vpn)});
    fw.rules[0].remoteAddrs += L",malformed";
    CHECK(!decide(L"100.64.0.0/10").admitted());
    fw = Snap({Rule(kDirIn, vpn.substr(0, vpn.find(L",::/0"))), Rule(kDirOut, vpn)});
    CHECK(!decide(L"100.64.0.0/10").admitted());
    // VPN coverage is not permission to ignore live non-private TCP evidence.
    fw = Snap({Rule(kDirIn, vpn), Rule(kDirOut, vpn)});
    CHECK(!Decide(Tri::Clear, Tri::Clear, fw, kExe, Tcp({Row(kPid, 5, 0x64400001u)}),
                  kPid, nullptr, L"100.64.0.0/10").admitted());
    CHECK(!Decide(Tri::Unknown, Tri::Clear, fw, kExe, Tcp({}), kPid, nullptr,
                  L"100.64.0.0/10").admitted());
}

CASE(cache_single_flight_waiter_shares_a_refusal) {
    FakeClock clk;
    std::atomic<int> probes{0};
    std::promise<void> entered, release, waiting;
    std::future<void> releaseF = release.get_future();
    EvidenceCache c(clk.fn(), [&] {
        if (probes.fetch_add(1) == 0) {
            entered.set_value();
            releaseF.wait();
        }
        return Evidence{};
    });
    std::atomic<bool> waiterSignalled{false};
    c.SetWaitObserver([&] {
        if (!waiterSignalled.exchange(true)) waiting.set_value();
    });
    bool leaderOk = true, followerOk = true;
    std::thread leader([&] { leaderOk = c.Get().admitted(); });
    CHECK(entered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    std::thread follower([&] { followerOk = c.Get().admitted(); });
    CHECK(waiting.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    release.set_value();
    leader.join();
    follower.join();
    CHECK(!leaderOk && !followerOk);
    CHECK(probes.load() == 1);
    CHECK(!c.Get().admitted());
    CHECK(probes.load() == 2);
}

}

int main() {
    int run = 0;
    for (const CaseEntry& c : Cases()) {
        g_case = c.name;
        const int before = g_failed;
        try {
            c.fn();
        } catch (...) {
            ++g_failed;
            std::fprintf(stderr, "FAIL [%s]: unexpected exception\n", c.name);
        }
        ++run;
        if (g_failed != before) std::fprintf(stderr, "case %s failed\n", c.name);
    }
    std::printf("admission_evidence_test: %d cases, %d checks, %d failed\n", run, g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
