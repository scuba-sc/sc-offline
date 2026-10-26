// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/install_identity_win32.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace sco::launcher::discovery;
namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static InstallIdentity Read(const std::wstring& path) {
    return ReadInstallIdentity(path);
}

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 3);
    fs::path root(argv[1]);
    root.make_preferred();
    const auto original = root / L"source" / L"LIVE" / L"Bin64";
    const auto alias = root / L"alias" / L"Bin64";
    const auto other = root / L"other" / L"LIVE" / L"Bin64";
    const auto ptu = root / L"source" / L"PTU" / L"Bin64";
    const auto shared = root / L"shared" / L"LIVE" / L"Bin64";
    const auto identity = Read(original.wstring());
    CHECK(identity.channel.valid && identity.bin.valid);
    CHECK(SameInstall(original.wstring(), identity, alias.wstring(), Read(alias.wstring())));
    CHECK(!SameInstall(original.wstring(), identity, other.wstring(), Read(other.wstring())));
    CHECK(!SameInstall(original.wstring(), identity, ptu.wstring(), Read(ptu.wstring())));
    const auto shared_identity = Read(shared.wstring());
    CHECK(shared_identity.channel.valid && shared_identity.bin.valid);
    CHECK(Equal(identity.bin, shared_identity.bin));
    CHECK(!Equal(identity.channel, shared_identity.channel));
    CHECK(!SameInstall(original.wstring(), identity, shared.wstring(), shared_identity));
    CHECK(!ReadDirectoryIdentity((original / L"StarCitizen.exe").wstring()).valid);
    CHECK(!ReadDirectoryIdentity((root / L"missing").wstring()).valid);
    CHECK(!ResolveDirectories(L"").valid);
    CHECK(!ResolveDirectories(std::wstring(32766, L'x')).valid);
    CHECK(!ResolveDirectories(std::wstring(L"a\0b", 3)).valid);
    const auto missing = (root / L"missing").wstring();
    CHECK(SameInstall(missing, Read(missing), missing, Read(missing)));
    std::vector<Found> found;
    const char first[] = "remembered";
    AppendUnique(found, original.wstring(), first, Read);
    AppendUnique(found, alias.wstring(), "alias", Read);
    AppendUnique(found, other.wstring(), "hardlinked exe", Read);
    AppendUnique(found, ptu.wstring(), "PTU", Read);
    AppendUnique(found, shared.wstring(), "shared Bin64", Read);
    CHECK(found.size() == 4 && found[0].bin == original.wstring() && found[0].how == first);
    const auto cwd = fs::current_path();
    CHECK(SetCurrentDirectoryW(original.parent_path().c_str()));
    const auto relative = Read(L"Bin64");
    CHECK(relative.channel.valid && relative.bin.valid);
    CHECK(Equal(relative.channel, identity.channel) && Equal(relative.bin, identity.bin));
    CHECK(Equal(ReadDirectoryIdentity(ParentForSuffix(L"Bin64")), identity.channel));
    std::vector<Found> relative_found;
    AppendUnique(relative_found, L"Bin64", first, Read);
    AppendUnique(relative_found, original.wstring(), "absolute alias", Read);
    CHECK(relative_found.size() == 1 && relative_found[0].bin == L"Bin64" && relative_found[0].how == first);
    const auto nested_relative = Read(L"Bin64\\Bin64");
    CHECK(nested_relative.bin.valid && nested_relative.channel.valid);
    CHECK(Equal(relative.bin, nested_relative.bin));
    CHECK(!Equal(relative.channel, nested_relative.channel));
    CHECK(!SameInstall(L"Bin64", relative, L"Bin64\\Bin64", nested_relative));
    CHECK(SetCurrentDirectoryW(original.c_str()));
    CHECK(Equal(Read(L".").channel, identity.channel) && Equal(Read(L".").bin, identity.bin));
    CHECK(SetCurrentDirectoryW((original / L"child").c_str()));
    CHECK(Equal(Read(L"..").channel, identity.channel) && Equal(Read(L"..").bin, identity.bin));
    CHECK(SetCurrentDirectoryW(cwd.c_str()));
    const std::wstring drive(argv[2]);
    CHECK(SetCurrentDirectoryW((drive + L"\\Other").c_str()));
    const auto root_bin = drive + L"\\Bin64";
    const auto nested_bin = drive + L"\\Other\\Bin64";
    const auto root_id = Read(root_bin), nested_id = Read(nested_bin);
    CHECK(root_id.channel.valid && root_id.bin.valid && nested_id.channel.valid && nested_id.bin.valid);
    CHECK(Equal(root_id.bin, nested_id.bin));
    CHECK(!Equal(root_id.channel, nested_id.channel));
    CHECK(!SameInstall(root_bin, root_id, nested_bin, nested_id));
    std::vector<Found> rooted;
    AppendUnique(rooted, root_bin, "root", Read);
    AppendUnique(rooted, nested_bin, "nested", Read);
    CHECK(rooted.size() == 2 && rooted[0].bin == root_bin);
    const auto drive_root = Read(drive), absolute_root = Read(drive + L"\\");
    CHECK(drive_root.bin.valid && drive_root.channel.valid && absolute_root.bin.valid && absolute_root.channel.valid);
    CHECK(Equal(drive_root.bin, absolute_root.bin) && Equal(drive_root.channel, absolute_root.channel));
    const auto direct_other = Read(drive + L"\\Other");
    CHECK(!SameInstall(drive, drive_root, drive + L"\\Other", direct_other));
    const auto drive_relative = Read(drive + L"Bin64");
    CHECK(Equal(drive_relative.bin, nested_id.bin) && Equal(drive_relative.channel, nested_id.channel));
    CHECK(SetCurrentDirectoryW((drive + L"\\").c_str()));
    CHECK(Equal(Read(drive + L"Bin64").channel, root_id.channel));
    CHECK(Equal(Read(drive + L"Bin64").bin, root_id.bin));
    const auto extended_root = L"\\\\?\\" + drive;
    const auto extended_identity = Read(extended_root);
    CHECK(Equal(extended_identity.bin, absolute_root.bin) && Equal(extended_identity.channel, absolute_root.channel));
    CHECK(ReadDirectoryIdentity(ParentForSuffix(extended_root) + L"\\root-user").valid);
    CHECK(SetCurrentDirectoryW(cwd.c_str()));
    struct Literal { const wchar_t* channel; const wchar_t* bin; };
    for (const auto& literal : {Literal{L"literal-dot.", L"Bin64"},
                               Literal{L"literal-space ", L"Bin64"},
                               Literal{L"literal-bin-dot", L"Bin64."},
                               Literal{L"literal-bin-space", L"Bin64 "}}) {
        const auto channel = L"\\\\?\\" + (root / literal.channel).wstring();
        const auto bin = channel + L"\\" + literal.bin;
        const auto observed = Read(bin);
        const auto channel_id = ReadDirectoryIdentity(channel), bin_id = ReadDirectoryIdentity(bin);
        CHECK(channel_id.valid && bin_id.valid && observed.channel.valid && observed.bin.valid);
        CHECK(Equal(channel_id, observed.channel) && Equal(bin_id, observed.bin));
        CHECK(Equal(ReadDirectoryIdentity(ParentForSuffix(bin)), channel_id));
    }
    struct RootCase { const wchar_t* bin; const wchar_t* parent; };
    for (const auto& path : {RootCase{L"C:\\Bin64", L"C:"}, {L"C:\\", L"C:"},
                            {L"\\\\server\\share\\Bin64", L"\\\\server\\share"},
                            {L"\\\\server\\share\\", L"\\\\server\\share"},
                            {L"\\\\?\\C:\\Bin64", L"\\\\?\\C:"}, {L"\\\\?\\C:\\", L"\\\\?\\C:"},
                            {L"\\\\?\\UNC\\server\\share\\Bin64", L"\\\\?\\UNC\\server\\share"},
                            {L"\\\\?\\UNC\\server\\share\\", L"\\\\?\\UNC\\server\\share"},
                            {L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\Bin64",
                             L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}"},
                            {L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\",
                             L"\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}"}}) {
        const auto resolved = ResolveDirectories(path.bin);
        CHECK(resolved.valid && !resolved.bin.empty() && !resolved.channel.empty());
        CHECK(ParentForSuffix(path.bin) == path.parent);
    }
    DWORD before = 0, after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(Read(original.wstring()).bin.valid);
        CHECK(!ReadDirectoryIdentity(missing).valid);
        CHECK(!ReadDirectoryIdentity((original / L"StarCitizen.exe").wstring()).valid);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    CHECK(before == after);
    std::cout << "install discovery: " << checks << " Windows filesystem checks passed\n";
}
