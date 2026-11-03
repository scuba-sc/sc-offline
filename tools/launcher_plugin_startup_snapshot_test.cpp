// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/plugin_startup_snapshot.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace report = sco::launcher::plugin_startup;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { std::cerr << "can't read fixture " << path << '\n'; std::exit(2); }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static bool ReplaceOnce(std::string& text, const std::string& before, const std::string& after) {
    const size_t at = text.find(before);
    if (at == std::string::npos) return false;
    text.replace(at, before.size(), after);
    return true;
}

static void Reject(std::string text) {
    CHECK(report::Parse(text).status != report::Status::Complete);
}

int main(int argc, char** argv) {
    CHECK(argc == 5);
    const std::string complete = ReadFile(argv[1]);
    const std::string starting = ReadFile(argv[2]);
    const std::string writerEnabled = ReadFile(argv[3]);
    const std::string writerDisabled = ReadFile(argv[4]);

    const auto parsed = report::Parse(complete);
    CHECK(parsed.status == report::Status::Complete);
    CHECK(parsed.snapshot.sessionId == "0123456789abcdef0123456789abcdef");
    CHECK(parsed.snapshot.startedAtUtc == "2020-01-02T03:04:05Z");
    CHECK(parsed.snapshot.completedAtUtc == "2020-01-02T03:04:09Z"); // old timestamps remain snapshots; UI always warns
    CHECK(parsed.snapshot.pluginsEnabledAtStart);
    CHECK(parsed.snapshot.pluginCount == 3 && parsed.snapshot.loadedCount == 1);
    CHECK(parsed.snapshot.plugins.size() == 3);
    CHECK(report::FindPlugin(parsed.snapshot, "sim_alpha") != nullptr);
    CHECK(report::FindPlugin(parsed.snapshot, "sim_alpha")->state == "loaded");
    CHECK(report::FindPlugin(parsed.snapshot, "sim_beta")->restartRequired);
    CHECK(report::FindPlugin(parsed.snapshot, "unknown_2") == nullptr); // invalid manifest aliases never bind to a current folder
    CHECK(report::FindPlugin(parsed.snapshot, "../sim_alpha") == nullptr);

    const auto emittedEnabled = report::Parse(writerEnabled);
    CHECK(emittedEnabled.status == report::Status::Complete);
    CHECK(emittedEnabled.snapshot.pluginCount == 9 && emittedEnabled.snapshot.loadedCount == 1);
    CHECK(emittedEnabled.snapshot.pluginsEnabledAtStart);
    CHECK(report::FindPlugin(emittedEnabled.snapshot, "disabled_valid")->state == "disabled");
    CHECK(report::FindPlugin(emittedEnabled.snapshot, "faulted_valid")->restartRequired);
    CHECK(report::FindPlugin(emittedEnabled.snapshot, "unknown_7") == nullptr);
    const auto emittedDisabled = report::Parse(writerDisabled);
    CHECK(emittedDisabled.status == report::Status::Complete);
    CHECK(emittedDisabled.snapshot.pluginCount == 2 && emittedDisabled.snapshot.loadedCount == 0);
    CHECK(!emittedDisabled.snapshot.pluginsEnabledAtStart);
    CHECK(report::FindPlugin(emittedDisabled.snapshot, "off_valid")->state == "off");
    CHECK(report::FindPlugin(emittedDisabled.snapshot, "unknown_1") == nullptr);

    CHECK(report::Parse(starting).status == report::Status::NoCompletedSnapshot);
    std::string startingWithNumericCounts = starting;
    CHECK(ReplaceOnce(startingWithNumericCounts, "\"plugin_count\": null", "\"plugin_count\": 0"));
    CHECK(ReplaceOnce(startingWithNumericCounts, "\"loaded_count\": null", "\"loaded_count\": 0"));
    CHECK(report::Parse(startingWithNumericCounts).status == report::Status::Unknown);
    std::string failed = starting;
    CHECK(ReplaceOnce(failed, "\"record_state\": \"starting\"", "\"record_state\": \"failed\""));
    CHECK(ReplaceOnce(failed, "\"completed_at_utc\": null", "\"completed_at_utc\": \"2026-11-03T12:00:04Z\""));
    CHECK(report::Parse(failed).status == report::Status::NoCompletedSnapshot);
    std::string failedWithNumericCounts = failed;
    CHECK(ReplaceOnce(failedWithNumericCounts, "\"plugin_count\": null", "\"plugin_count\": 0"));
    CHECK(ReplaceOnce(failedWithNumericCounts, "\"loaded_count\": null", "\"loaded_count\": 0"));
    CHECK(report::Parse(failedWithNumericCounts).status == report::Status::Unknown);
    CHECK(ReplaceOnce(failed, "2026-11-03T12:00:00Z", "2026-11-03T12:00:05Z"));
    Reject(failed); // finalized incomplete records still need chronological timestamps
    CHECK(report::Parse(complete.substr(0, complete.size() / 2)).status == report::Status::Unknown);
    CHECK(report::Parse(complete + " trailing").status == report::Status::Unknown);

    std::string changed = complete;
    CHECK(ReplaceOnce(changed, "\"schema_version\": 1", "\"schema_version\": 2"));
    CHECK(report::Parse(changed).status == report::Status::Unsupported);

    changed = complete;
    CHECK(ReplaceOnce(changed, "\"plugin_count\": 3", "\"plugin_count\": 4"));
    Reject(changed);
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"loaded_count\": 1", "\"loaded_count\": 2"));
    Reject(changed);
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"id\": \"sim_beta\"", "\"id\": \"sim_alpha\""));
    Reject(changed); // duplicate ids are ambiguous

    changed = complete;
    CHECK(ReplaceOnce(changed, "\"schema_version\": 1,", "\"schema_version\": 1, \"schema_version\": 1,"));
    Reject(changed); // duplicate fields
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"restart_required\": true", "\"restart_required\": false"));
    Reject(changed); // restart flag must match faulted state
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"reason_code\": \"provider_fault_restart_required\"",
                             "\"reason_code\": \"plugin_disabled\""));
    Reject(changed); // a fixed reason code must agree with its state
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"reason_code\": \"provider_fault_restart_required\"",
                             "\"reason_code\": \"plugin says stop\\nnow\""));
    Reject(changed); // controls and free-form reason text are not part of v1
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"reason_code\": \"provider_fault_restart_required\"",
                             std::string("\"reason_code\": \"") + char(0xc0) + char(0xaf) + "\""));
    Reject(changed); // non-UTF-8 / non-ASCII display text is rejected
    changed = complete;
    CHECK(ReplaceOnce(changed, "2020-01-02T03:04:09Z", "2020-02-30T03:04:09Z"));
    Reject(changed);
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"plugins_enabled_at_start\": true", "\"plugins_enabled_at_start\": false"));
    CHECK(report::Parse(changed).status == report::Status::Complete); // setting at startup is data, not current UI intent
    changed = complete;
    CHECK(ReplaceOnce(changed, "\"schema_version\": 1,", "\"schema_version\": 1, \"future_field\": true,"));
    Reject(changed); // closed schema: do not guess at fields the launcher does not understand

    Reject(std::string(report::kMaxFileBytes + 1, ' '));
    std::string tooMany = complete;
    const size_t arrayStart = tooMany.find("\"plugins\": [");
    const size_t arrayEnd = tooMany.rfind("\n  ]");
    CHECK(arrayStart != std::string::npos && arrayEnd != std::string::npos);
    const std::string sampleRow = "{\"id\":\"extra_";
    std::string rows;
    for (size_t i = 0; i < report::kMaxPlugins + 1; ++i) {
        if (i) rows += ',';
        rows += sampleRow + std::to_string(i) + "\",\"manifest_valid\":true,\"version\":\"1\",\"kind\":\"data\",\"state\":\"ready\",\"reason_code\":null,\"restart_required\":false}";
    }
    tooMany.replace(arrayStart + std::string("\"plugins\": [").size(), arrayEnd - (arrayStart + std::string("\"plugins\": [").size()), rows);
    CHECK(tooMany.size() < report::kMaxFileBytes);
    Reject(tooMany);

    std::cout << "plugin startup snapshot parser: " << checks << " checks passed\n";
    return 0;
}
