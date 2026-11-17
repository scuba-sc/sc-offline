// SPDX-License-Identifier: GPL-3.0-only
// Offscreen preview of the in-game menu: draws the product's shell (src/menu_shell.cpp) and the
// product's widget renderer (sco-core's ImGuiRenderer) into a PNG, without the game, a window or a
// GPU (Direct3D WARP). The pages come from a panel dump (tools/panel_dump.h) that the plugins'
// synthetic tests write, so each page is the node tree its plugin really built in that test.
//
// What it is not: an in-game check. Fonts, theme, layout and text are the product's; game state,
// status text and the menu's own Settings page are stand-ins (marked as such on the picture).
//
//   menu_preview <dump.txt> <out folder> [--width 1000] [--height 800] [--developer]
// writes one PNG per page, named after the page id, plus index.txt listing them.
#include "menu_shell.h"
#include "sco/widgets_imgui.h"
#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_impl_dx11.h"
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Nav = sc_offline::menu_nav;

// ---- the dump --------------------------------------------------------------------------------

static std::string Unescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) { out += text[i]; continue; }
        const char c = text[++i];
        out += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
    }
    return out;
}

static std::vector<std::string> Split(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::string field;
    for (const char c : line) {
        if (c == separator) { fields.push_back(field); field.clear(); }
        else field += c;
    }
    fields.push_back(field);
    return fields;
}

struct Record {
    unsigned long sequence = 0;
    sco::widgets::Panel panel;
};

// How full a recorded page is: rows in its lists and combos first (a page with something to choose
// from), then its node count.
static size_t Fullness(const sco::widgets::Panel& panel) {
    size_t rows = 0;
    for (const auto& node : panel.nodes)
        if (node.kind == SCO_WIDGET_LIST || node.kind == SCO_WIDGET_COMBO || node.kind == SCO_WIDGET_TABLE) rows += node.items.size();
    return std::min<size_t>(rows, 64) * 1000 + panel.nodes.size();
}

// Every recorded state of every panel; the preview draws, per panel, the fullest state the test
// produced, the latest one on a tie.
static std::map<std::string, Record> LoadDump(const char* path) {
    std::map<std::string, Record> best;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    Record current;
    size_t expected = 0;
    auto finish = [&] {
        if (current.panel.id.empty() || current.panel.nodes.size() != expected) return;
        auto found = best.find(current.panel.id);
        if (found == best.end() || Fullness(current.panel) >= Fullness(found->second.panel))
            best[current.panel.id] = current;
    };
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto f = Split(line, '\t');
        if (f[0] == "panel" && f.size() == 5) {
            finish();
            current = Record{};
            current.sequence = std::strtoul(f[1].c_str(), nullptr, 10);
            current.panel.id = Unescape(f[2]);
            current.panel.title = Unescape(f[3]);
            expected = std::strtoul(f[4].c_str(), nullptr, 10);
        } else if (f[0] == "node" && f.size() == 18) {
            sco::widgets::Node node;
            node.kind = std::strtoul(f[1].c_str(), nullptr, 10);
            node.id = std::strtoul(f[2].c_str(), nullptr, 10);
            node.parent = std::strtoul(f[3].c_str(), nullptr, 10);
            node.flags = std::strtoul(f[4].c_str(), nullptr, 10);
            node.index = std::atoi(f[5].c_str());
            node.value = std::strtod(f[6].c_str(), nullptr);
            node.minimum = std::strtod(f[7].c_str(), nullptr);
            node.maximum = std::strtod(f[8].c_str(), nullptr);
            node.columns = std::strtoul(f[9].c_str(), nullptr, 10);
            node.severity = std::strtoul(f[10].c_str(), nullptr, 10);
            node.first_row = std::strtoul(f[11].c_str(), nullptr, 10);
            node.visible_rows = std::strtoul(f[12].c_str(), nullptr, 10);
            node.presentation = std::strtoul(f[13].c_str(), nullptr, 10);
            node.label = Unescape(f[14]);
            node.text = Unescape(f[15]);
            node.tooltip = Unescape(f[16]);
            if (!f[17].empty())
                for (const auto& item : Split(f[17], '\x1f')) node.items.push_back(Unescape(item));
            current.panel.nodes.push_back(std::move(node));
        }
    }
    finish();
    return best;
}

// ---- drawing ---------------------------------------------------------------------------------

struct Scene {
    std::map<std::string, Record>* panels;
};

static void DrawPanel(const sco::widgets::Panel& panel) {
    sco::widgets::ImGuiRenderer renderer(false);
    std::vector<sco::widgets::Event> events;
    renderer.Render(panel, events);
}

static void DrawPage(const Nav::Page& page, void* raw) {
    auto& scene = *static_cast<Scene*>(raw);
    if (page.source == Nav::Source::Settings) {
        // Stand-in: the real page is drawn by menu.cpp (background picture, developer switch, about).
        SectionHeading("Background");
        Hint("To use a background image, save it as menu_background.png (or .jpg) in the data folder, then restart the game.");
        SectionHeading("Developer pages");
        bool developer = false;
        ImGui::Checkbox("Show developer pages", &developer);
        Hint("Adds pages for testing and finding bugs, such as the Inspector. They show raw game data and are not needed to play.");
        SectionHeading("About");
        Hint("Press M or Esc to close this menu.");
        return;
    }
    const auto found = scene.panels->find(page.id);
    if (found == scene.panels->end()) return;
    DrawPanel(found->second.panel);
    const auto asop = scene.panels->find("spawn.asop");
    if (page.id == "spawn.vehicles" && asop != scene.panels->end()) {
        SectionHeading("Your own ships");
        DrawPanel(asop->second.panel);
    }
}

static void DrawFrame(std::vector<Nav::Page>& pages, const std::string& current, Scene& scene) {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), size, ImGui::GetColorU32(kSoil));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    bool open = true;
    ImGui::Begin("sc-offline (preview)###main", &open,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    const float statusHeight = MenuStatusStripHeight();
    const float bodyHeight = ImGui::GetContentRegionAvail().y - statusHeight - ImGui::GetStyle().ItemSpacing.y;
    (void)MenuDrawShell(pages, current, bodyHeight, DrawPage, &scene);
    MenuBeginStatusStrip(statusHeight);
    ImGui::TextWrapped("%s", "Preview from synthetic test data: the game's status line shows here in game.");
    MenuEndStatusStrip();
    ImGui::End();
}

// ---- offscreen device and PNG ----------------------------------------------------------------

static bool SavePng(const std::filesystem::path& path, const uint8_t* rgba, UINT width, UINT height, UINT pitch) {
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x) {
            const uint8_t* s = rgba + static_cast<size_t>(y) * pitch + x * 4;
            uint8_t* d = bgra.data() + (static_cast<size_t>(y) * width + x) * 4;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
           && SUCCEEDED(factory->CreateStream(&stream))
           && SUCCEEDED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE))
           && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
           && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
           && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
           && SUCCEEDED(frame->Initialize(nullptr))
           && SUCCEEDED(frame->SetSize(width, height))
           && SUCCEEDED(frame->SetPixelFormat(&format))
           && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)
           && SUCCEEDED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data()))
           && SUCCEEDED(frame->Commit())
           && SUCCEEDED(encoder->Commit());
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: menu_preview <dump.txt> <out folder> [--width N] [--height N] [--developer]\n");
        return 2;
    }
    UINT width = 1000, height = 800;
    bool developer = false;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--width") && i + 1 < argc) width = std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--height") && i + 1 < argc) height = std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--developer")) developer = true;
    }
    auto panels = LoadDump(argv[1]);
    if (panels.empty()) { std::fprintf(stderr, "no panels in %s\n", argv[1]); return 1; }
    const std::filesystem::path out = argv[2];
    std::filesystem::create_directories(out);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                 &device, nullptr, &context))) { std::fprintf(stderr, "no WARP device\n"); return 1; }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    device->CreateTexture2D(&desc, nullptr, &target);
    device->CreateRenderTargetView(target, nullptr, &rtv);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    device->CreateTexture2D(&desc, nullptr, &staging);
    if (!target || !rtv || !staging) { std::fprintf(stderr, "no render target\n"); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    MenuApplyTheme();
    MenuLoadFonts();
    ImGui_ImplDX11_Init(device, context);

    // The pages as the product lists them: every panel, then the menu's own Settings page.
    std::vector<Nav::Page> pages;
    int arrival = 0;
    for (const auto& [id, record] : panels) {
        if (id == "spawn.asop" && panels.count("spawn.vehicles")) continue;
        pages.push_back(Nav::Describe(id, record.panel.title, "", arrival++, Nav::Source::Panel));
    }
    pages.push_back(Nav::Describe(Nav::kSettingsPage, "Menu", "", arrival++, Nav::Source::Settings));
    pages = Nav::Arrange(std::move(pages), developer);

    Scene scene{&panels};
    std::ofstream index(out / "index.txt", std::ios::binary | std::ios::trunc);
    int written = 0;
    for (const auto& page : pages) {
        // A few frames, so auto-sized items and tables settle as they do in the live menu.
        for (int frame = 0; frame < 4; ++frame) {
            io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
            io.DeltaTime = 1.0f / 60.0f;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            DrawFrame(pages, page.id, scene);
            ImGui::Render();
            const float clear[4] = { kSoil.x, kSoil.y, kSoil.z, 1.0f };
            context->OMSetRenderTargets(1, &rtv, nullptr);
            context->ClearRenderTargetView(rtv, clear);
            D3D11_VIEWPORT viewport = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
            context->RSSetViewports(1, &viewport);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        context->CopyResource(staging, target);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) return 1;
        const auto file = out / (page.id + ".png");
        const bool saved = SavePng(file, static_cast<const uint8_t*>(mapped.pData), width, height, mapped.RowPitch);
        context->Unmap(staging, 0);
        if (!saved) { std::fprintf(stderr, "couldn't write %s\n", file.string().c_str()); return 1; }
        index << page.id << '\t' << page.title << '\t' << file.filename().string() << '\n';
        ++written;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    std::printf("menu preview: %d page(s) drawn to %s (synthetic test data; not the game)\n", written, out.string().c_str());
    return 0;
}
