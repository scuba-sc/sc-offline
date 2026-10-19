// SPDX-License-Identifier: GPL-3.0-only
#include "contracts.h"
#include <sc_missions.h>
#include <sc_contracts.h>
#include <sc_economy.h>
#include <sco_content.h>
#include <sco_widgets.h>
#include <cstring>
#include <string_view>
#include <charconv>
#include <cstddef>
#include <cstdio>
#define MEMBER(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
namespace sco::features::contracts {
namespace {
const sco_api* api = nullptr;
sco_plugin* self = nullptr;
bool cancelled = false;
char panelReply[512] = "Ready; provider admission is required for every action";
char missionInputs[3][24] = {"0", "1", "0"};
const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "contracts", "1.0.0", "sc-offline"};
enum Action { Summary, Offer, Mission, Accept, Objective, Advance, Abandon, Ingest, Cancel, Resume, Balance, Apply, Receipt, Radius };
struct Spec { const char* name; Action action; uint32_t nargs; };
const Spec specs[] = {{"contracts.status", Summary, 0}, {"contracts.offer", Offer, 1}, {"contracts.mission", Mission, 1}, {"contracts.accept", Accept, 2}, {"contracts.objective", Objective, 3}, {"contracts.advance", Advance, 2}, {"contracts.abandon", Abandon, 1}, {"contracts.ingest", Ingest, 1}, {"contracts.cancel", Cancel, 0}, {"contracts.resume", Resume, 0}, {"contracts.balance", Balance, 0}, {"contracts.apply", Apply, 3}, {"contracts.receipt", Receipt, 1}};
const sco_arg_def defs[] = {{"id_or_index", SCO_ARG_INT, 0, "Nonnegative offer index or positive mission ID"}, {"key_or_phase", SCO_ARG_INT, 0, "Receipt key or expected phase"}, {"objective", SCO_ARG_INT, 0, "Expected objective"}};
template<class T> const T* Service(const char* name, uint32_t version) {
    const void* table = nullptr;
    if (!MEMBER(api, sco_api, query_service) || !self || api->query_service(name, version, &table) != SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
bool MissionPath(const char* name, size_t capacity) {
    const void* end = std::memchr(name, 0, capacity);
    if (!end) return false;
    std::string_view path(name, static_cast<const char*>(end) - name);
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
    if (count != spec.nargs || (count && !args)) return SCO_BAD_ARG;
    if (spec.action == Radius) {
        const auto* c = Service<sc_contracts_v1>(SC_CONTRACTS_SERVICE_NAME, SC_CONTRACTS_SERVICE_VERSION);
        if (!MEMBER(c, sc_contracts_v1, stream_radius)) return SCO_UNAVAILABLE;
        float radius = 0;
        const sco_result r = c->stream_radius(self, &radius);
        if (r == SCO_OK) std::snprintf(reply, capacity, "Mission stream radius %.3f (game units)", static_cast<double>(radius));
        else std::snprintf(reply, capacity, "Mission stream radius unavailable (%u)", static_cast<unsigned>(r));
        return r;
    }
    for (uint32_t i = 0; i < count; ++i) if (args[i].type != SCO_ARG_INT || (args[i].v.i < 0 && !(spec.action == Apply && i == 2))) return SCO_BAD_ARG;
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
        if (item.kind != SCO_CONTENT_SCRIPT || !item.id || !MissionPath(item.name, sizeof(item.name))) return SCO_BAD_ARG;
        uint32_t needed = 0;
        result = c->read(self, item.id, nullptr, &needed);
        if (result != SCO_TOO_MANY && result != SCO_OK) return result;
        if (!needed || needed > SC_MISSIONS_SCRIPT_BYTES) return SCO_BAD_ARG;
        char bytes[SC_MISSIONS_SCRIPT_BYTES];
        uint32_t written = needed;
        result = c->read(self, item.id, bytes, &written);
        if (result != SCO_OK) return result;
        if (written != needed || !Declaration(std::string_view(bytes, written))) return SCO_BAD_ARG;
        sc_mission_script script{}; script.size = sizeof(script);
        std::snprintf(script.relative_path, sizeof(script.relative_path), "%s", item.name);
        script.bytes = bytes; script.byte_count = written;
        result = m->ingest(self, &script);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Declarative mission registered; session lifetime; no executable scripts");
    }
    if (result != SCO_OK && result != SCO_UNAVAILABLE) std::snprintf(reply, capacity, "Contracts operation refused (%u); effects not retried", static_cast<unsigned>(result));
    return result;
}
}
namespace {
void PublishPanel();
void PanelEvent(const char*, const sco_widget_event* event, void*) {
    if (!event || event->size < sizeof(*event)) return;
    if (event->node_id >= 10 && event->node_id <= 12 && event->kind == SCO_WIDGET_TEXT_INPUT) {
        if (!event->text) return;
        const auto length = std::strlen(event->text);
        if (length >= sizeof(missionInputs[0])) return;
        std::memcpy(missionInputs[event->node_id - 10], event->text, length + 1);
        PublishPanel(); return;
    }
    if (event->kind != SCO_WIDGET_BUTTON) return;
    Spec action{};
    sco_arg args[3]{};
    switch (event->node_id) {
    case 1: action = {"contracts.status", Summary, 0}; break;
    case 2: action = {"contracts.balance", Balance, 0}; break;
    case 3: action = {"contracts.radius", Radius, 0}; break;
    case 4: action = {"contracts.cancel", Cancel, 0}; break;
    case 5: action = {"contracts.resume", Resume, 0}; break;
    case 20: action = {"contracts.accept", Accept, 2}; break;
    case 21: action = {"contracts.mission", Mission, 1}; break;
    case 22: action = {"contracts.objective", Objective, 3}; break;
    case 23: action = {"contracts.advance", Advance, 2}; break;
    case 24: action = {"contracts.abandon", Abandon, 1}; break;
    default: return;
    }
    for (uint32_t i = 0; i < action.nargs; ++i) {
        args[i].type = SCO_ARG_INT;
        const char* end = missionInputs[i] + std::strlen(missionInputs[i]);
        const auto parsed = std::from_chars(missionInputs[i], end, args[i].v.i);
        if (parsed.ec != std::errc{} || parsed.ptr != end || args[i].v.i < 0) {
            std::snprintf(panelReply, sizeof(panelReply), "Enter bounded nonnegative integer mission arguments; no action submitted");
            PublishPanel(); return;
        }
    }
    (void)Execute(action.nargs ? args : nullptr, action.nargs, &action, panelReply, sizeof(panelReply));
    PublishPanel();
}
void PublishPanel() {
    const auto* widgets = Service<sco_widgets_v1>(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0);
    if (!MEMBER(widgets, sco_widgets_v1, set_panel)) return;
    const char* labels[] = {"Read offers", "Read local balance", "Read stream radius", "Cancel consumer mutations", "Resume consumer mutations"};
    sco_widget_node nodes[16]{};
    for (uint32_t i = 0; i < 5; ++i) {
        nodes[i].size = sizeof(nodes[i]); nodes[i].kind = SCO_WIDGET_BUTTON;
        nodes[i].id = i + 1; nodes[i].label = labels[i];
    }
    nodes[5].size = sizeof(nodes[5]); nodes[5].kind = SCO_WIDGET_LABEL;
    nodes[5].id = 6; nodes[5].label = panelReply;
    nodes[6].size = sizeof(nodes[6]); nodes[6].kind = SCO_WIDGET_LABEL;
    nodes[6].id = 7; nodes[6].label = cancelled ? "Mutations cancelled" : "Provider admission required; no automatic payout";
    const char* inputs[] = {"Offer index / mission ID", "Receipt key / expected phase", "Objective index"};
    for (uint32_t i = 0; i < 3; ++i) {
        auto& node = nodes[7 + i]; node.size = sizeof(node); node.kind = SCO_WIDGET_TEXT_INPUT;
        node.id = 10 + i; node.label = inputs[i]; node.text = missionInputs[i];
    }
    const char* actions[] = {"Accept offer (explicit)", "Read mission", "Complete objective (explicit)", "Advance phase (explicit)", "Abandon mission (explicit)"};
    for (uint32_t i = 0; i < 5; ++i) {
        auto& node = nodes[10 + i]; node.size = sizeof(node); node.kind = SCO_WIDGET_BUTTON;
        node.id = 20 + i; node.label = actions[i];
    }
    nodes[15].size = sizeof(nodes[15]); nodes[15].kind = SCO_WIDGET_LABEL; nodes[15].id = 25;
    nodes[15].label = "Mission IDs are session-only. Completion never pays automatically; production adapter required.";
    const auto result = widgets->set_panel(self, "contracts.main", "Contracts", nodes, 16, sizeof(nodes[0]), PanelEvent, nullptr);
    if (result != SCO_OK && MEMBER(api, sco_api, log)) api->log(self, SCO_LOG_WARN, "Contracts panel refused; commands remain available");
}
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* host, sco_plugin* owner) {
    if (api) return SCO_BAD_ARG;
    if (!owner || !MEMBER(host, sco_api, query_service) || !MEMBER(host, sco_api, register_command) || host->major != SCO_API_MAJOR || host->minor < SCO_API_MINOR) return SCO_UNAVAILABLE;
    api = host; self = owner; cancelled = false;
    std::snprintf(missionInputs[0], sizeof(missionInputs[0]), "0");
    std::snprintf(missionInputs[1], sizeof(missionInputs[1]), "1");
    std::snprintf(missionInputs[2], sizeof(missionInputs[2]), "0");
    std::snprintf(panelReply, sizeof(panelReply), "Ready; provider admission is required for every action");
    static const Spec radiusSpec{"contracts.radius", Radius, 0};
    const sco_command radiusCommand{sizeof(sco_command), 0, radiusSpec.name, radiusSpec.name, "Read the public mission stream radius", nullptr, nullptr, 0, sizeof(sco_arg_def), Execute, const_cast<Spec*>(&radiusSpec)};
    const sco_result radiusResult = api->register_command(self, &radiusCommand);
    if (radiusResult != SCO_OK) { api = nullptr; self = nullptr; return radiusResult; }
    for (const auto& spec : specs) {
        const sco_command command{sizeof(sco_command), 0, spec.name, spec.name, "Guarded public offline contracts boundary", nullptr, spec.nargs ? defs : nullptr, spec.nargs, sizeof(sco_arg_def), Execute, const_cast<Spec*>(&spec)};
        sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) { api = nullptr; self = nullptr; return r; }
    }
    PublishPanel();
    return SCO_OK;
}
void Unload() {
    const auto* widgets = Service<sco_widgets_v1>(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0);
    if (MEMBER(widgets, sco_widgets_v1, remove_panel)) (void)widgets->remove_panel(self, "contracts.main");
    cancelled = true; api = nullptr; self = nullptr;
} // no late provider mutation; host releases commands
}
