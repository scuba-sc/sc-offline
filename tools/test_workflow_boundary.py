#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Targeted SDK-only workflow boundary proof, including three negative controls.

Pass an extracted SDK's include directory and the corresponding archive identity.
No product or host include directory is given to the copied modules. CMake, a C++
compiler and Unix Makefiles are required.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(command, cwd, expect_success=True):
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if expect_success and result.returncode:
        raise RuntimeError(result.stdout)
    return result


def prove(sdk_include, sdk_archive, sdk_version, sdk_sha256):
    with tempfile.TemporaryDirectory(prefix="workflow-boundary-", dir=os.environ.get("TMPDIR")) as directory:
        root = Path(directory)
        source, build = root / "plugins", root / "build"
        # The plugin project configures all feature subdirectories even when
        # only the two workflow targets below are built.
        shutil.copytree(ROOT / "plugins", source)
        run(["cmake", "-S", str(source), "-B", str(build), "-G", "Unix Makefiles",
             f"-DSCO_SDK_INCLUDE_DIR={sdk_include}", f"-DSCO_SDK_ARCHIVE={sdk_archive}",
             f"-DSCO_SDK_VERSION={sdk_version}", f"-DSCO_SDK_SHA256={sdk_sha256}",
             "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], root)
        run(["cmake", "--build", str(build), "--target", "sc-offline-npc", "sc-offline-inspector"], root)
        all_commands = json.loads((build / "compile_commands.json").read_text())
        commands = [command for command in all_commands
                    if Path(command["file"]).resolve().relative_to(source).parts[0] in {"npc", "inspector"}]
        if len(commands) != 4:
            raise AssertionError(f"Expected exactly four SDK-only translation units, saw {len(commands)}")
        # Ask the same compiler for non-system dependencies. Directory prefixes do not
        # bless unrelated product files: each dependency must live in the copied feature or SDK.
        toolchain_metadata = set()
        if sys.platform == "darwin":
            # AppleClang's -MM includes this compiler-injected, non-header SDK metadata file.
            sysroot = Path(run(["xcrun", "--show-sdk-path"], root).stdout.strip()).resolve()
            toolchain_metadata.add(sysroot / "SDKSettings.json")
        for command in commands:
            translation_unit = Path(command["file"]).resolve().relative_to(source)
            feature_source = source / translation_unit.parts[0]
            if feature_source.name not in {"npc", "inspector"}:
                raise AssertionError(f"Unexpected translation unit: {translation_unit}")
            argv = shlex.split(command["command"])
            argv.pop(argv.index("-o") + 1)
            argv.remove("-o")
            argv.remove("-c")
            output = run(argv + ["-MM"], command["directory"]).stdout
            dependencies = shlex.split(output.replace("\\\n", " "))[1:]
            if not dependencies:
                raise AssertionError("Empty dependency trace")
            for dep in dependencies:
                path = Path(dep).resolve()
                if not (path.is_relative_to(feature_source) or path == source / "picker.h"
                        or path.is_relative_to(sdk_include) or path in toolchain_metadata):
                    raise AssertionError(f"Non-SDK input: {path.name}")
        print("workflow-boundary: two modules built; four translation-unit dependency traces clean")
        original = (source / "npc/npc.cpp").read_text()
        original_mtime_ns = (source / "npc/npc.cpp").stat().st_mtime_ns
        mutants = {
            "host registration header": '#include "sco/plugins.h"\n',
            "product version header": '#include "version.h"\n',
            "private menu/teleport symbols": (
                'extern void Menu_RequestClearNpcs();\nextern void TeleportGo();\n'
                'namespace { struct Forbidden { Forbidden() { Menu_RequestClearNpcs(); TeleportGo(); } } forbidden; }\n'),
        }
        for label, mutation in mutants.items():
            (source / "npc/npc.cpp").write_text(original + "\n" + mutation)
            # Model timestamp-preserving copies and coarse-resolution filesystem clocks.
            # Negative controls must compile even when a mutation looks older than its object.
            mutant = source / "npc/npc.cpp"
            os.utime(mutant, ns=(mutant.stat().st_atime_ns, original_mtime_ns))
            run(["cmake", "--build", str(build), "--target", "clean"], root)
            result = run(["cmake", "--build", str(build), "--target", "sc-offline-npc"], root, False)
            if result.returncode == 0:
                raise AssertionError(f"External target admitted {label}")
            # A failed build for an unrelated reason is not a valid negative control.
            tokens = {"host registration header": "sco/plugins.h", "product version header": "version.h",
                      "private menu/teleport symbols": "Menu_RequestClearNpcs"}
            if tokens[label] not in result.stdout:
                raise AssertionError(f"Wrong failure for {label}: {result.stdout}")
            print(f"workflow-boundary: rejected {label}")
        print("workflow-boundary: 4 cases passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-include", required=True, type=Path)
    parser.add_argument("--sdk-archive", required=True, type=Path)
    parser.add_argument("--sdk-version", required=True)
    parser.add_argument("--sdk-sha256", required=True)
    args = parser.parse_args()
    include = args.sdk_include.resolve()
    if not (include / "sco_api.h").is_file():
        parser.error("--sdk-include must contain sco_api.h")
    prove(include, args.sdk_archive.resolve(), args.sdk_version, args.sdk_sha256)
