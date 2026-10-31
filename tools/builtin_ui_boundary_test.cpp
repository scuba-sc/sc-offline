// Synthetic ABI boundary tests for the built-in tab and hotkey helpers. No game is loaded.
#include "tabs.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

static const sco_ui_v1* published;
static sco_result query_result = SCO_OK, operation_result = SCO_OK, error_result = SCO_OK;
static int queries, tabs, hotkeys, errors, logs;
static std::string message;
static auto* self = reinterpret_cast<sco_plugin*>(static_cast<uintptr_t>(1));

static sco_result Query(const char* name, uint32_t version, const void** out) {
    ++queries;
    assert(std::strcmp(name, SCO_UI_NAME) == 0 && version == SCO_UI_VERSION_1_0);
    *out = published; // even a nonconforming failure must not be used
    return query_result;
}
static void Log(sco_plugin* who, sco_log_level level, const char* line) {
    assert(who == self && level == SCO_LOG_WARN);
    ++logs; message = line;
}
static void Draw(void*, void*) {}
static sco_result Tab(sco_plugin* who, const char* id, const char* title, int32_t order,
                      sco_ui_draw_fn draw, void* ctx) {
    assert(who == self && std::strcmp(id, "test.tab") == 0 && std::strcmp(title, "Test") == 0);
    assert(order == 123 && draw == Draw && !ctx);
    ++tabs; return operation_result;
}
static sco_result Hotkey(sco_plugin* who, const char* chord, const char* command,
                         const sco_arg* args, uint32_t count) {
    assert(who == self && std::strcmp(chord, "f7") == 0 && std::strcmp(command, "test.go") == 0);
    assert(!args && count == 0);
    ++hotkeys; return operation_result;
}
static sco_result Error(sco_plugin* who, char* out, uint32_t* n) {
    assert(who == self && *n >= 12);
    ++errors; std::strcpy(out, "key is used"); return error_result;
}

// Allocate only the advertised extent, so ASan catches even partial function-pointer reads.
template<class T> static T* Truncate(const T& source, size_t size) {
    auto* p = static_cast<T*>(std::malloc(size));
    assert(p && size >= sizeof(uint32_t));
    std::memcpy(p, &source, size);
    p->size = static_cast<uint32_t>(size);
    return p;
}
static void Reset() { queries = tabs = hotkeys = errors = logs = 0; message.clear(); }
static void Register(const sco_api* api) { RegisterBuiltinTab(api, self, "test.tab", "Test", 123, Draw); }
static void Bind(const sco_api* api) { BindBuiltinHotkey(api, self, "f7", "test.go"); }

int main() {
    sco_api api{}; api.size = sizeof(api); api.query_service = Query; api.log = Log;
    sco_ui_v1 ui{}; ui.size = sizeof(ui); ui.register_tab = Tab; ui.bind_hotkey = Hotkey; ui.last_error = Error;
    published = &ui;

    Register(nullptr); Bind(nullptr); assert(queries == 0 && logs == 0);
    constexpr size_t query_end = offsetof(sco_api, query_service) + sizeof(api.query_service);
    auto* short_api = Truncate(api, query_end - 1);
    Register(short_api); assert(!queries && logs == 1 && message.find("unavailable or incomplete") != std::string::npos);
    std::free(short_api); Reset();
    short_api = Truncate(api, offsetof(sco_api, log) + sizeof(api.log) - 1);
    Register(short_api); assert(!queries && !logs); std::free(short_api); Reset();
    auto* exact_api = Truncate(api, query_end);
    Register(exact_api); assert(queries == 1 && tabs == 1 && !logs); std::free(exact_api); Reset();
    api.query_service = nullptr; Register(&api); assert(!queries && logs == 1); api.query_service = Query; Reset();
    api.log = nullptr; query_result = SCO_NOT_FOUND; Register(&api); assert(queries == 1 && !tabs && !logs);
    api.log = Log; query_result = SCO_OK; Reset();
    published = nullptr; Register(&api); assert(queries == 1 && !tabs && logs == 1); Reset();
    published = &ui; query_result = SCO_UNAVAILABLE; Register(&api); assert(queries == 1 && !tabs && logs == 1);
    query_result = SCO_OK; Reset();

    constexpr size_t tab_end = offsetof(sco_ui_v1, register_tab) + sizeof(ui.register_tab);
    auto* short_ui = Truncate(ui, tab_end - 1); published = short_ui;
    Register(&api); assert(!tabs && !errors && logs == 1); std::free(short_ui); Reset();
    auto* exact_ui = Truncate(ui, tab_end); published = exact_ui;
    Register(&api); assert(tabs == 1 && !logs); std::free(exact_ui); Reset();
    published = &ui; ui.register_tab = nullptr; Register(&api); assert(!tabs && !errors && logs == 1);
    ui.register_tab = Tab; Reset();
    constexpr size_t bind_end = offsetof(sco_ui_v1, bind_hotkey) + sizeof(ui.bind_hotkey);
    short_ui = Truncate(ui, bind_end - 1); published = short_ui;
    Bind(&api); assert(!hotkeys && !errors && logs == 1); std::free(short_ui); Reset();
    exact_ui = Truncate(ui, bind_end); published = exact_ui;
    Bind(&api); assert(hotkeys == 1 && !logs); std::free(exact_ui); Reset();
    published = &ui; ui.bind_hotkey = nullptr; Bind(&api); assert(!hotkeys && !errors && logs == 1);
    ui.bind_hotkey = Hotkey; Reset();

    operation_result = SCO_BAD_ARG;
    Register(&api); assert(tabs == 1 && errors == 1 && logs == 1 && message.find("key is used") != std::string::npos);
    Reset(); Bind(&api); assert(hotkeys == 1 && errors == 1 && logs == 1 && message.find("key is used") != std::string::npos);
    Reset();
    constexpr size_t error_end = offsetof(sco_ui_v1, last_error) + sizeof(ui.last_error);
    short_ui = Truncate(ui, error_end - 1); published = short_ui;
    Bind(&api); assert(hotkeys == 1 && !errors && logs == 1 && message.find("no reason available") != std::string::npos);
    std::free(short_ui); Reset();
    published = &ui; ui.last_error = nullptr; Bind(&api); assert(hotkeys == 1 && !errors && logs == 1); Reset();
    ui.last_error = Error; error_result = SCO_FAILED; Bind(&api);
    assert(errors == 1 && logs == 1 && message.find("key is used") == std::string::npos);
    return 0;
}
