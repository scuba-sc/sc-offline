// SPDX-License-Identifier: GPL-3.0-only
// creative: sc-offline's cheats (god mode, noclip and its speed, infinite ammo, infinite ship ammo) as
// an optional plugin on sco-core's game.creative service. A plain SDK consumer: it includes only the
// SDK zip's headers (the sdk-headers gate covers this folder with no allow-list entry) and does
// nothing the SDK does not offer any other plugin.
//
// The commands are the old cheats' controls: creative.god, creative.noclip, creative.noclip_speed,
// creative.ammo, creative.ship_ammo and creative.status. The same controls also appear as the menu's
// Creative tab: a sco.widgets 1.0 panel registered after the commands, skipped with a warning when no
// renderer is present, so the commands never depend on it. A toggle this plugin switches on goes off
// when the plugin unloads (the game pack's release hook), so unloading it is the off switch for
// everything it did. The folder ships with a `disabled` file: turn the plugin on from the launcher's
// Plugins page.
#include "creative.h"
#include "scosdk/scosdk.hpp"
#include "sc_creative.h"
#include "sco_settings.h"
#include "sco_widgets.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
void AppendBounded(char* destination, std::size_t capacity, const char* text) noexcept {
    if (!destination || capacity == 0) return;
    std::size_t used = 0;
    while (used < capacity && destination[used]) ++used;
    if (used == capacity) {
        destination[capacity - 1] = '\0';
        return;
    }
    if (!text) return;
    const std::size_t available = capacity - used - 1;
    std::size_t length = 0;
    while (length < available && text[length]) ++length;
    if (length) std::memcpy(destination + used, text, length);
    destination[used + length] = '\0';
}

// The pinned SDK wrapper assumes full, non-null tables. Keep the wire contract
// here: reopen each operation and check the complete member before reading it.
class CreativeService {
public:
    void Open(const sco::sdk::Plugin& plugin) { plugin_ = &plugin; }
    const sc_creative_v1* Query() const noexcept {
        const void* table = nullptr;
        if (!plugin_ || plugin_->Api()->query_service(SC_CREATIVE_SERVICE_NAME,
                SC_CREATIVE_SERVICE_VERSION, &table) != SCO_OK) return nullptr;
        return static_cast<const sc_creative_v1*>(table);
    }
#define CREATIVE_MEMBER(member, cap) \
    const auto* t = Query(); \
    if (!t || !sco::sdk::Covers(t->size, offsetof(sc_creative_v1, member), sizeof(t->member)) \
        || !t->member || ((cap) && !plugin_->Has(cap))) return SCO_UNAVAILABLE
    sco_result SetGodMode(bool on) const noexcept {
        CREATIVE_MEMBER(set_god_mode, "game.creative.god_mode"); return t->set_god_mode(plugin_->Self(), on);
    }
    sco_result SetFlyMode(bool on) const noexcept {
        // Resolve/validate the fly method before settings or speed can mutate anything.
        { CREATIVE_MEMBER(set_fly_mode, "game.creative.fly"); }
        if (on) {
            const void* table = nullptr;
            auto r = plugin_->Api()->query_service(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0, &table);
            if (r != SCO_NOT_FOUND) {
                if (r != SCO_OK) return r;
                const auto* st = static_cast<const sco_settings_v1*>(table);
                if (!st || !sco::sdk::Covers(st->size, offsetof(sco_settings_v1, get_float), sizeof(st->get_float))
                    || !st->get_float) return SCO_UNAVAILABLE;
                double speed = 0;
                r = st->get_float(plugin_->Self(), "noclip_speed", &speed);
                if (r != SCO_OK) return r;
                if (!std::isfinite(speed) || speed < 1 || speed > SC_CREATIVE_FLY_SPEED_MAX) return SCO_BAD_ARG;
                r = SetFlySpeed(static_cast<float>(speed));
                if (r != SCO_OK) return r;
            }
        }
        CREATIVE_MEMBER(set_fly_mode, "game.creative.fly"); return t->set_fly_mode(plugin_->Self(), on);
    }
    sco_result SetFlySpeed(float speed) const noexcept {
        CREATIVE_MEMBER(set_fly_speed, "game.creative.fly"); return t->set_fly_speed(plugin_->Self(), speed);
    }
    sco_result SetInfiniteAmmo(bool on) const noexcept {
        CREATIVE_MEMBER(set_infinite_ammo, "game.creative.ammo"); return t->set_infinite_ammo(plugin_->Self(), on);
    }
    sco_result SetInfiniteShipAmmo(bool on) const noexcept {
        CREATIVE_MEMBER(set_infinite_ship_ammo, "game.creative.ship_ammo"); return t->set_infinite_ship_ammo(plugin_->Self(), on);
    }
    sco_result State(uint32_t& flags) const noexcept {
        CREATIVE_MEMBER(state, static_cast<const char*>(nullptr)); return t->state(plugin_->Self(), &flags);
    }
    // The panel's starting slider value: the noclip_speed setting when it is readable and valid.
    bool ConfiguredSpeed(double& out) const noexcept {
        const void* table = nullptr;
        if (!plugin_ || plugin_->Api()->query_service(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0, &table) != SCO_OK
            || !table) return false;
        const auto* st = static_cast<const sco_settings_v1*>(table);
        if (!sco::sdk::Covers(st->size, offsetof(sco_settings_v1, get_float), sizeof(st->get_float)) || !st->get_float)
            return false;
        double speed = 0;
        if (st->get_float(plugin_->Self(), "noclip_speed", &speed) != SCO_OK || !std::isfinite(speed)
            || speed < 1 || speed > SC_CREATIVE_FLY_SPEED_MAX) return false;
        out = speed;
        return true;
    }
    sco_result LastError(char (&out)[256]) const noexcept {
        out[0] = '\0';
        CREATIVE_MEMBER(last_error, static_cast<const char*>(nullptr));
        uint32_t size = sizeof(out);
        auto result = t->last_error(plugin_->Self(), out, &size);
        out[sizeof(out) - 1] = '\0';
        if (result != SCO_OK) out[0] = '\0';
        return result;
    }
#undef CREATIVE_MEMBER
private:
    const sco::sdk::Plugin* plugin_ = nullptr;
};

class Creative : public sco::sdk::Plugin {
public:
    sco_result OnLoad() override {
        using namespace sco::sdk;
        cr_.Open(*this); // Missing services leave commands available with explicit refusal.
        sco_result r = Toggle("creative.god", "God mode", "No damage to you", "game.creative.god_mode",
                              &CreativeService::SetGodMode);
        if (r == SCO_OK)
            r = Toggle("creative.noclip", "Noclip", "Fly through everything (speed: creative.noclip_speed)", "game.creative.fly",
                       &CreativeService::SetFlyMode);
        if (r == SCO_OK)
            r = Toggle("creative.ammo", "Infinite ammo", "The weapons you carry never run out", "game.creative.ammo",
                       &CreativeService::SetInfiniteAmmo);
        if (r == SCO_OK)
            r = Toggle("creative.ship_ammo", "Infinite ship ammo", "Every weapon on the ship you're aboard never runs out",
                       "game.creative.ship_ammo", &CreativeService::SetInfiniteShipAmmo);
        if (r == SCO_OK)
            r = CommandBuilder(*this, "creative.noclip_speed")
                    .Title("Noclip speed")
                    .Help("The fly speed in metres per second (1 to 10000); the game's own speed returns when this plugin unloads")
                    .Capability("game.creative.fly")
                    .Arg<double>("speed", "Metres per second")
                    .Handle([this](const Args& args, Reply& reply) {
                        const double speed = args.Float(0);
                        if (args.Count() != 1 || args.Type(0) != SCO_ARG_FLOAT || !std::isfinite(speed)
                            || speed < 1 || speed > SC_CREATIVE_FLY_SPEED_MAX) {
                            reply.Set("Noclip speed must be finite, 1 to 10000"); return SCO_BAD_ARG;
                        }
                        const sco_result s = cr_.SetFlySpeed(static_cast<float>(speed));
                        if (s != SCO_OK) return Failed(reply, "Noclip speed", s);
                        reply.Printf("Noclip speed %.0f", speed);
                        return SCO_OK;
                    })
                    .Register();
        if (r == SCO_OK)
            r = CommandBuilder(*this, "creative.status")
                    .Title("Creative status")
                    .Help("Which of the toggles are on")
                    .Handle([this](const Args& args, Reply& reply) {
                        if (args.Count()) { reply.Set("Creative status takes no arguments"); return SCO_BAD_ARG; }
                        uint32_t flags = 0;
                        const sco_result s = cr_.State(flags);
                        if (s != SCO_OK) return Failed(reply, "Creative status", s);
                        reply.Printf("god %s, noclip %s, ammo %s, ship ammo %s", On(flags, SC_CREATIVE_GOD_MODE),
                                     On(flags, SC_CREATIVE_FLY), On(flags, SC_CREATIVE_INFINITE_AMMO),
                                     On(flags, SC_CREATIVE_INFINITE_SHIP_AMMO));
                        return SCO_OK;
                    })
                    .Register();
        if (r == SCO_OK) StartPanel();
        return r;
    }

    void OnUnload() override {
        if (!panel_) return;
        if (const auto* w = Widgets()) (void)w->remove_panel(Self(), kPanelId);
        panel_ = false;
    }

private:
    using SetFn = sco_result (CreativeService::*)(bool) const noexcept;

    static const char* On(uint32_t flags, uint32_t bit) { return (flags & bit) ? "on" : "off"; }

    // The reply says why: the game pack's own reason, when it left one.
    sco_result Failed(sco::sdk::Reply& reply, const char* what, sco_result r) const {
        char why[256] = {};
        cr_.LastError(why);
        reply.Printf("%s failed: %s", what, why[0] ? why : "see mod.log");
        return r;
    }

    sco_result Toggle(const char* name, const char* title, const char* help, const char* capability, SetFn set) {
        using namespace sco::sdk;
        const std::string label = title;
        return CommandBuilder(*this, name)
            .Title(title)
            .Help(help)
            .Capability(capability)
            .Arg<bool>("on", "true = on, false = off")
            .Handle([this, set, label](const Args& args, Reply& reply) {
                if (args.Count() != 1 || args.Type(0) != SCO_ARG_BOOL
                    || (args.Raw()[0].v.i != 0 && args.Raw()[0].v.i != 1)) {
                    reply.Set("Expected one boolean (true or false)"); return SCO_BAD_ARG;
                }
                const bool on = args.Bool(0);
                const sco_result s = (cr_.*set)(on);
                if (s != SCO_OK) return Failed(reply, (label + (on ? " on" : " off")).c_str(), s);
                reply.Printf("%s %s", label.c_str(), on ? "on" : "off");
                return SCO_OK;
            })
            .Register();
    }

    // ---- the Creative tab: a sco.widgets 1.0 panel over the same CreativeService setters ----
    static constexpr char kPanelId[] = "creative.panel";
    enum : uint32_t { kGod = 1, kNoclip, kAmmo, kShipAmmo, kSpeed, kStatus };

    const sco_widgets_v1* Widgets() const noexcept {
        const void* table = nullptr;
        if (Api()->query_service(SCO_WIDGETS_NAME, SCO_WIDGETS_VERSION_1_0, &table) != SCO_OK || !table) return nullptr;
        const auto* w = static_cast<const sco_widgets_v1*>(table);
        if (!sco::sdk::Covers(w->size, offsetof(sco_widgets_v1, remove_panel), sizeof(w->remove_panel))
            || !w->set_panel || !w->remove_panel) return nullptr;
        return w;
    }

    // A missing renderer or a refused panel never fails the load: the commands stay available.
    void StartPanel() noexcept {
        if (!Widgets()) { Warn("Creative tab not shown (no sco.widgets); creative commands remain available"); return; }
        double configured = 0;
        speed_ = cr_.ConfiguredSpeed(configured) ? configured : 10;
        status_[0] = '\0';
        if (!RenderPanel()) Warn("Creative panel refused; creative commands remain available");
    }

    static sco_widget_node MakeNode(uint32_t kind, uint32_t id, const char* label) noexcept {
        sco_widget_node n{};
        n.size = sizeof(n); n.kind = kind; n.id = id; n.label = label;
        return n;
    }

    // Toggles always show the provider's state, so a refused toggle snaps back; 0 when state fails.
    bool RenderPanel() noexcept {
        const auto* w = Widgets();
        if (!w) return false;
        uint32_t flags = 0;
        if (cr_.State(flags) != SCO_OK) flags = 0;
        sco_widget_node nodes[] = {
            MakeNode(SCO_WIDGET_TOGGLE, kGod, "God mode"),
            MakeNode(SCO_WIDGET_TOGGLE, kNoclip, "Noclip"),
            MakeNode(SCO_WIDGET_TOGGLE, kAmmo, "Infinite ammo"),
            MakeNode(SCO_WIDGET_TOGGLE, kShipAmmo, "Infinite ship ammo"),
            MakeNode(SCO_WIDGET_SLIDER, kSpeed, "Noclip speed (metres per second)"),
            MakeNode(SCO_WIDGET_LABEL, kStatus, status_)};
        nodes[0].value = (flags & SC_CREATIVE_GOD_MODE) ? 1 : 0;
        nodes[1].value = (flags & SC_CREATIVE_FLY) ? 1 : 0;
        nodes[2].value = (flags & SC_CREATIVE_INFINITE_AMMO) ? 1 : 0;
        nodes[3].value = (flags & SC_CREATIVE_INFINITE_SHIP_AMMO) ? 1 : 0;
        nodes[4].value = speed_; nodes[4].minimum = 1; nodes[4].maximum = SC_CREATIVE_FLY_SPEED_MAX;
        const sco_result r = w->set_panel(Self(), kPanelId, "Creative", nodes,
                                          static_cast<uint32_t>(sizeof(nodes) / sizeof(nodes[0])),
                                          static_cast<uint32_t>(sizeof(nodes[0])), PanelEvent, this);
        if (r == SCO_OK) panel_ = true;
        return r == SCO_OK;
    }

    static void PanelEvent(const char* panel, const sco_widget_event* e, void* ctx) {
        if (!ctx || !panel || std::strcmp(panel, kPanelId) || !e || e->size < sizeof(*e)) return;
        static_cast<Creative*>(ctx)->OnPanelEvent(*e);
    }

    // Same reason text as a refused command: the game pack's own reason, when it left one.
    void Refused(const char* what) noexcept {
        char why[256] = {};
        cr_.LastError(why);
        status_[0] = '\0';
        AppendBounded(status_, sizeof(status_), what);
        AppendBounded(status_, sizeof(status_), " failed: ");
        AppendBounded(status_, sizeof(status_), why[0] ? why : "see mod.log");
    }

    void OnPanelEvent(const sco_widget_event& e) noexcept {
        if (e.kind == SCO_WIDGET_TOGGLE && (e.value == 0 || e.value == 1)) {
            const char* label = nullptr;
            SetFn set = nullptr;
            switch (e.node_id) {
                case kGod: label = "God mode"; set = &CreativeService::SetGodMode; break;
                case kNoclip: label = "Noclip"; set = &CreativeService::SetFlyMode; break;
                case kAmmo: label = "Infinite ammo"; set = &CreativeService::SetInfiniteAmmo; break;
                case kShipAmmo: label = "Infinite ship ammo"; set = &CreativeService::SetInfiniteShipAmmo; break;
                default: return;
            }
            const bool on = e.value == 1;
            char what[64];
            std::snprintf(what, sizeof(what), "%s %s", label, on ? "on" : "off");
            if ((cr_.*set)(on) == SCO_OK) {
                std::snprintf(status_, sizeof(status_), "%s", what);
                double configured = 0; // noclip on applied the configured speed
                if (on && e.node_id == kNoclip && cr_.ConfiguredSpeed(configured)) speed_ = configured;
            } else {
                Refused(what);
            }
        } else if (e.kind == SCO_WIDGET_SLIDER && e.node_id == kSpeed && std::isfinite(e.value) && e.value >= 1
                   && e.value <= SC_CREATIVE_FLY_SPEED_MAX) {
            if (cr_.SetFlySpeed(static_cast<float>(e.value)) == SCO_OK) {
                speed_ = e.value;
                std::snprintf(status_, sizeof(status_), "Noclip speed %.0f", e.value);
            } else {
                Refused("Noclip speed");
            }
        } else {
            return;
        }
        (void)RenderPanel();
    }

    CreativeService cr_;
    bool panel_ = false;
    double speed_ = 10;
    char status_[256] = {};
};

}  // namespace

namespace creative_plugin {
namespace { sco::sdk::detail::Slot<Creative> slot; }
sco_result Load(const sco_api* api, sco_plugin* self) noexcept { return slot.Load(api, self); }
void Unload() noexcept { slot.Unload(); }
}
