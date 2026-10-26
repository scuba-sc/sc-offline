// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>
#include <pathcch.h>
#include <algorithm>
#include "install_discovery.h"

#pragma comment(lib, "pathcch.lib")

namespace sco::launcher::discovery {

struct InstallDirectories {
    bool valid = false;
    std::wstring bin;
    std::wstring channel;
};

inline InstallDirectories ResolveDirectories(const std::wstring& bin) {
    InstallDirectories result;
    constexpr DWORD capacity = 32768;
    if (bin.empty() || bin.size() > capacity - 3 || bin.find(L'\0') != std::wstring::npos)
        return result;
    const auto operational = bin + L"\\.";
    std::vector<wchar_t> buffer(capacity);
    if (bin.compare(0, 4, L"\\\\?\\") == 0) {
        // GetFullPathName strips literal trailing dots/spaces even in extended paths.
        if (FAILED(PathCchCanonicalizeEx(buffer.data(), buffer.size(), operational.c_str(),
                                        PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH))) return result;
    } else {
        const DWORD length = GetFullPathNameW(operational.c_str(), capacity, buffer.data(), nullptr);
        if (!length || length >= capacity) return result;
    }
    if (FAILED(PathCchAddBackslash(buffer.data(), buffer.size())) ||
        FAILED(PathCchRemoveBackslash(buffer.data(), buffer.size()))) return result;
    result.bin = buffer.data();
    if (FAILED(PathCchRemoveFileSpec(buffer.data(), buffer.size()))) return {};
    result.channel = buffer.data();
    result.valid = true;
    return result;
}

inline std::wstring ParentForSuffix(const std::wstring& path) {
    auto directories = ResolveDirectories(path);
    if (directories.valid) {
        if (!directories.channel.empty() && directories.channel.back() == L'\\')
            directories.channel.pop_back();
        return directories.channel;
    }
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(0, separator);
}

inline DirectoryIdentity ReadDirectoryIdentity(const std::wstring& path) {
    DirectoryIdentity result;
    const HANDLE handle = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return result;
    FILE_STANDARD_INFO standard{};
    FILE_ID_INFO identity{};
    const bool known = GetFileInformationByHandleEx(handle, FileStandardInfo,
        &standard, sizeof(standard)) && standard.Directory &&
        GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity));
    CloseHandle(handle);
    if (known) {
        result.valid = true;
        result.volume = identity.VolumeSerialNumber;
        std::copy_n(identity.FileId.Identifier, result.file.size(), result.file.begin());
    }
    return result;
}

inline InstallIdentity ReadInstallIdentity(const std::wstring& bin) {
    const auto directories = ResolveDirectories(bin);
    if (!directories.valid) return {};
    return {ReadDirectoryIdentity(directories.channel), ReadDirectoryIdentity(directories.bin)};
}

} // namespace sco::launcher::discovery
