// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/install_identity_win32.h"
#include <cstdlib>
#include <iostream>

using namespace sco::launcher::discovery;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static void Print(const wchar_t* name, const InstallIdentity& identity) {
    std::wcout << name;
    for (const auto& directory : {identity.channel, identity.bin}) {
        std::wcout << L" valid=" << directory.valid << L" volume=" << std::hex << directory.volume << L" file=";
        for (auto byte : directory.file) std::wcout << static_cast<unsigned>(byte) << L":";
    }
    std::wcout << std::dec << L"\n";
}

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    const std::wstring c = argv[1];
    const std::wstring root = argv[2];
    const auto z = root + L"\\source\\LIVE\\Bin64";
    const auto cIdentity = ReadInstallIdentity(c);
    const auto zIdentity = ReadInstallIdentity(z);
    Print(L"C alias", cIdentity);
    Print(L"Z source", zIdentity);
    CHECK(cIdentity.channel.valid && cIdentity.bin.valid);
    CHECK(zIdentity.channel.valid && zIdentity.bin.valid);
    CHECK(Equal(cIdentity.channel, zIdentity.channel));
    CHECK(Equal(cIdentity.bin, zIdentity.bin));
    CHECK(SameInstall(c, cIdentity, z, zIdentity));
    for (bool cFirst : {false, true}) {
        std::vector<Found> found;
        const auto& first = cFirst ? c : z;
        const auto& second = cFirst ? z : c;
        const char reason[] = "first discovery reason";
        AppendUnique(found, first, reason, ReadInstallIdentity);
        AppendUnique(found, second, "later alias", ReadInstallIdentity);
        CHECK(found.size() == 1 && found[0].bin == first && found[0].how == reason);
        for (const auto* suffix : {L"\\other\\LIVE\\Bin64", L"\\source\\PTU\\Bin64",
                                   L"\\shared\\LIVE\\Bin64"}) {
            const auto path = root + suffix;
            const auto identity = ReadInstallIdentity(path);
            Print(suffix, identity);
            CHECK(identity.channel.valid && identity.bin.valid);
            CHECK(!SameInstall(z, zIdentity, path, identity));
            const auto before = found.size();
            AppendUnique(found, path, "separate install", ReadInstallIdentity);
            CHECK(found.size() == before + 1 && found.back().bin == path);
        }
        CHECK(found.size() == 4);
    }
    std::cout << "install discovery Wine: " << checks << " checks passed\n";
    return 0;
}
