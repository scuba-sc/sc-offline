// SPDX-License-Identifier: GPL-3.0-only
// Actual module, synthetic copied metadata only; no engine installation evidence.
#include "workflow_module.h"
#include <sco_content.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace {
unsigned checks = 0, queries = 0, reads = 0;
sco_plugin* owner = reinterpret_cast<sco_plugin*>(uintptr_t{1});
std::map<std::string, sco_command> commands;
std::string reply;
const char* guid = "0123456789abcdefABCDEF0123456789";
struct File { std::string pack, name, bytes; };
std::vector<File> files;
const void* table = nullptr;
sco_result admission = SCO_OK;
bool badKind = false, badId = false, badSize = false, badName = false, badPack = false;
bool changedSize = false, badProbe = false, zeroTooMany = false;
uint32_t extraSize = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "metadata: %s; reply=%s\n", why, reply.c_str()); std::exit(1); }
}
sco_result Register(sco_plugin* self, const sco_command* command) {
    Check(self == owner, "command owner"); commands[command->name] = *command; return SCO_OK;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    Check(version == SCO_CONTENT_VERSION_1_0, "requested version");
    Check(!std::strcmp(name, SCO_CONTENT_NAME), "metadata never queries missions/economy/ingest");
    ++queries; *out = table; return table ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Item(sco_plugin* self, sco_content_kind kind, uint32_t ordinal, sco_content_item* out) {
    Check(self == owner && kind == SCO_CONTENT_LIST && out->size == sizeof(*out), "copied List request");
    if (admission != SCO_OK) return admission;
    if (ordinal >= files.size()) return SCO_NOT_FOUND;
    out->size = badSize ? 1u : sizeof(*out); out->kind = badKind ? SCO_CONTENT_SCRIPT : kind;
    out->id = badId ? 0 : ordinal + 1;
    std::snprintf(out->pack, sizeof(out->pack), "%s", files[ordinal].pack.c_str());
    std::snprintf(out->name, sizeof(out->name), "%s", files[ordinal].name.c_str());
    if (badName) std::memset(out->name, 'x', sizeof(out->name));
    if (badPack) std::memset(out->pack, 'x', sizeof(out->pack));
    return SCO_OK;
}
sco_result Read(sco_plugin* self, uint64_t id, void* out, uint32_t* size) {
    Check(self == owner && id && id <= files.size(), "read exact selected ID and owner"); ++reads;
    if (admission != SCO_OK) return admission;
    const auto& bytes = files[id - 1].bytes;
    const auto need = static_cast<uint32_t>(bytes.size()) + extraSize;
    if (!out) { *size = zeroTooMany ? 0 : need; return badProbe ? SCO_OK : zeroTooMany || need ? SCO_TOO_MANY : SCO_OK; }
    Check(*size >= bytes.size(), "read capacity");
    std::memcpy(out, bytes.data(), bytes.size()); *size = static_cast<uint32_t>(bytes.size()) + (changedSize ? 1u : 0u); return SCO_OK;
}
sco_content_v1 content{sizeof(content), 0, Item, Read};
sco_api api{};
sco_arg Int(int64_t n) { sco_arg a{}; a.type = SCO_ARG_INT; a.v.i = n; return a; }
sco_result Call(const char* name, std::initializer_list<sco_arg> args = {}, uint32_t capacity = 1024) {
    auto it = commands.find(name); if (it == commands.end()) return SCO_NOT_FOUND;
    char text[1024]{};
    auto r = it->second.fn(args.begin(), static_cast<uint32_t>(args.size()), it->second.ctx, text, capacity);
    reply = text; return r;
}
void Reset() {
    files = {{"first", "lists/contract_scripts.txt", std::string("# authored fixture\n") + guid + " Alpha one.xml|-|two.xml\n" + guid + " Duplicate -\n"},
             {"second", "lists/contract_scripts.txt", std::string(guid) + " Other three.xml\n"}};
}
void Good() { Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_OK, "read metadata"); }
}
int main(int argc, char** argv) {
    Check(argc == 2, "module path"); test_module::Open(argc, argv);
    api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.query_service = Query; api.register_command = Register;
    Reset(); table = &content;
    Check(test_module::Load(&api, owner) == SCO_OK, "load actual module");
    Good(); Check(reply.find("source GUID " + std::string(guid)) != reply.npos && reply.find("references 2") != reply.npos && reply.find("UNKNOWN/UNAVAILABLE") != reply.npos && reply.find("not a mission offer ID") != reply.npos, "summary provenance and unknown native truth");
    Check(Call("contracts.reference", {Int(0), Int(0), Int(1)}) == SCO_OK && reply.find("two.xml") != reply.npos, "single reference order, dash excluded");
    Check(Call("contracts.references", {Int(0), Int(1)}) == SCO_OK && reply.find("references 0") != reply.npos, "dash is no reference, not installed");
    Check(Call("contracts.reference", {Int(0), Int(1), Int(0)}) == SCO_NOT_FOUND, "no reference for dash");
    Check(Call("contracts.references", {Int(1), Int(0)}) == SCO_OK && reply.find("Pack second") != reply.npos, "same-name packs independently selected");
    Check(Call("contracts.references", {Int(2), Int(0)}) == SCO_NOT_FOUND, "end List ordinal");
    Check(Call("contracts.references", {Int(0), Int(2)}) == SCO_NOT_FOUND, "end row");
    Check(Call("contracts.cancel") == SCO_OK, "cancel mutations"); Good();
    Check(Call("contracts.references", {Int(-1), Int(0)}) == SCO_BAD_ARG, "negative index");
    Check(Call("contracts.references", {Int(4096), Int(0)}) == SCO_BAD_ARG, "List index cap");
    Check(Call("contracts.references", {Int(0), Int(16384)}) == SCO_BAD_ARG, "row index cap");
    Check(Call("contracts.reference", {Int(0), Int(0), Int(32)}) == SCO_BAD_ARG, "reference index cap");
    Check(Call("contracts.references", {Int(0)}) == SCO_BAD_ARG, "argument count");
    Check(Call("contracts.references", {Int(0), Int(0)}, 8) == SCO_TOO_MANY, "no silently truncated success");
    admission = SCO_WRONG_THREAD; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_WRONG_THREAD, "thread refusal");
    admission = SCO_UNAVAILABLE; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE, "terminal admission refusal"); admission = SCO_OK;
    for (bool* flag : {&badKind, &badId, &badSize, &badName, &badPack}) { *flag = true; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_BAD_ARG, "malformed copied record"); *flag = false; }
    files[0].name = "lists/other.txt"; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_BAD_ARG, "canonical name only"); Reset();
    changedSize = true; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_FAILED, "exact byte count"); changedSize = false;
    badProbe = true; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_FAILED, "invalid successful size probe"); badProbe = false;
    zeroTooMany = true; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_FAILED, "TOO_MANY must report a positive size"); zeroTooMany = false;
    extraSize = SCO_CONTENT_MAX_FILE_BYTES + 1; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "copy bound"); extraSize = 0;
    for (const std::string& bad : {std::string("not a row\n"), std::string(guid) + " A a||b\n", std::string(guid) + " A \n", std::string(guid) + " A a|\n", std::string("\0", 1)}) {
        files[0].bytes += bad; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_BAD_ARG && reply.find("no partial") != reply.npos, "whole-file malformed refusal"); Reset();
    }
    files[0].bytes = std::string(guid) + " " + std::string(256, 'a') + " -\n"; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "name bound");
    files[0].bytes = std::string(guid) + " " + std::string(255, 'a') + " " + std::string(255, 'b');
    Good(); const auto exactCapacity = static_cast<uint32_t>(reply.size() + 1);
    Check(Call("contracts.references", {Int(0), Int(0)}, exactCapacity) == SCO_OK, "maximum supported name, exact reply capacity");
    Check(Call("contracts.references", {Int(0), Int(0)}, exactCapacity - 1) == SCO_TOO_MANY, "one-byte-short reply refuses");
    Check(Call("contracts.reference", {Int(0), Int(0), Int(0)}) == SCO_OK, "maximum supported reference, final line without LF");
    files[0].bytes = std::string(guid) + " A " + std::string(256, 'a') + "\n"; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "reference bound");
    files[0].bytes = std::string(guid) + " A a"; for (unsigned i = 1; i < 33; ++i) files[0].bytes += "|a";
    Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "reference count bound");
    files[0].bytes = std::string(8192, '#'); Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "physical line bound");
    files[0].bytes.clear(); for (unsigned i = 0; i < 16385; ++i) files[0].bytes += std::string(guid) + " A -\n";
    Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_TOO_MANY, "row count bound");
    files[0].bytes.clear(); Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_NOT_FOUND, "empty copied file"); Reset();
    table = nullptr; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE, "missing public content");
    for (size_t length : {offsetof(sco_content_v1, item) + 1, offsetof(sco_content_v1, read) + 1}) {
        void* partial = std::malloc(length); Check(partial != nullptr, "allocate physical short table"); std::memset(partial, 0, length); *static_cast<uint32_t*>(partial) = static_cast<uint32_t>(length);
        if (length > offsetof(sco_content_v1, read)) std::memcpy(static_cast<char*>(partial) + offsetof(sco_content_v1, item), &content.item, sizeof(content.item));
        table = partial; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE, "complete pointer extent"); std::free(partial);
    }
    table = &content;
    auto itemFn = content.item; content.item = nullptr; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE, "null item"); content.item = itemFn;
    auto readFn = content.read; content.read = nullptr; Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE, "null read"); content.read = readFn;
    const auto before = queries; test_module::Unload(); test_module::Unload();
    Check(Call("contracts.references", {Int(0), Int(0)}) == SCO_UNAVAILABLE && queries == before, "unload admission, no late query");
    commands.clear(); owner = reinterpret_cast<sco_plugin*>(uintptr_t{2}); Check(test_module::Load(&api, owner) == SCO_OK, "reload new owner"); Good();
    test_module::Unload(); test_module::Close();
    std::printf("contracts metadata: %u checks PASS; copied references, native installation UNKNOWN; no mission/economy calls\n", checks);
}
