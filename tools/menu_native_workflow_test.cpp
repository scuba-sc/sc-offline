// SPDX-License-Identifier: GPL-3.0-only
#define NOMINMAX
#include <windows.h>
#include "../src/menu_input.h"
#include <thread>
#include <vector>
#include <cstdio>
namespace {
offline::menu::DeferredInput input;
std::vector<UINT> received;
bool building = true;
LRESULT CALLBACK Window(HWND window, UINT message, WPARAM word, LPARAM parameter) {
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_CHAR) {
        input.Route(building, message, word, parameter,
            [](const auto& event) { received.push_back(event.message); });
        return 0;
    }
    return DefWindowProcW(window, message, word, parameter);
}
}
int main() {
    WNDCLASSW cls{}; cls.lpfnWndProc = Window; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"OfflineMenuInputFixture";
    if (!RegisterClassW(&cls)) return 2;
    HWND window = CreateWindowW(cls.lpszClassName, L"fixture", 0, 0,0,1,1, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    if (!window) return 2;
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread producer([&] {
        SendMessageW(window, WM_LBUTTONDOWN, 1, 0);
        SendMessageW(window, WM_LBUTTONUP, 0, 0);
        SendMessageW(window, WM_CHAR, 'a', 0);
        SetEvent(done);
    });
    for (;;) {
        const DWORD state = MsgWaitForMultipleObjects(1, &done, FALSE, 10000, QS_SENDMESSAGE);
        if (state == WAIT_OBJECT_0) break;
        if (state != WAIT_OBJECT_0+1) { std::terminate(); }
        MSG message; PeekMessageW(&message, nullptr, 0,0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    producer.join(); building = false;
    input.Drain([](const auto& event) { received.push_back(event.message); });
    DestroyWindow(window); CloseHandle(done); UnregisterClassW(cls.lpszClassName, cls.hInstance);
    const bool pass = received == std::vector<UINT>{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_CHAR};
    std::printf("%s native sent-message frame overlap: %zu/3 input events\n", pass?"PASS":"FAIL", received.size());
    return pass?0:1;
}
