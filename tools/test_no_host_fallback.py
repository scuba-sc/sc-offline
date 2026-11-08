#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile and run the real no-host branches without host declarations.

This is a synthetic source-fragment test, not a Windows runtime or native proof.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/dllmain.cpp').read_text()
request = source.split('static void RequestLifecycleStop(const char* trigger) {', 1)[1].split('\n}', 1)[0]
worker = source.split('static DWORD WINAPI ModThread(LPVOID param) {', 1)[1].split('\n}', 1)[0]
program = r'''
#include <atomic>
using DWORD = unsigned long;
using HMODULE = void*;
using LPVOID = void*;
#define WINAPI
#define SCO_TITLE "test"
#define SCO_BASED_ON "test"
namespace offline_host { bool NativeActivationAvailable() { return true; } }
static int released = 0, menuStopped = 0, closed = 0, signals = 0;
static void OpenConsole() {}
template <class... Args> static void Log(const char*, Args...) {}
static void ReleaseStartupGate() { ++released; }
static void Menu_RequestStop() { ++menuStopped; }
struct Admission { void Close() { ++closed; } bool ClosedAndDrained() { return true; } };
static Admission g_hookCallbackAdmission;
static int event1 = 0, event2 = 0;
static int* g_hookCallbacksDrainedEvent = &event1;
static int* g_controlWakeEvent = &event2;
static std::atomic<bool> g_stopRequestIssued{false};
static bool SetEvent(int*) { ++signals; return true; }
static unsigned long GetLastError() { return 0; }
static void RequestLifecycleStop(const char* trigger) {
''' + request + r'''
}
static DWORD WINAPI ModThread(LPVOID param) {
''' + worker + r'''
}
int main() {
    if (ModThread(nullptr) != 0 || released != 1) return 1;
    RequestLifecycleStop("synthetic");
    if (menuStopped != 1 || closed != 1 || signals != 2 || !g_stopRequestIssued) return 2;
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='offline-no-host-') as temp:
    tmp = Path(temp)
    src = tmp / 'fallback.cpp'
    src.write_text(program)
    for flags in ([], ['-DSCO_CORE_NON_DRAINING_STOP_API']):
        binary = tmp / ('contract' if flags else 'default')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-parameter', *flags,
                        str(src), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('no-host composition fallback: PASS (synthetic fragments)')
