// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/install_discovery.h"
#include <cstdlib>
#include <iostream>

using namespace sco::launcher::discovery;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static DirectoryIdentity Directory(uint64_t volume, uint8_t low, uint8_t high = 0) {
    DirectoryIdentity id;
    id.valid = true; id.volume = volume; id.file[0] = low; id.file[15] = high;
    return id;
}

static InstallIdentity Read(const std::wstring& path) {
    if (path == L"first" || path == L"alias") return {Directory(1, 1), Directory(1, 2)};
    if (path == L"other") return {Directory(1, 3), Directory(1, 4)};
    return {};
}

int main() {
    const InstallIdentity first{Directory(1, 1), Directory(1, 2)};
    CHECK(SameInstall(L"first", first, L"alias", first));
    CHECK(!SameInstall(L"", first, L"alias", first));
    CHECK(!SameInstall(L"first", first, L"", first));
    for (bool channel : {false, true}) {
        auto other = first;
        auto& id = channel ? other.channel : other.bin;
        ++id.volume;
        CHECK(!SameInstall(L"same", first, L"SAME", other));
        id = channel ? first.channel : first.bin;
        ++id.file[0];
        CHECK(!SameInstall(L"same", first, L"SAME", other));
        id = channel ? first.channel : first.bin;
        ++id.file[15];
        CHECK(!SameInstall(L"same", first, L"SAME", other));
    }
    CHECK(SameInstall(L"missing", {}, L"MISSING", {}));
    CHECK(!SameInstall(L"missing", {}, L"other", {}));
    CHECK(!SameInstall(L"", {}, L"", {}));
    for (bool channel : {false, true}) {
        auto partial = first;
        (channel ? partial.channel : partial.bin).valid = false;
        CHECK(SameInstall(L"same", first, L"SAME", partial));
        CHECK(!SameInstall(L"different", first, L"alias", partial));
        auto different = first;
        (channel ? different.bin : different.channel).file[15] = 42;
        CHECK(!SameInstall(L"same", different, L"SAME", partial));
    }
    const char how[] = "remembered first";
    std::vector<Found> found;
    AppendUnique(found, L"", "ignored", Read);
    CHECK(found.empty());
    AppendUnique(found, L"first", how, Read);
    AppendUnique(found, L"alias", "later", Read);
    CHECK(found.size() == 1 && found[0].bin == L"first" && found[0].how == how);
    AppendUnique(found, L"other", "second install", Read);
    CHECK(found.size() == 2 && found[1].bin == L"other");
    AppendUnique(found, L"missing", "unreadable", Read);
    AppendUnique(found, L"MISSING", "same unreadable path", Read);
    AppendUnique(found, L"different missing", "keep", Read);
    CHECK(found.size() == 4 && found[2].bin == L"missing");
    std::cout << "install discovery: " << checks << " portable checks passed\n";
}
