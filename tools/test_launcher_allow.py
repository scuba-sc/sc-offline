#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the real launcher parser with portable Windows-CRT stand-ins.

This exercises parsing and shared range policy, not Win32 environment or firewall APIs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'launcher/launcher.cpp').read_text()
parser = source[source.index('struct Ipv4Range {'):source.index('\nstatic bool ReadConfig(', source.index('struct Ipv4Range {'))]
harness = r'''
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include "src/private_network_ranges.h"
using std::wstring;
static wstring Trim(const wstring& s) {
    const auto begin = s.find_first_not_of(L" \t\r\n");
    if (begin == wstring::npos) return L"";
    return s.substr(begin, s.find_last_not_of(L" \t\r\n") - begin + 1);
}
template <size_t N, typename... Args>
static int swprintf_s(wchar_t (&out)[N], const wchar_t* fmt, Args... args) {
    return std::swprintf(out, N, fmt, args...);
}
''' + parser + r'''
int main() {
    std::vector<Ipv4Range> ranges; wstring normalized;
    if (!ParseAllowList(L" 100.64.0.0/10 , 10.0.0.0/8 ", ranges, normalized) ||
        normalized != L"100.64.0.0/10,10.0.0.0/8" || ranges.size() != 2) return 1;
    for (const auto* bad : {L"8.0.0.0/8", L"100.0.0.0/8", L"100.128.0.0/9",
                            L"172.0.0.0/8", L"192.168.0.0/15", L"100.64.0.1/10",
                            L"4294967396.64.0.0/10", L"+100.64.0.0/10", L"100.64.0.0/+10",
                            L"100.64.0.0/4294967306", L"100.64.0.0/10x",
                            L"100.64.0.0/10,", L",100.64.0.0/10",
                            L"100.64.0.0/10,8.0.0.0/8"})
        if (ParseAllowList(bad, ranges, normalized) || !ranges.empty() || !normalized.empty()) return 2;
    for (const auto* good : {L"", L"100.64.0.0/10", L"100.127.255.255/32",
                             L"10.0.0.0/8", L"172.16.0.0/12", L"192.168.0.0/16"})
        if (!ParseAllowList(good, ranges, normalized)) return 3;
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'launcher_allow.cpp'
    exe = Path(tmp) / 'launcher_allow'
    cpp.write_text(harness)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('launcher allow parser: passed (portable CRT stand-ins)')
