// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>
namespace offline::asop {
namespace {
bool Environment(const char* key, std::string& value) {
#ifdef _MSC_VER
    char bytes[32]{};
    std::size_t required = 0;
    if (getenv_s(&required, bytes, sizeof(bytes), key) != 0) return false;
    if (required == 0) { value.clear(); return true; }
    value.assign(bytes, required - 1);
#else
    const char* bytes = std::getenv(key);
    if (!bytes) { value.clear(); return true; }
    std::size_t n = 0;
    while (n < 32 && bytes[n]) ++n;
    if (n == 32) return false;
    value.assign(bytes, n);
#endif
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return true;
}
// A missing file is an empty selection only while its directory resolves; any other failure is unknown and refuses startup.
bool ReadSelection(const char* path, std::string& bytes) {
    std::error_code ec;
    const std::filesystem::path file(path);
    const auto state = std::filesystem::status(file, ec);
    if (state.type() == std::filesystem::file_type::not_found) {
        ec.clear();
        return std::filesystem::is_directory(file.parent_path(), ec) && !ec;
    }
    if (ec || state.type() != std::filesystem::file_type::regular) return false;
    std::ifstream input(file, std::ios::binary);
    char c = 0;
    while (bytes.size() <= kMaxSelectionBytes && input.get(c)) bytes.push_back(c);
    return input.eof();
}
}
bool ParseShips(const std::string& bytes, std::vector<std::string>& out) {
    if (bytes.size() > kMaxSelectionBytes) return false;
    std::istringstream input(bytes);
    std::vector<std::string> next;
    std::set<std::string> seen; // lowercase keys: case-insensitive uniqueness
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        const auto begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos) continue;
        line = line.substr(begin, line.find_last_not_of(" \t") - begin + 1);
        if (line.size() > 63) return false;
        for (unsigned char c : line)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        std::string key = line;
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!seen.insert(key).second) continue;
        if (next.size() == sco::game::asop_host::kMaxShips) return false;
        next.push_back(line);
    }
    out = std::move(next);
    return true;
}
// Host policy only: environment/filesystem inputs, no raw mechanics or session broker.
sco::game::asop_host::Result StartFromEnvironment(bool offlinePrepared, bool eacAbsent, const char* selectionPath) {
    using namespace sco::game::asop_host;
    Options options;
    std::string value;
    if (!Environment("SC_OFFLINE_ASOP", value)) return Result::InvalidConfig;
    if (value == "off" || value == "0" || value == "no" || value == "false") options.enabled = false;
    else if (!value.empty() && value != "on" && value != "1" && value != "yes" && value != "true") return Result::InvalidConfig;
    if (!Environment("SC_OFFLINE_ASOP_FLEET_LIST", value)) return Result::InvalidConfig;
    if (value == "ships") options.catalog = CatalogMode::Ships;
    else if (!value.empty() && value != "game") return Result::InvalidConfig;
    if (!options.enabled) return Start(options, offlinePrepared, eacAbsent);
    if (!offlinePrepared || !eacAbsent) return Result::AdmissionRefused;
    if (options.catalog == CatalogMode::Ships) {
        if (!selectionPath || !*selectionPath) return Result::InvalidConfig;
        std::string bytes;
        if (!ReadSelection(selectionPath, bytes) || !ParseShips(bytes, options.ships)) return Result::InvalidConfig;
    }
    return Start(options, offlinePrepared, eacAbsent);
}
}
