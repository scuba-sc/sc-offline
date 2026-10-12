#!/usr/bin/env python3
"""Exact host ImGui source/configuration pin, separate from sco_api.h's C ABI.

The WIP version label alone is not a pin. Digests were compared against the
immutable sc-offline revision below; intentional upgrades hand-edit these pins
with source provenance and require drawing-plugin rebuilds. No auto-update mode.
"""
import hashlib
from pathlib import Path
import sys

SOURCE_REVISION = "1ee0727cc63866a95e709ae61dd5dd355b511253"
EXPECTED_VERSION = '"1.92.8 WIP"'
EXPECTED_VERSION_NUM = '19277'
PUBLIC_HEADERS = ('imconfig.h', 'imgui.h', 'LICENSE.txt')
# Includes implementation-private inputs: these are NOT added to the public SDK.
SHA256 = {
    "LICENSE.txt": "173506a2d6f7fb67990d257fb2507f188690eca39060c39469ae7bef43aae2a3",
    "imconfig.h": "1353bfa59da16eec481723fd4e5d910356f0406b79e3c4142e0c9867bafffab5",
    "imgui.cpp": "5d0202cda228c1568462599054d3f67c4cab579d23cb7e48708e9dde4869e309",
    "imgui.h": "cd65b1905d5cbdaeab82f6ff180ae8767bd72c7bc99ee258c74665eb2d3ee1fa",
    "imgui_draw.cpp": "363fedbf1eee46b2f44e2c2ae158253da4864846cd0f26b7ff2a1c645d302559",
    "imgui_impl_dx11.cpp": "1c0c3af25b45dceb4c45de7da591244f4935718154e7149d0e90858958c6f19d",
    "imgui_impl_dx11.h": "fb8a314a6a2904dcc5bfeb938a169389e170f233ad367d33f1ce5c7bde586a7c",
    "imgui_impl_win32.cpp": "12316399a6acd772a2ec376df7a6eeb138b385f0e00c2dc4d48776a25cc81794",
    "imgui_impl_win32.h": "2736595b8de1e56871661d5d9b96deec567d35521746fbe2ade8f0543acdfd2b",
    "imgui_internal.h": "5f8ee8d2150450b4d11ec378595fe96a44f3ce7fd0e75203b05d0169f09136ee",
    "imgui_tables.cpp": "052ffb50f195577efc930d28741c30f9a80b55afad70feb56cc46bad981342bc",
    "imgui_widgets.cpp": "b51ce815076c7aee971dc34d17178f33a3b29a456d670a2e11e5cae95db51382",
    "imstb_rectpack.h": "889b396795202d1457560a797a7242e96f6f132d4b88ca2d69be58bf05e1771f",
    "imstb_textedit.h": "a985f5fa0ed97353d493b497961e9eef52082edcd045cf6954b69990ec9d0741",
    "imstb_truetype.h": "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528",
}


def check(root):
    host = root / 'src/third_party/imgui'
    sdk = root / 'external/sco-core/sdk/third_party/imgui'
    errors = []
    actual = {path.relative_to(host).as_posix() for path in host.rglob('*') if path.is_file()}
    for name in sorted(actual - SHA256.keys()):
        errors.append(f'{name}: unpinned host input')
    for name, expected in SHA256.items():
        path = host / name
        try:
            content = path.read_bytes()
        except OSError:
            errors.append(f'{name}: missing/unreadable host input')
            continue
        if hashlib.sha256(content).hexdigest() != expected:
            errors.append(f'{name}: differs from exact host source pin {SOURCE_REVISION}')
        if name in PUBLIC_HEADERS:
            try:
                if content != (sdk / name).read_bytes():
                    errors.append(f'{name}: host/SDK mismatch')
            except OSError:
                errors.append(f'{name}: missing/unreadable SDK input; initialize sco-core submodule')
    return errors


def main():
    root = Path(__file__).resolve().parent.parent
    errors = check(root)
    for error in errors:
        print(f'imgui-pin: FAIL {error}')
    if errors:
        return 1
    print(f'imgui-pin: OK {len(SHA256)} exact source/configuration inputs, '
          f'{len(PUBLIC_HEADERS)} SDK matches; {EXPECTED_VERSION} ({EXPECTED_VERSION_NUM}); '
          'third-party ABI coupling, not the frozen C ABI')
    return 0


if __name__ == '__main__':
    sys.exit(main())
