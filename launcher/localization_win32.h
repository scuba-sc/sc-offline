// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <windows.h>
#include "localization.h"

namespace sco::launcher::locale {

enum class FileResult { Ready, Missing, Refused };

inline FileResult ReadLocaleFile(const std::wstring& path, std::string& out) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) return FileResult::Refused;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? FileResult::Missing : FileResult::Refused;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<ULONGLONG>(size.QuadPart) > kCatalogueLimit) {
        CloseHandle(file);
        return FileResult::Refused;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    size_t at = 0;
    while (at < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + at, static_cast<DWORD>(bytes.size() - at), &read, nullptr) || !read) {
            CloseHandle(file);
            return FileResult::Refused;
        }
        at += read;
    }
    CloseHandle(file);
    out = std::move(bytes);
    return FileResult::Ready;
}

inline bool SaveLanguage(const std::wstring& path, Language language) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) return false;
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    std::string before, next;
    if (ReadLocaleFile(path, before) == FileResult::Refused || !UpdateLanguageSetting(before, language, next)) return false;
    static LONG sequence = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        temporary = path + L".language." + std::to_wstring(GetCurrentProcessId()) + L"." +
                    std::to_wstring(static_cast<unsigned long>(InterlockedIncrement(&sequence)));
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    if (file == INVALID_HANDLE_VALUE) return false;
    bool saved = true;
    size_t at = 0;
    while (at < next.size()) {
        DWORD written = 0;
        if (!WriteFile(file, next.data() + at, static_cast<DWORD>(next.size() - at), &written, nullptr) || !written) {
            saved = false;
            break;
        }
        at += written;
    }
    saved = saved && FlushFileBuffers(file);
    CloseHandle(file);
    if (saved) saved = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!saved) DeleteFileW(temporary.c_str());
    return saved;
}

} // namespace sco::launcher::locale
