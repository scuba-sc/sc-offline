// SPDX-License-Identifier: GPL-3.0-only
#include "contracts.h"
#include "reference_metadata.h"
#include <sc_missions.h>
#include <sc_economy.h>
#include <sco_content.h>
#include <cstring>
#include <string_view>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <string>
#include <new>
#define MEMBER(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
namespace sco::features::contracts {
namespace {
const sco_api* api = nullptr;
sco_plugin* self = nullptr;
bool cancelled = false;
const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "contracts", "1.0.0", "sc-offline"};
enum Action { Summary, Offer, Mission, Accept, Objective, Advance, Abandon, Ingest, Cancel, Resume, Balance, Apply, Receipt, References, Reference };
struct Spec { const char* name; Action action; uint32_t nargs; };
const Spec specs[] = {{"contracts.status", Summary, 0}, {"contracts.offer", Offer, 1}, {"contracts.mission", Mission, 1}, {"contracts.accept", Accept, 2}, {"contracts.objective", Objective, 3}, {"contracts.advance", Advance, 2}, {"contracts.abandon", Abandon, 1}, {"contracts.ingest", Ingest, 1}, {"contracts.cancel", Cancel, 0}, {"contracts.resume", Resume, 0}, {"contracts.balance", Balance, 0}, {"contracts.apply", Apply, 3}, {"contracts.receipt", Receipt, 1}, {"contracts.references", References, 2}, {"contracts.reference", Reference, 3}};
const sco_arg_def defs[] = {{"id_or_index", SCO_ARG_INT, 0, "Nonnegative offer index or positive mission ID"}, {"key_or_phase", SCO_ARG_INT, 0, "Receipt key or expected phase"}, {"objective", SCO_ARG_INT, 0, "Expected objective"}};
const sco_arg_def referenceDefs[] = {{"list_ordinal", SCO_ARG_INT, 0, "Startup-scoped public List ordinal for lists/contract_scripts.txt"}, {"row_index", SCO_ARG_INT, 0, "Zero-based metadata row; comments/blank lines excluded"}, {"reference_index", SCO_ARG_INT, 0, "Zero-based non-dash reference; no native installation claim"}};
template<class T> const T* Service(const char* name, uint32_t version) {
    const void* table = nullptr;
    if (!MEMBER(api, sco_api, query_service) || !self || api->query_service(name, version, &table) != SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
sco_result MetadataReply(sco_result result, const char* text, char* reply, uint32_t capacity) {
    if (std::strlen(text) >= capacity) {
        std::snprintf(reply, capacity, "Metadata reply buffer too small");
        return SCO_TOO_MANY;
    }
    std::memcpy(reply, text, std::strlen(text) + 1);
    return result;
}
sco_result ReadReferences(const sco_arg* args, bool one, char* reply, uint32_t capacity) {
    auto fail = [&](sco_result r, const char* why) { return MetadataReply(r, why, reply, capacity); };
    if (args[0].v.i >= SCO_CONTENT_MAX_ITEMS || args[1].v.i >= metadata::kMaxRows ||
        (one && args[2].v.i >= metadata::kMaxReferences))
        return fail(SCO_BAD_ARG, "Metadata index exceeds its bound; native installation UNKNOWN");
    const auto* content = Service<sco_content_v1>(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0);
    if (!MEMBER(content, sco_content_v1, item) || !MEMBER(content, sco_content_v1, read))
        return fail(SCO_UNAVAILABLE, "Copied reference metadata unavailable; native installation UNKNOWN; no private fallback");
    sco_content_item item{}; item.size = sizeof(item);
    auto r = content->item(self, SCO_CONTENT_LIST, static_cast<uint32_t>(args[0].v.i), &item);
    if (r != SCO_OK) return fail(r, "Metadata List ordinal unavailable; native installation UNKNOWN");
    if (item.size < sizeof(item) || item.kind != SCO_CONTENT_LIST || !item.id ||
        !std::memchr(item.name, 0, sizeof(item.name)) || !std::memchr(item.pack, 0, sizeof(item.pack)) ||
        !item.pack[0] || std::strcmp(item.name, "lists/contract_scripts.txt"))
        return fail(SCO_BAD_ARG, "Select an exact lists/contract_scripts.txt List record; native installation UNKNOWN");
    uint32_t needed = 0;
    r = content->read(self, item.id, nullptr, &needed);
    if (!((r == SCO_TOO_MANY && needed > 0) || (r == SCO_OK && needed == 0)))
        return fail(r == SCO_OK || r == SCO_TOO_MANY ? SCO_FAILED : r, "Metadata size unavailable; native installation UNKNOWN");
    if (needed > SCO_CONTENT_MAX_FILE_BYTES)
        return fail(SCO_TOO_MANY, "Metadata file exceeds 1 MiB; native installation UNKNOWN");
    try {
        std::string bytes(needed, '\0');
        uint32_t written = needed;
        if (needed) {
            r = content->read(self, item.id, bytes.data(), &written);
            if (r != SCO_OK) return fail(r, "Metadata read refused; native installation UNKNOWN");
        }
        if (written != needed) return fail(SCO_FAILED, "Metadata byte count changed; native installation UNKNOWN");
        metadata::Row row{};
        const auto parsed = metadata::Parse(bytes, static_cast<uint32_t>(args[1].v.i), row);
        if (parsed == metadata::Result::Malformed) return fail(SCO_BAD_ARG, "Malformed metadata file; no partial row result; native installation UNKNOWN");
        if (parsed == metadata::Result::Limit) return fail(SCO_TOO_MANY, "Metadata parser limit exceeded; no partial row result; native installation UNKNOWN");
        if (parsed == metadata::Result::NotFound) return fail(SCO_NOT_FOUND, "Metadata row not found; native installation UNKNOWN");
        char text[1024]{};
        if (one) {
            const auto index = static_cast<uint32_t>(args[2].v.i);
            if (index >= row.count) return fail(SCO_NOT_FOUND, "Metadata reference not found; dash means no reference; native installation UNKNOWN");
            const auto ref = row.references[index];
            std::snprintf(text, sizeof(text), "Pack %s row %lld source GUID %.*s reference %u: %.*s; metadata only; native installation UNKNOWN/UNAVAILABLE (no public XML-installation query)", item.pack, static_cast<long long>(args[1].v.i), 32, row.guid.data(), index, static_cast<int>(ref.size()), ref.data());
        } else {
            std::snprintf(text, sizeof(text), "Pack %s row %lld source GUID %.*s name %.*s references %u; metadata only, not a mission offer ID; native installation UNKNOWN/UNAVAILABLE (no public XML-installation query)", item.pack, static_cast<long long>(args[1].v.i), 32, row.guid.data(), static_cast<int>(row.name.size()), row.name.data(), row.count);
        }
        return fail(SCO_OK, text);
    } catch (const std::bad_alloc&) {
        return fail(SCO_TOO_MANY, "Metadata copy allocation refused; native installation UNKNOWN");
    }
}
bool MissionPath(const char* name, size_t capacity, size_t path_capacity, std::string_view& path) {
    const void* end = std::memchr(name, 0, capacity);
    if (!end) return false;
    path = std::string_view(name, static_cast<const char*>(end) - name);
    if (path.size() >= path_capacity) return false;
    if (!path.starts_with("missions/") || !path.ends_with(".scom")) return false;
    auto id = path.substr(9, path.size() - 9 - 5);
    if (id.empty() || id.size() > 100) return false;
    for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
bool Declaration(std::string_view bytes) {
    if (!bytes.starts_with("sco-mission 1\nphases ")) return false;
    bytes.remove_prefix(std::string_view("sco-mission 1\nphases ").size());
    auto number = [&bytes]() {
        auto newline = bytes.find('\n');
        if (newline == std::string_view::npos || newline == 0 || bytes.front() == '0') return false;
        uint32_t value = 0;
        auto parsed = std::from_chars(bytes.data(), bytes.data() + newline, value);
        if (parsed.ec != std::errc{} || parsed.ptr != bytes.data() + newline || !value || value > SC_MISSIONS_MAX_STEPS) return false;
        bytes.remove_prefix(newline + 1); return true;
    };
    if (!number() || !bytes.starts_with("objectives ")) return false;
    bytes.remove_prefix(std::string_view("objectives ").size());
    return number() && bytes.empty();
}
sco_result Execute(const sco_arg* args, uint32_t count, void* context, char* reply, uint32_t capacity) {
    if (!reply || !capacity || !context) return SCO_BAD_ARG;
    std::snprintf(reply, capacity, "Contracts provider unavailable; no private fallback");
    if (!api || !self) return SCO_UNAVAILABLE;
    const auto& spec = *static_cast<const Spec*>(context);
    if (spec.action == References || spec.action == Reference)
        std::snprintf(reply, capacity, "Invalid metadata indices; native installation UNKNOWN");
    if (count != spec.nargs || (count && !args)) return SCO_BAD_ARG;
    for (uint32_t i = 0; i < count; ++i) if (args[i].type != SCO_ARG_INT || (args[i].v.i < 0 && !(spec.action == Apply && i == 2))) return SCO_BAD_ARG;
    if (spec.action == References || spec.action == Reference)
        return ReadReferences(args, spec.action == Reference, reply, capacity);
    if (spec.action == Cancel || spec.action == Resume) {
        cancelled = spec.action == Cancel;
        std::snprintf(reply, capacity, cancelled ? "Consumer mutations cancelled; receipts preserved" : "Consumer resumed; provider admission still required");
        return SCO_OK;
    }
    if (cancelled && (spec.action == Accept || spec.action == Objective || spec.action == Advance || spec.action == Ingest || spec.action == Apply)) {
        std::snprintf(reply, capacity, "Consumer cancelled; no provider mutation; receipts preserved"); return SCO_UNAVAILABLE;
    }
    if (spec.action == Balance || spec.action == Apply || spec.action == Receipt) {
        const auto* e = Service<sc_economy_v1>(SC_ECONOMY_SERVICE_NAME, SC_ECONOMY_SERVICE_VERSION);
        if (spec.action == Balance) {
            if (!MEMBER(e, sc_economy_v1, balance)) return SCO_UNAVAILABLE;
            sc_economy_balance out{}; out.size = sizeof(out);
            sco_result r = e->balance(self, &out);
            if (r == SCO_OK) std::snprintf(reply, capacity, "Local balance %lld; wallet import/save policy pending", static_cast<long long>(out.local_balance));
            else std::snprintf(reply, capacity, "Local balance unavailable (%u)", static_cast<unsigned>(r));
            return r;
        }
        if (!args[0].v.i) return SCO_BAD_ARG;
        sc_economy_receipt out{}; out.size = sizeof(out);
        sco_result r;
        if (spec.action == Apply) {
            int64_t expected = args[1].v.i, delta = args[2].v.i;
            if (expected >= SC_ECONOMY_BALANCE_LIMIT || !delta || delta <= -SC_ECONOMY_BALANCE_LIMIT || delta >= SC_ECONOMY_BALANCE_LIMIT) return SCO_BAD_ARG;
            if (expected + delta < 0 || expected + delta >= SC_ECONOMY_BALANCE_LIMIT) return SCO_BAD_ARG;
            if (!MEMBER(e, sc_economy_v1, apply)) return SCO_UNAVAILABLE;
            const sc_economy_request request{sizeof(sc_economy_request), 0, static_cast<uint64_t>(args[0].v.i), expected, delta};
            r = e->apply(self, &request, &out);
        } else {
            if (!MEMBER(e, sc_economy_v1, receipt)) return SCO_UNAVAILABLE;
            r = e->receipt(self, static_cast<uint64_t>(args[0].v.i), &out);
        }
        if (r == SCO_OK) std::snprintf(reply, capacity, "Payment key %llu session %llu before %lld after %lld sequence %llu flags %u; session-only receipt; persistence pending", static_cast<unsigned long long>(out.key), static_cast<unsigned long long>(out.session), static_cast<long long>(out.before), static_cast<long long>(out.after), static_cast<unsigned long long>(out.sequence), out.flags);
        else std::snprintf(reply, capacity, "Payment refused (%u); uncertain effects never retried", static_cast<unsigned>(r));
        return r;
    }
    const auto* m = Service<sc_missions_v1>(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION);
    sco_result result = SCO_UNAVAILABLE;
    if (spec.action == Summary || spec.action == Offer) {
        uint64_t index = count ? static_cast<uint64_t>(args[0].v.i) : 0;
        if (index >= SC_MISSIONS_MAX_OFFERS) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, offer)) return SCO_UNAVAILABLE;
        sc_mission_offer out{}; out.size = sizeof(out);
        result = m->offer(self, static_cast<uint32_t>(index), &out);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Offer %llu broker %llu generation %llu; wallet persistence pending", static_cast<unsigned long long>(out.offer_id), static_cast<unsigned long long>(out.broker_id), static_cast<unsigned long long>(out.generation));
        if (spec.action == Summary && result == SCO_NOT_FOUND) { std::snprintf(reply, capacity, "Contracts provider ready; no offers; wallet persistence pending"); return SCO_OK; }
    } else if (spec.action == Mission) {
        if (!args[0].v.i) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, status)) return SCO_UNAVAILABLE;
        sc_mission_status out{}; out.size = sizeof(out);
        result = m->status(self, static_cast<uint64_t>(args[0].v.i), &out);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Mission %llu state %u phase %u objective %u session %llu", static_cast<unsigned long long>(out.mission_id), out.state, out.phase, out.objective, static_cast<unsigned long long>(out.session));
    }
    if (spec.action == Accept) {
        if (args[0].v.i >= SC_MISSIONS_MAX_OFFERS || !args[1].v.i) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, offer) || !MEMBER(m, sc_missions_v1, accept)) return SCO_UNAVAILABLE;
        sc_mission_offer offer{}; offer.size = sizeof(offer);
        result = m->offer(self, static_cast<uint32_t>(args[0].v.i), &offer);
        if (result == SCO_OK) {
            uint64_t id = 0;
            result = m->accept(self, &offer, static_cast<uint64_t>(args[1].v.i), &id);
            if (result == SCO_OK) std::snprintf(reply, capacity, "Accepted mission %llu; owner/session receipt key %llu", static_cast<unsigned long long>(id), static_cast<unsigned long long>(args[1].v.i));
        }
    }
    if (spec.action == Objective || spec.action == Advance) {
        if (!args[0].v.i || args[1].v.i >= SC_MISSIONS_MAX_STEPS || (spec.action == Objective && args[2].v.i >= SC_MISSIONS_MAX_STEPS)) return SCO_BAD_ARG;
        uint64_t id = static_cast<uint64_t>(args[0].v.i);
        uint32_t phase = static_cast<uint32_t>(args[1].v.i);
        if (spec.action == Objective) {
            if (!MEMBER(m, sc_missions_v1, objective)) return SCO_UNAVAILABLE;
            result = m->objective(self, id, phase, static_cast<uint32_t>(args[2].v.i));
        } else {
            if (!MEMBER(m, sc_missions_v1, advance)) return SCO_UNAVAILABLE;
            result = m->advance(self, id, phase);
        }
        if (result == SCO_OK) std::snprintf(reply, capacity, "Mission step accepted; no automatic payout");
    }
    if (spec.action == Abandon) {
        if (!args[0].v.i) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, abandon)) return SCO_UNAVAILABLE;
        result = m->abandon(self, static_cast<uint64_t>(args[0].v.i));
        if (result == SCO_OK) std::snprintf(reply, capacity, "Abandonment accepted; effects not retried");
    }
    if (spec.action == Ingest) {
        if (args[0].v.i >= SCO_CONTENT_MAX_ITEMS) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, ingest)) return SCO_UNAVAILABLE;
        const auto* c = Service<sco_content_v1>(SCO_CONTENT_NAME, SCO_CONTENT_VERSION_1_0);
        if (!MEMBER(c, sco_content_v1, item) || !MEMBER(c, sco_content_v1, read)) return SCO_UNAVAILABLE;
        sco_content_item item{}; item.size = sizeof(item);
        result = c->item(self, SCO_CONTENT_SCRIPT, static_cast<uint32_t>(args[0].v.i), &item);
        if (result != SCO_OK) return result;
        sc_mission_script script{}; script.size = sizeof(script);
        std::string_view path;
        if (item.kind != SCO_CONTENT_SCRIPT || !item.id ||
            !MissionPath(item.name, sizeof(item.name), sizeof(script.relative_path), path)) return SCO_BAD_ARG;
        uint32_t needed = 0;
        result = c->read(self, item.id, nullptr, &needed);
        if (result != SCO_TOO_MANY && result != SCO_OK) return result;
        if (!needed || needed > SC_MISSIONS_SCRIPT_BYTES) return SCO_BAD_ARG;
        char bytes[SC_MISSIONS_SCRIPT_BYTES];
        uint32_t written = needed;
        result = c->read(self, item.id, bytes, &written);
        if (result != SCO_OK) return result;
        if (written != needed || !Declaration(std::string_view(bytes, written))) return SCO_BAD_ARG;
        std::memcpy(script.relative_path, path.data(), path.size());
        script.relative_path[path.size()] = '\0';
        script.bytes = bytes; script.byte_count = written;
        result = m->ingest(self, &script);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Declarative mission registered; session lifetime; no executable scripts");
    }
    if (result != SCO_OK && result != SCO_UNAVAILABLE) std::snprintf(reply, capacity, "Contracts operation refused (%u); effects not retried", static_cast<unsigned>(result));
    return result;
}
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* host, sco_plugin* owner) {
    if (api) return SCO_BAD_ARG;
    if (!owner || !MEMBER(host, sco_api, query_service) || !MEMBER(host, sco_api, register_command) || host->major != SCO_API_MAJOR || host->minor < SCO_API_MINOR) return SCO_UNAVAILABLE;
    api = host; self = owner; cancelled = false;
    for (const auto& spec : specs) {
        const auto* arguments = spec.action == References || spec.action == Reference ? referenceDefs : defs;
        const sco_command command{sizeof(sco_command), 0, spec.name, spec.name, "Guarded public offline contracts boundary", nullptr, spec.nargs ? arguments : nullptr, spec.nargs, sizeof(sco_arg_def), Execute, const_cast<Spec*>(&spec)};
        sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) { api = nullptr; self = nullptr; return r; }
    }
    return SCO_OK;
}
void Unload() { cancelled = true; api = nullptr; self = nullptr; } // no late provider mutation; host releases commands
}
