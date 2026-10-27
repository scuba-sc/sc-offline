// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
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
}
// Host policy only: environment/filesystem inputs, no raw mechanics or session broker.
sco::game::asop_host::Result StartFromEnvironment(bool offlinePrepared, bool eacAbsent, const char* shipsPath) {
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
        if (!shipsPath || !*shipsPath) return Result::InvalidConfig;
        std::ifstream input(shipsPath, std::ios::binary);
        std::string bytes;
        char c;
        while (bytes.size() <= kMaxShips * 128 && input.get(c)) bytes.push_back(c);
        if (!input.eof() || !ParseShips(bytes, options.ships)) return Result::InvalidConfig;
    }
    return Start(options, offlinePrepared, eacAbsent);
}
}
