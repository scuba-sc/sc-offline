#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Move every published-SDK pin of this repository to a new sco-core SDK release in one step.

    python tools/bump_sdk_pin.py --version 2.4.1 --tag sdk-v2.4.1 --game-pack 0.8.1 \\
        --commit <40 hex: the commit the tag points at> --run-id <publication workflow run id> \\
        --sha256sums SHA256SUMS
    python tools/bump_sdk_pin.py --check [same options]       report stale pins, change nothing
    python tools/bump_sdk_pin.py --print-tag                   the SDK tag the repository pins now

A candidate is pinned the same way, with its own tag and the plain version: --version 2.4.1 --tag sdk-v2.4.1-rc.1.
The archives of sdk-v2.4.1-rc.1 are named for 2.4.1 (sco-sdk-2.4.1.zip), as for sdk-v2.4.0-rc.2; only the tag carries
-rc.<n>. The later move to the final sdk-v2.4.1 is the same command with --tag sdk-v2.4.1 and the final hashes.
    python tools/bump_sdk_pin.py --release-json release.json   take tag, commit, run id, game pack and hashes
                                                               from the published release.json instead

Only run it after the SDK is published: the hashes of the published archives are not known before. Nothing
here downloads anything; give it the release's SHA256SUMS (or --sha256 NAME=HEX once per archive).

The pins it moves, by what they are:

  version       sdk-vX, sco-sdk[-core|-core-source|-host-source-sc]-X(.zip), SDK_VERSION, core_sdk_version,
                "SDK X" and "published ... X" in prose, "[X release]"
  game pack     sco-sdk-game-sc-G.zip and "game pack G"
  archive hash  the hashes of sco-sdk-X.zip, sco-sdk-core-source-X.zip and sco-sdk-host-source-sc-X.zip
  source commit the commit of the SDK tag (the stable-host jobs and tools/ci-stable-sdk-preflight.sh)
  run id        the publication run the stable-host job asserts

in .github/workflows/build.yml, tools/check_*_sdk.py, tools/contracts_sdk_check.py,
tools/ci-stable-sdk-preflight.sh, tools/test_sdk_generators.py and docs/sdk-workflows.md; and `sdk-vX` in the CMake
files. It does not touch history: docs/handoff/, the "Published SDK X" sections of docs/sdk-candidate.md,
CHANGELOG.md, external/ and launcher-rs/. The current values are read from the repository itself
(tools/check_inspector_sdk.py, the expected-hash table and assertions in build.yml, docs/sdk-workflows.md), so a
run on a tree that is already at the target changes nothing.

It prints every change (file, line, old, new). It also lists, as follow-ups for a person, pages that still name
the old version in prose and the hash table to put in docs/sdk-candidate.md. --check exits 1 when any pin differs
from the target (without options it checks that the sites agree with tools/check_inspector_sdk.py).
Python 3 standard library only.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Files where every pin is rewritten.
PIN_FILES = (
    ".github/workflows/build.yml",
    "tools/check_*_sdk.py",
    "tools/contracts_sdk_check.py",
    "tools/ci-stable-sdk-preflight.sh",
    "tools/test_sdk_generators.py",
    "docs/sdk-workflows.md",
)
# Files where only the tag `sdk-vX` is rewritten (comments naming the pinned release).
CMAKE_FILES = ("CMakeLists.txt", "plugins/**/CMakeLists.txt", "plugins/CMakeLists.txt", "tools/**/CMakeLists.txt")
# Pages that may still name the old version for a reason (history); listed, never rewritten.
REVIEW_FILES = ("docs/*.md", "README.md", "tools/*.json", "plugins/*/README.md", "THIRD_PARTY_NOTICES.md")

VER = r"\d+\.\d+\.\d+"
RC = r"-rc\.\d+"          # the only tag suffix: a release candidate of the SDK in the tag
HEX64 = re.compile(r"[0-9a-f]{64}")
HEX40 = re.compile(r"[0-9a-f]{40}")
# (name, regex with the version as group "v"); each match has its group replaced by the target.
VERSION_PATTERNS = (
    # The tag is the one pin that may carry a suffix: sdk-v2.4.1 (the release) or sdk-v2.4.1-rc.1 (a candidate of it).
    # The archives keep the plain version in both cases (sco-sdk-2.4.1.zip is the asset of sdk-v2.4.1-rc.1).
    ("tag", re.compile(r"sdk-v(?P<v>%s)(?P<rc>%s)?(?![\d-])" % (VER, RC))),
    ("archive", re.compile(r"sco-sdk-(?:core-|core-source-|host-source-sc-)?(?P<v>%s)(?![\d-])" % VER)),
    ("SDK_VERSION", re.compile(r"SDK_VERSION(?:\s*[=:]\s*)'(?P<v>%s)'" % VER)),
    ("sdk_version in a test", re.compile(r"args\.sdk_version,\s*'(?P<v>%s)'" % VER)),
    ("core_sdk_version", re.compile(r"core_sdk_version'\]\s*==\s*'(?P<v>%s)'" % VER)),
    ("prose", re.compile(r"(?:(?<=SDK )|(?<=published stable )|(?<=Published SDK ))(?P<v>%s)(?![\d-])" % VER)),
    ("release link text", re.compile(r"\[(?P<v>%s) release\]" % VER)),
)
GAME_PACK_PATTERNS = (
    ("game pack archive", re.compile(r"sco-sdk-game-sc-(?P<v>%s)" % VER)),
    ("game pack", re.compile(r"game pack (?P<v>%s)" % VER)),
)
# Files that also hold made-up versions for their tests: only the repository's own pin is changed there.
ONLY_REFERENCE_VERSION = ("tools/test_sdk_generators.py",)
ARCHIVES = ("sco-sdk-{v}.zip", "sco-sdk-core-source-{v}.zip", "sco-sdk-host-source-sc-{v}.zip")


class BumpError(Exception):
    pass


def split_tag(tag: str) -> tuple[str, str]:
    """('2.4.1', '-rc.1') for sdk-v2.4.1-rc.1 and ('2.4.1', '') for sdk-v2.4.1; anything else is refused."""
    match = re.fullmatch(r"sdk-v(%s)(%s)?" % (VER, RC), tag)
    if not match:
        raise BumpError("tag %r must be sdk-v<version> or sdk-v<version>-rc.<number>" % tag)
    return match.group(1), match.group(2) or ""


def files(root: Path, patterns) -> list[Path]:
    found: dict[Path, None] = {}
    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            if path.is_file():
                found[path] = None
    return list(found)


def read_text(path: Path) -> tuple[str, str]:
    raw = path.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in raw else "\n"
    return raw.replace("\r\n", "\n"), newline


def current_pins(root: Path) -> dict:
    """The pins as the repository holds them now (the reference every other site must agree with)."""
    inspector, _ = read_text(root / "tools/check_inspector_sdk.py")
    version = re.search(r"^SDK_VERSION\s*=\s*'(%s)'" % VER, inspector, re.M)
    sha = re.search(r"^SDK_SHA256\s*=\s*'(%s)'" % HEX64.pattern, inspector, re.M)
    if not version or not sha:
        raise BumpError("tools/check_inspector_sdk.py: SDK_VERSION or SDK_SHA256 not found")
    pins = {"version": version.group(1), "hashes": {"sco-sdk-{v}.zip": sha.group(1)}}
    workflow, _ = read_text(root / ".github/workflows/build.yml")
    for template in ARCHIVES[1:]:
        name = template.format(v=pins["version"])
        found = re.search(r"'%s':\s*'(%s)'" % (re.escape(name), HEX64.pattern), workflow)
        if found:
            pins["hashes"][template] = found.group(1)
    commit = re.search(r"\['source'\]\['sha'\]\s*==\s*'(%s)'" % HEX40.pattern, workflow)
    run_id = re.search(r"\['builder'\]\['run_id'\]\s*==\s*(\d+)", workflow)
    tag = re.search(r"SDK_TAG:\s*'sdk-v(%s)(%s)?'" % (VER, RC), workflow)
    pins["rc"] = (tag.group(2) or "") if tag and tag.group(1) == pins["version"] else ""
    pins["tag"] = "sdk-v" + pins["version"] + pins["rc"]
    pins["commit"] = commit.group(1) if commit else None
    pins["run_id"] = run_id.group(1) if run_id else None
    docs, _ = read_text(root / "docs/sdk-workflows.md")
    pack = re.search(r"game pack (%s)" % VER, docs)
    pins["game_pack"] = pack.group(1) if pack else None
    return pins


def parse_sums(text: str) -> dict[str, str]:
    sums = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and HEX64.fullmatch(parts[0].lower()):
            sums[parts[1].lstrip("*")] = parts[0].lower()
    return sums


def target_from_args(args, ref: dict) -> dict:
    """The target pins: options given win; anything not given stays as the repository holds it."""
    release = {}
    if args.release_json:
        data = json.loads(Path(args.release_json).read_text(encoding="utf-8"))
        release["tag"] = data["tag"]
        release["commit"] = data["source"]["sha"]
        release["run_id"] = str(data["builder"]["run_id"])
        release["sums"] = {a["name"]: a["sha256"] for a in data["artifacts"]}
        for name in release["sums"]:
            match = re.fullmatch(r"sco-sdk-game-sc-(%s)\.zip" % VER, name)
            if match:
                release["game_pack"] = match.group(1)
    sums = dict(release.get("sums", {}))
    if args.sha256sums:
        sums.update(parse_sums(Path(args.sha256sums).read_text(encoding="utf-8")))
    for item in args.sha256 or []:
        name, _, digest = item.partition("=")
        sums[name] = digest.lower()
    tag = args.tag or release.get("tag")
    tag_version, tag_rc = split_tag(tag) if tag else (None, None)
    version = args.version or tag_version
    explicit = bool(version or tag or args.game_pack or args.commit or args.run_id or sums or release)
    # No tag given: the same version keeps the pinned tag (a candidate stays a candidate), a new version means its release.
    rc = tag_rc if tag else (ref["rc"] if (version or ref["version"]) == ref["version"] else "")
    target = {"version": version or ref["version"], "rc": rc, "game_pack": args.game_pack or release.get("game_pack") or ref["game_pack"],
              "commit": args.commit or release.get("commit") or ref["commit"],
              "run_id": args.run_id or release.get("run_id") or ref["run_id"], "hashes": {}}
    if not re.fullmatch(VER, target["version"]):
        raise BumpError("--version must look like 2.4.1")
    target["tag"] = "sdk-v" + target["version"] + target["rc"]
    if tag and tag_version != target["version"]:
        raise BumpError("--tag %s does not match --version %s (the tag is sdk-v<version> or sdk-v<version>-rc.<number>)" % (tag, target["version"]))
    if target["game_pack"] and not re.fullmatch(VER, target["game_pack"]):
        raise BumpError("--game-pack must look like 0.8.1")
    if target["commit"] and not HEX40.fullmatch(target["commit"]):
        raise BumpError("--commit must be 40 lowercase hex")
    if target["run_id"] and not re.fullmatch(r"\d+", str(target["run_id"])):
        raise BumpError("--run-id must be a number")
    # A new version, or the same version as a candidate or the release: both change the commit, the run and the hashes.
    moving = target["version"] != ref["version"] or target["rc"] != ref["rc"]
    for template in ARCHIVES:
        name = template.format(v=target["version"])
        old = ref["hashes"].get(template)
        if name in sums:
            target["hashes"][template] = sums[name]
        elif not moving:
            target["hashes"][template] = old
        elif old is not None or template == ARCHIVES[0]:
            raise BumpError("no SHA-256 for %s: pass --sha256sums or --sha256 %s=<hex>" % (name, name))
    for template, digest in target["hashes"].items():
        if digest is not None and not HEX64.fullmatch(digest):
            raise BumpError("the SHA-256 for %s is not 64 lowercase hex" % template.format(v=target["version"]))
    if moving and explicit:
        for key, flag in (("commit", "--commit"), ("run_id", "--run-id"), ("game_pack", "--game-pack")):
            given = {"commit": args.commit or release.get("commit"), "run_id": args.run_id or release.get("run_id"),
                     "game_pack": args.game_pack or release.get("game_pack")}[key]
            if ref[key] and not given:
                raise BumpError("%s is required: the repository still holds the old %s" % (flag, key.replace("_", " ")))
    return target


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def rewrite(text: str, ref: dict, target: dict, version_patterns=VERSION_PATTERNS, full: bool = True,
            only_ref: bool = False):
    """(new text, [(line, old, new)]) with every pin set to the target.

    only_ref: change only versions equal to the repository's current pin (a test file also holds made-up versions).
    """
    changes: list[tuple[int, str, str]] = []

    def apply(pattern, wanted, suffix_group=None):
        nonlocal text
        edits = []
        for m in pattern.finditer(text):
            end = m.end(suffix_group) if suffix_group and m.group(suffix_group) else m.end("v")
            old = text[m.start("v"):end]
            if old != wanted and (not only_ref or m.group("v") in (ref["version"], ref["game_pack"])):
                edits.append((m.start("v"), end, old))
        for start, end, old in reversed(edits):
            changes.append((line_of(text, start), old, wanted))
            text = text[:start] + wanted + text[end:]

    for name, pattern in version_patterns:
        if name == "tag":
            apply(pattern, target["version"] + target["rc"], "rc")
        else:
            apply(pattern, target["version"])
    if full:
        if target["game_pack"]:
            for _name, pattern in GAME_PACK_PATTERNS:
                apply(pattern, target["game_pack"])

        def swap(old, new):
            nonlocal text
            if not old or not new or old == new:
                return
            for m in reversed(list(re.finditer(re.escape(old), text))):
                changes.append((line_of(text, m.start()), old, new))
                text = text[:m.start()] + new + text[m.end():]

        for template, old in ref["hashes"].items():
            swap(old, target["hashes"].get(template))
        swap(ref["commit"], target["commit"])
        swap(ref["run_id"], target["run_id"])
        # The stable-host qualification asserts what the release is: a full release, or for a candidate a pre-release.
        flag = "True" if target["rc"] else "False"
        for m in reversed(list(re.finditer(r"release_view\['isPrerelease'\] is (True|False)", text))):
            if m.group(1) != flag:
                changes.append((line_of(text, m.start(1)), m.group(1), flag))
                text = text[:m.start(1)] + flag + text[m.end(1):]
    changes.sort()
    return text, changes


def run(root: Path, args) -> int:
    ref = current_pins(root)
    if args.print_tag:
        print(ref["tag"])
        return 0
    target = target_from_args(args, ref)
    total = 0
    leftovers: list[str] = []
    for path in files(root, PIN_FILES) + [p for p in files(root, CMAKE_FILES) if p not in files(root, PIN_FILES)]:
        rel = path.relative_to(root).as_posix()
        text, newline = read_text(path)
        cmake = rel.endswith("CMakeLists.txt")
        new, changes = rewrite(text, ref, target, VERSION_PATTERNS[:1] if cmake else VERSION_PATTERNS, full=not cmake,
                               only_ref=rel in ONLY_REFERENCE_VERSION)
        for number, line in enumerate(new.split("\n"), 1):
            if not cmake and target["version"] != ref["version"] and re.search(r"(?<![\d.])%s(?![\d-])" % re.escape(ref["version"]), line):
                leftovers.append("%s:%d: still names %s" % (rel, number, ref["version"]))
        if not changes:
            continue
        total += len(changes)
        for line, old, replacement in changes:
            print("%s%s:%d: %s -> %s" % ("stale " if args.check else "", rel, line, old, replacement))
        if not args.check:
            path.write_bytes(new.replace("\n", newline).encode("utf-8"))
    pins = "%s, tag %s (game pack %s)" % (target["version"], target["tag"], target["game_pack"])
    if args.check:
        print("bump_sdk_pin: %d stale pin(s) against SDK %s" % (total, pins) if total else "bump_sdk_pin: every pin is at SDK %s" % pins)
        return 1 if total else 0
    print("bump_sdk_pin: %d pin(s) changed, now SDK %s" % (total, pins))
    follow_ups(root, target, leftovers)
    return 0


def follow_ups(root: Path, target: dict, leftovers=()) -> None:
    """Things only a person can finish: prose that names another version, and the new hash table."""
    left = list(leftovers)
    for path in files(root, REVIEW_FILES):
        text, _ = read_text(path)
        for number, line in enumerate(text.split("\n"), 1):
            for match in re.finditer(r"(?:SDK|sdk-v|published)\s*v?(%s)(?![\d-])" % VER, line):
                if match.group(1) != target["version"]:
                    left.append("%s:%d: names SDK %s" % (path.relative_to(root).as_posix(), number, match.group(1)))
                    break
    if left:
        print("follow-up: pages that still name another SDK version (history may be right; read each):")
        for item in left:
            print("  " + item)
    print("follow-up: docs/sdk-candidate.md needs a \"Published SDK %s\" section with these archives:" % target["version"])
    for template in ARCHIVES:
        digest = target["hashes"].get(template)
        if digest:
            print("  %s  %s" % (template.format(v=target["version"]), digest))
    if target["commit"]:
        print("  tag %s, source commit %s" % (target["tag"], target["commit"]))
    if target["rc"]:
        print("follow-up: %s is a release candidate. Say so in docs/sdk-candidate.md; the final sdk-v%s moves every pin again "
              "(new hashes, commit and run id)." % (target["tag"], target["version"]))


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", help="the SDK version, for example 2.4.1")
    ap.add_argument("--tag", help="the SDK release tag, sdk-v<version>")
    ap.add_argument("--game-pack", help="the game pack version of that SDK, for example 0.8.1")
    ap.add_argument("--commit", help="the commit the tag points at (40 hex)")
    ap.add_argument("--run-id", help="the publication workflow run id (release.json: builder.run_id)")
    ap.add_argument("--sha256sums", help="the release's SHA256SUMS file")
    ap.add_argument("--sha256", action="append", metavar="NAME=HEX", help="the SHA-256 of one archive, for example sco-sdk-2.4.1.zip=<hex>")
    ap.add_argument("--release-json", help="the release's release.json: tag, commit, run id, game pack and hashes")
    ap.add_argument("--check", action="store_true", help="report stale pins and change nothing")
    ap.add_argument("--print-tag", action="store_true", help="print the SDK tag the repository pins (sdk-v2.4.1 or sdk-v2.4.1-rc.1) and exit")
    ap.add_argument("--root", default=str(ROOT), help="repository root (default: this checkout)")
    return ap


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        return run(Path(args.root).resolve(), args)
    except (BumpError, OSError, ValueError, KeyError) as error:
        print("bump_sdk_pin: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
