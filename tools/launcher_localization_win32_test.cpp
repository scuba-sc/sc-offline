// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/localization_win32.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

using namespace sco::launcher::locale;
namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static void Put(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    CHECK(bool(output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))));
}

static std::string Get(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(input.good());
    const auto length = input.tellg();
    CHECK(length >= 0 && length <= static_cast<std::streamoff>(kCatalogueLimit + 1));
    std::string bytes(static_cast<size_t>(length), '\0');
    input.seekg(0);
    CHECK(bool(input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))));
    return bytes;
}

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    fs::path root(argv[1]);
    root.make_preferred();
    root /= L"unicode-\u044f-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CHECK(fs::create_directories(root));
    const auto file = root / L"sc-offline.ini";
    std::string bytes = "kept";
    CHECK(ReadLocaleFile(file.wstring(), bytes) == FileResult::Missing && bytes == "kept");
    CHECK(SaveLanguage(file.wstring(), Language::Russian));
    CHECK(Get(file) == "language = ru\r\n");
    const std::string original = "\xEF\xBB\xBF# kept\r\nplugins = off\r\nlanguage=ru\r\n# language=auto\r\nLanguage=auto\r\ncheck_updates=off\r\n";
    Put(file, original);
    CHECK(SaveLanguage(file.wstring(), Language::English));
    const std::string expected = "\xEF\xBB\xBF# kept\r\nplugins = off\r\nlanguage = en\r\n# language=auto\r\nlanguage = en\r\ncheck_updates=off\r\n";
    CHECK(Get(file) == expected);
    bytes = "kept";
    CHECK(ReadLocaleFile(L"", bytes) == FileResult::Refused && bytes == "kept");
    const auto nul_path = file.wstring() + std::wstring(L"\0suffix", 7);
    CHECK(ReadLocaleFile(nul_path, bytes) == FileResult::Refused && bytes == "kept");
    CHECK(!SaveLanguage(L"", Language::English));
    CHECK(!SaveLanguage(nul_path, Language::English));
    const auto linked = root / L"linked.ini";
    DWORD link_flags = 0;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    link_flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
    if (CreateSymbolicLinkW(linked.c_str(), file.c_str(), link_flags)) {
        CHECK(!SaveLanguage(linked.wstring(), Language::Russian));
        CHECK(fs::is_symlink(linked));
        CHECK(Get(file) == expected);
        CHECK(fs::remove(linked));
        std::cout << "INI reparse preservation: tested\n";
    } else {
        std::cout << "INI reparse preservation: fixture unavailable, Windows error " << GetLastError() << '\n';
    }
    CHECK(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
    CHECK(!SaveLanguage(file.wstring(), Language::Auto));
    CHECK(Get(file) == expected);
    CHECK(SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(!SaveLanguage(root.wstring(), Language::English));
    CHECK(!SaveLanguage((root / L"missing" / L"sc-offline.ini").wstring(), Language::English));
    CHECK(!SaveLanguage(file.wstring(), static_cast<Language>(999)));
    CHECK(Get(file) == expected);
    const auto bad = root / L"invalid.ini";
    const std::string invalid = "language=\xC0\xAF\n";
    Put(bad, invalid);
    CHECK(!SaveLanguage(bad.wstring(), Language::English));
    CHECK(Get(bad) == invalid);
    const auto large = root / L"large.ini";
    const std::string big(kCatalogueLimit + 1, 'x');
    Put(large, big);
    bytes = "kept";
    CHECK(ReadLocaleFile(large.wstring(), bytes) == FileResult::Refused && bytes == "kept");
    CHECK(!SaveLanguage(large.wstring(), Language::English));
    CHECK(Get(large) == big);
    DWORD before = 0, after = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (unsigned loop = 0; loop < 30; ++loop) {
        CHECK(SaveLanguage(file.wstring(), loop % 2 ? Language::Auto : Language::Russian));
        CHECK(ReadLocaleFile(file.wstring(), bytes) == FileResult::Ready);
        CHECK(ReadLocaleFile(large.wstring(), bytes) == FileResult::Refused);
        CHECK(ReadLocaleFile((root / L"missing.ini").wstring(), bytes) == FileResult::Missing);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    CHECK(before == after);
    for (const auto& entry : fs::directory_iterator(root))
        CHECK(entry.path().filename().wstring().find(L".language.") == std::wstring::npos);
    std::cout << "launcher locale Win32: " << checks << " checks passed (owned files only)\n";
    return 0;
}
