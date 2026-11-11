// SPDX-License-Identifier: GPL-3.0-only
// Private scratch directory for a synthetic quantum test process.
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

class QuantumTestDirectory {
public:
    explicit QuantumTestDirectory(const char* prefix) {
#ifdef _WIN32
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned n = 0; n < 1024; ++n) {
            const auto candidate = base / (std::string(prefix) + "-" + std::to_string(pid) + "-" + std::to_string(n));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error("create quantum test directory", candidate, error);
        }
        throw std::runtime_error("could not create unique quantum test directory");
    }
    QuantumTestDirectory(const QuantumTestDirectory&) = delete;
    QuantumTestDirectory& operator=(const QuantumTestDirectory&) = delete;
    ~QuantumTestDirectory() { cleanup(); }

    const std::filesystem::path& path() const { return path_; }
    bool cleanup() noexcept {
        if (path_.empty()) return true;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (!error) path_.clear();
        return !error;
    }
private:
    std::filesystem::path path_;
};
