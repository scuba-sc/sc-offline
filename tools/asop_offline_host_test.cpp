// SPDX-License-Identifier: GPL-3.0-only
// The real Offline composition and fixed-file adapter, with a synthetic game host and temporary files.
#include "offline_host.h"
#include <sco/app.h>
#include <sc_asop_selection.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static std::string directory;
static sco::game::host::Options options;
static const sc_asop_selection_v1* selection = nullptr;
static sco_plugin* owner = nullptr;
static int loadResult = 0;

bool ShipsFilePath(char* path, DWORD size) { return strcpy_s(path, size, (directory + "\\ships.txt").c_str()) == 0; }
bool DataFilePath(char* path, DWORD size, const char* file) { return strcpy_s(path, size, (directory + "\\" + file).c_str()) == 0; }
namespace sco::game::host {
Result Prepare(const Options& in) { options = in; return Result::Ok; }
Result Start() { return app::Start(options.platform) ? Result::Ok : Result::Failed; }
Result Tick(uint32_t now) { app::Tick(now); return Result::Ok; }
Result Stop() { app::Stop(); return Result::Ok; }
Snapshot Status() { Snapshot value; value.phase = Phase::Running; return value; }
}
static const sco_plugin_info* Query() {
    static const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "catalogueprobe", "1.0.0", "test"};
    return &info;
}
static sco_result Load(const sco_api* api, sco_plugin* self) {
    const void* raw = nullptr;
    loadResult = api->query_service(SC_ASOP_SELECTION_NAME, SC_ASOP_SELECTION_VERSION, &raw);
    selection = static_cast<const sc_asop_selection_v1*>(raw); owner = self;
    return SCO_OK;
}
static void Unload() { selection = nullptr; owner = nullptr; }
int main() {
    char temp[MAX_PATH]{};
    CHECK(GetTempPathA(MAX_PATH, temp) > 0);
    directory = std::string(temp) + "sco-asop-composition-" + std::to_string(GetCurrentProcessId());
    CHECK(CreateDirectoryA(directory.c_str(), nullptr));
    { std::ofstream f(directory + "\\ships.txt", std::ios::binary); f << "# ships\nA\nB\n"; CHECK(f.good()); }
    { std::ofstream f(directory + "\\asop_ships.txt", std::ios::binary); f << "A\nUnknown\n"; CHECK(f.good()); }
    const sco::plugins::Builtin builtin{"catalogueprobe", Query, Load, Unload};
    sco::game::host::Options in; in.platform.builtins = &builtin; in.platform.nBuiltins = 1;
    CHECK(offline_host::Prepare(in) == sco::Result::Ok && offline_host::Start() == sco::Result::Ok);
    CHECK(loadResult == SCO_OK && selection && owner);
    sc_asop_selection_state state{}; state.size = sizeof(state);
    CHECK(selection->state(owner, &state) == SCO_OK && (state.flags & SC_ASOP_SELECTION_FILE_KNOWN));
    sc_asop_selection_read read{}; read.size = sizeof(read); read.kind = SC_ASOP_SELECTION_ROWS_CATALOGUE;
    read.session = state.session; read.revision = state.catalogue_generation;
    sc_asop_selection_row rows[4]{}; for (auto& row : rows) row.size = sizeof(row);
    uint32_t count = 0, more = 0;
    CHECK(selection->rows(owner, &read, rows, sizeof(rows[0]), 4, &count, &more) == SCO_OK);
    CHECK(count == 2 && !more && std::strcmp(rows[0].class_name, "A") == 0 && std::strcmp(rows[1].class_name, "B") == 0);
    uint64_t lease = 0, requestId = 0;
    CHECK(selection->acquire(owner, state.session, &lease) == SCO_OK);
    sc_asop_selection_class selected{}; selected.size = sizeof(selected); std::strcpy(selected.class_name, "B");
    sc_asop_selection_request request{}; request.size = sizeof(request); request.session = state.session;
    request.file_revision = state.file_revision; request.client_key = 1; request.classes = &selected;
    request.count = 1; request.class_stride = sizeof(selected); std::memcpy(request.file_digest, state.file_digest, sizeof(request.file_digest));
    CHECK(selection->replace(owner, lease, &request, &requestId) == SCO_OK);
    CHECK(offline_host::Tick(1) == sco::Result::Ok);
    sc_asop_selection_receipt receipt{}; receipt.size = sizeof(receipt);
    CHECK(selection->status(owner, lease, requestId, &receipt) == SCO_OK && receipt.phase == SC_ASOP_SELECTION_FILE_COMMITTED);
    CHECK(offline_host::Stop() == sco::Result::Ok && !selection && !owner);
    std::ifstream f(directory + "\\asop_ships.txt", std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    CHECK(content.find("B\n") != std::string::npos);
    CHECK(DeleteFileA((directory + "\\ships.txt").c_str()));
    CHECK(DeleteFileA((directory + "\\asop_ships.txt").c_str()));
    CHECK(RemoveDirectoryA(directory.c_str()));
    std::puts("Offline host + real Windows ASOP file + full ships catalogue passed");
}
