// SPDX-License-Identifier: GPL-3.0-only
// Release-candidate version rules of the C++ launcher's update check, on the shipped functions (launcher.cpp is
// included): a pre-release tag sorts below the same numbers without the suffix, 0.7.8 < 0.8.0-rc.1 < 0.8.0-rc.2 <
// 0.8.0, and PickRelease (the choice CheckLatest makes on GitHub's reply) offers a pre-release only on the
// pre-release channel. The Rust launcher has the same tests in crates/scol-update/src/release.rs and
// version.rs, and the two are run against each other in launcher-rs/tests/update/update.rs.
// No network, no game and no file outside the one scratch folder named by the first argument.
#define wmain sco_launcher_unused_entrypoint
#include "../launcher/launcher.cpp"
#undef wmain
#include <iostream>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static std::string Release(const std::string& tag, bool pre, bool draft = false) {
    const std::string name = "sc-offline-" + tag + ".zip";
    return "{\"tag_name\":\"" + tag + "\",\"prerelease\":" + (pre ? "true" : "false") + ",\"draft\":" + (draft ? "true" : "false") +
           ",\"assets\":[{\"name\":\"" + name + "\",\"size\":10,\"digest\":\"sha256:" + std::string(64, 'a') +
           "\",\"browser_download_url\":\"https://github.com/scuba-sc/sc-offline/releases/download/" + tag + "/" + name + "\"}]}";
}

static std::string List(std::initializer_list<std::string> releases) {
    std::string out = "[";
    for (const std::string& r : releases) out += (out.size() > 1 ? "," : "") + r;
    return out + "]";
}

// "v0.8.0" or "" when nothing is offered.
static std::string Offer(const std::string& body, bool prereleaseChannel, const std::string& current) {
    std::string why;
    return PickRelease(body, prereleaseChannel, current, &why).tag;
}

static std::string Why(const std::string& body, bool prereleaseChannel, const std::string& current) {
    std::string why;
    PickRelease(body, prereleaseChannel, current, &why);
    return why;
}

static void TestOrdering() {
    // The required chain, each step both ways.
    CHECK(VersionGreater("0.8.0-rc.1", "0.7.8"));
    CHECK(VersionGreater("0.8.0-rc.2", "0.8.0-rc.1"));
    CHECK(VersionGreater("0.8.0", "0.8.0-rc.2"));
    CHECK(VersionGreater("0.8.0", "0.7.8"));
    CHECK(!VersionGreater("0.7.8", "0.8.0-rc.1"));
    CHECK(!VersionGreater("0.8.0-rc.1", "0.8.0-rc.2"));
    CHECK(!VersionGreater("0.8.0-rc.2", "0.8.0"));
    CHECK(!VersionGreater("0.8.0-rc.1", "0.8.0-rc.1"));
    // Tags carry a v; the running version does not.
    CHECK(VersionGreater("v0.8.0", "0.8.0-rc.1"));
    CHECK(VersionGreater("v0.8.0-rc.1", "0.7.7"));
    CHECK(!VersionGreater("v0.8.0-rc.1", "0.8.0-rc.1"));
    // The next minor release outranks any candidate of the previous one.
    CHECK(VersionGreater("0.9.0-rc.1", "0.8.0"));
    // Anything that is not a version is never newer.
    CHECK(!VersionGreater("junk", "0.7.7"));
    CHECK(!VersionGreater("0.8.0-", "0.7.7"));
    CHECK(!VersionGreater("0.8.0-rc_1", "0.7.7"));
}

static void TestChannels() {
    const std::string rc1 = Release("v0.8.0-rc.1", true), rc2 = Release("v0.8.0-rc.2", true), stableRel = Release("v0.8.0", false);
    const std::string old = Release("v0.7.7", false);

    // A 0.8.0-rc.1 launcher on the stable channel: offered 0.8.0 once it is published ...
    CHECK(Offer(stableRel, false, "0.8.0-rc.1") == "v0.8.0");
    CHECK(Offer(List({stableRel, rc2, rc1, old}), false, "0.8.0-rc.1") == "v0.8.0");
    // ... and not another pre-release (the latest reply is one, or the list holds one).
    CHECK(Offer(rc2, false, "0.8.0-rc.1").empty());
    CHECK(Offer(List({rc2, rc1, old}), false, "0.8.0-rc.1").empty());
    // On the pre-release channel (update --prerelease or update_channel = prerelease) the newest is offered.
    CHECK(Offer(List({rc2, rc1, old}), true, "0.8.0-rc.1") == "v0.8.0-rc.2");
    CHECK(Offer(List({stableRel, rc2, rc1}), true, "0.8.0-rc.1") == "v0.8.0");
    CHECK(Offer(List({rc1, old}), true, "0.8.0-rc.1").empty());
    CHECK(Why(List({rc1, old}), true, "0.8.0-rc.1").rfind("up to date", 0) == 0);
    // A draft is never offered.
    CHECK(Offer(Release("v0.8.0", false, true), false, "0.8.0-rc.1").empty());
    CHECK(Offer(List({Release("v0.8.0-rc.2", true, true), rc1}), true, "0.8.0-rc.1").empty());

    // A 0.7.x launcher: on the stable channel a pre-release is not offered, whatever else is published.
    CHECK(Offer(rc1, false, "0.7.7").empty());
    CHECK(Offer(List({rc1, old}), false, "0.7.7").empty());
    CHECK(Offer(List({rc2, rc1, Release("v0.7.8", false), old}), false, "0.7.7") == "v0.7.8");
    // It is offered 0.8.0 as soon as that is published, and on the pre-release channel the candidate.
    CHECK(Offer(stableRel, false, "0.7.7") == "v0.8.0");
    CHECK(Offer(List({rc1, old}), true, "0.7.7") == "v0.8.0-rc.1");
    // A 0.7.8 launcher is also below the candidate.
    CHECK(Offer(List({rc1}), true, "0.7.8") == "v0.8.0-rc.1");

    // A release without the zip that carries its own tag is skipped, as before.
    std::string bad = rc1;
    bad.replace(bad.find("sc-offline-v0.8.0-rc.1.zip"), std::string("sc-offline-v0.8.0-rc.1.zip").size(), "sc-offline-v0.8.0.zip");
    CHECK(Offer(bad, true, "0.7.7").empty());
}

int wmain(int, wchar_t**) {
    TestOrdering();
    TestChannels();
    std::cout << "launcher_update_version_test: " << checks << " checks ok\n";
    return 0;
}
