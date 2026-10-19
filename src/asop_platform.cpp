// SPDX-License-Identifier: GPL-3.0-only
#include "asop_shell.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sco/runtime.h>
namespace offline::asop {
namespace {
bool Environment(const char* key, std::string& value) {
#ifdef _MSC_VER
    char* owned = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&owned, &length, key) != 0) return false;
    if (length > 32) { std::free(owned); return false; }
    value = owned ? owned : "";
    std::free(owned);
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
        // The selector plugin loaded during host startup. Missing handler or no
        // saved selection preserves the configured catalogue; other errors refuse.
        const char caller = 0;
        uint32_t count = 0;
        auto r = sco::InvokeRaw(&caller, "asop_selector.snapshot", nullptr, 0, nullptr, &count);
        if (r != sco::Result::NotFound) {
            if ((r != sco::Result::Ok && r != sco::Result::TooMany) || count > kMaxShips * 64)
                return Result::InvalidConfig;
            std::string selected(count, '\0');
            uint32_t copied = count;
            // A zero-length saved selection intentionally requests an empty list.
            if (count) r = sco::InvokeRaw(&caller, "asop_selector.snapshot", nullptr, 0, selected.data(), &copied);
            if ((count && r != sco::Result::Ok) || copied != count || !ApplySelection(selected, options))
                return Result::InvalidConfig;
        }
    }
    return Start(options, offlinePrepared, eacAbsent);
}
}
