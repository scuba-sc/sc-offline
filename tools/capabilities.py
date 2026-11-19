#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the launcher capability table from the pinned sco-core status registry.

Reads <core>/sdk/status_registry.json and greps <core>/src for the environment switches the host
reads. Writes:
  launcher-rs/crates/scol-contract/src/capabilities.json   (machine table for the launcher)
  docs/capabilities.md                                      (plain table for players)

  python tools/capabilities.py --core external/sco-core            write both files
  python tools/capabilities.py --core external/sco-core --check    fail if either file is stale
                                                                   or the wording check trips

Release policy (maintainer decision 2026-11-17): every capability switch is ON by default in every
release package, is a visible setting the player can turn OFF, and its text says it has not been
run in game. The diagnostic switch (exp_entities_diag) is not a feature: its hooks patch game
functions to write log lines, and it is OFF by default. The "registry default" column is what Core
itself ships; it is not the release default. Each switch is the key "exp_" plus the variable name
without "SCO_", in lower case, in launcher/sc-offline.ini; the tool checks that the launcher source
(kExperimental in launcher/launcher.cpp) and the shipped ini agree with its table.
Nothing here was run in game.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JSON_OUT = ROOT / "launcher-rs/crates/scol-contract/src/capabilities.json"
MD_OUT = ROOT / "docs/capabilities.md"
LAUNCHER_CPP = ROOT / "launcher/launcher.cpp"
SHIPPED_INI = ROOT / "launcher/sc-offline.ini"

# Capabilities served by each switch. The switch NAMES are found by grepping Core source; this map
# only says which registry capabilities a found switch controls. A name found in source and absent
# here, or listed here and absent from source, is an error.
SWITCH_CAPS = {
    "SCO_LOADOUT_ASYNC": ["game.loadout.async"],
    "SCO_INVENTORY_READ": ["game.inventory.read"],
    "SCO_ACTOR_STATS_WRITE": ["game.actor_stats.write"],
    "SCO_AUDIO_NATIVE": ["game.audio.triggers", "game.audio.play_once"],
    "SCO_CVARS_NATIVE": ["game.cvars.read"],
}
# Per-switch notes that say what the switch really does in Core source. Checked against
# docs/game/audio.md and audio_engine.cpp in Core: the native audio adapter refuses to install.
SWITCH_NOTE = {
    "SCO_AUDIO_NATIVE": "Core currently refuses the native audio adapter even with this on, so both "
                        "audio capabilities stay off. The switch only asks Core to try.",
    "SCO_ACTOR_STATS_WRITE": "The registry says OFF pending in-game confirmation. The release package "
                             "still sets it on by owner decision; you can turn it off.",
}
# Capabilities the owner has seen run in game. Everything else says "Not run in game."
SEEN_ONCE = {
    "game.loadout.async": "Seen once in game: gear was attached through the Gear page. See docs/status.md.",
}
# Switches that exist in Core source and that the launcher does not set, with the reason.
NOT_SET_BY_LAUNCHER = {
    "SCO_LOADOUT_OUTFIT": "Opens the outfit slots (clothing, hat, head accessory, head horn, jewellery) of "
                          "game.loadout.outfit on the asynchronous route. The launcher never sets it, so "
                          "that capability stays off.",
}
# Diagnostic switches: not capabilities. Off by default, a visible key in the ini.
DIAGNOSTIC = {
    "SCO_ENTITIES_DIAG": "Installs hooks that patch game functions (the entity streaming functions) "
                         "only to write log lines, and runs one diagnostic walk that writes to mod.log. "
                         "Not a capability and no gameplay change. Off by default; turn it on only when "
                         "asked for a bug report. Not run in game.",
}


def ini_key(name):
    """sc-offline.ini key of a switch: exp_ and the variable name without SCO_, lower case."""
    return "exp_" + name[len("SCO_"):].lower()

LEVEL_TEXT = {
    "in_game_verified": "run in game on a recorded date",
    "sigcheck_resolved": "signature rows resolve in a build report; nothing executed",
    "off_pending_probe": "rows resolve; an in-game probe must settle an open question first",
    "synthetic_only": "synthetic tests only",
    "blocked": "blocked: no trustworthy locator",
    "undocumented": "no evidence recorded",
}

# Phrases that overclaim. There is no negation exemption: generated text never needs these
# phrases, so any match fails. Spaces and hyphens are interchangeable; "the" is optional.
_G = r"in[\s-]*(?:the[\s-]*)?game"
BANNED = [
    r"works?(?:[\s-]+fine)?[\s-]+" + _G, r"working[\s-]+" + _G, r"runs(?:[\s-]+fine)?[\s-]+" + _G,
    r"(?:verified|confirmed|proven|tested|validated|supported)[\s-]+" + _G,
    r"fully[\s-]+(?:supported|working|functional)", r"guaranteed[\s-]+to[\s-]+work",
    r"functions?[\s-]+" + _G, r"functional[\s-]+" + _G, r"confirmed[\s-]+working",
    r"(?:is|are|it's)[\s-]+game[\s-]+verified",
]
BANNED_RE = re.compile("(?:%s)" % "|".join(BANNED), re.I)

# kExperimental rows in launcher/launcher.cpp: { L"exp_x", L"SCO_X", true },
KEXP_RE = re.compile(r'\{\s*L"(exp_[a-z0-9_]+)"\s*,\s*L"(SCO_[A-Z0-9_]+)"\s*,\s*(true|false)\s*\}')

SWITCH_RE = re.compile(r'GetEnvironmentVariable[AW]?\s*\(\s*L?"(SCO_[A-Z0-9_]+)"')
ENABLED_RE = re.compile(r'\bEnabled\s*\(\s*"(SCO_[A-Z0-9_]+)"')


def wording_problems(text, where):
    return [f"{where}: overclaiming phrase {m.group(0)!r}" for m in BANNED_RE.finditer(text)]


def evidence_problems(md):
    """Positive rule: every table row of generated text says it was not run in game."""
    return [f"capabilities.md: row lacks 'Not run in game': {l[:60]}" for l in md.splitlines()
            if l.startswith("| `") and "Not run in game" not in l and "Seen once in game" not in l]


def launcher_problems():
    """The C++ table and the shipped ini must carry the same keys, variables and defaults as this tool."""
    want = {ini_key(n): (n, True) for n in SWITCH_CAPS}
    want.update({ini_key(n): (n, False) for n in DIAGNOSTIC})
    cpp = LAUNCHER_CPP.read_text(encoding="utf-8", errors="replace")
    have = {m.group(1): (m.group(2), m.group(3) == "true") for m in KEXP_RE.finditer(cpp)}
    problems = []
    if have != want:
        problems.append(f"launcher/launcher.cpp kExperimental differs from the table: has {sorted(have.items())}, "
                        f"want {sorted(want.items())}")
    ini = {}
    for line in SHIPPED_INI.read_text(encoding="utf-8").splitlines():
        k, eq, v = line.partition("=")
        if eq and not line.lstrip().startswith("#") and k.strip().lower().startswith("exp_"):
            ini.setdefault(k.strip().lower(), []).append(v.strip().lower())
    for key, (_, default_on) in want.items():
        if ini.get(key) != ["on" if default_on else "off"]:
            problems.append(f"launcher/sc-offline.ini: {key} must appear once, set {'on' if default_on else 'off'}; has {ini.get(key)}")
    for key in sorted(set(ini) - set(want)):
        problems.append(f"launcher/sc-offline.ini: unknown capability key {key}")
    return problems


def find_switches(core):
    found = {}
    for p in sorted((core / "src").rglob("*")):
        if p.suffix not in (".cpp", ".h", ".hpp", ".cc", ".c"):
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        for rx in (SWITCH_RE, ENABLED_RE):
            for m in rx.finditer(text):
                found.setdefault(m.group(1), set()).add(p.relative_to(core).as_posix())
    return {k: sorted(v) for k, v in found.items()}


SPECIAL_REASON = {
    "game.cvars.write": "Core refuses production native cvar writes; no switch enables them. "
                        "Not run in game.",
}


def reason_for(cap):
    if cap["capability"] in SPECIAL_REASON:
        return SPECIAL_REASON[cap["capability"]]
    lvl, d = cap["evidence_level"], cap["registry_default"]
    if d == "ON":
        return "Core turns this on by itself when its rows resolve. No player switch exists."
    tail = {
        "off_pending_probe": "Off until an in-game probe settles an open question.",
        "synthetic_only": "Only synthetic tests exist for it.",
        "blocked": "No trustworthy locator, so nothing is published.",
        "sigcheck_resolved": "Rows resolve in a build report; nothing was executed.",
        "undocumented": "The repository records no evidence.",
    }.get(lvl, "")
    return f"Core source reads no environment switch for this capability. {tail} Not run in game."


def build(core):
    raw = (core / "sdk/status_registry.json").read_bytes()
    reg = json.loads(raw)
    found = find_switches(core)
    known = set(SWITCH_CAPS) | set(DIAGNOSTIC) | set(NOT_SET_BY_LAUNCHER)
    if set(found) != known:
        sys.exit(f"switch drift: in Core source only {sorted(set(found) - known)}; "
                 f"in the map only {sorted(known - set(found))}")
    owner = {c: s for s, cs in SWITCH_CAPS.items() for c in cs}
    caps = []
    for svc in reg["services"]:
        for c in svc.get("capabilities", []):
            if not c.get("name"):
                continue  # unnamed registry entries (blocked FOV, "no capability of its own") are skipped on purpose
            row = {
                "capability": c["name"],
                "service": svc["service"],
                "registry_default": c["default"],
                "evidence_level": c["evidence_level"],
                "switch": owner.get(c["name"]),
                "evidence": c.get("evidence", ""),
            }
            if row["switch"] is None:
                row["reason"] = reason_for(row)
            caps.append(row)
    missing = set(owner) - {r["capability"] for r in caps}
    if missing:
        sys.exit(f"switch map names capabilities absent from the registry: {sorted(missing)}")
    switches = [{
        "name": s,
        "ini_key": ini_key(s),
        "capabilities": SWITCH_CAPS[s],
        "on_value": "1",
        "release_default": "on",
        "player_can_turn_off": True,
        "source": found[s],
        "note": ("On by default in every release package; a visible setting the player can turn off. "
                 + (SWITCH_NOTE[s] + " " if s in SWITCH_NOTE else "")
                 + next((SEEN_ONCE[c] for c in SWITCH_CAPS[s] if c in SEEN_ONCE), "Not run in game.")),
    } for s in sorted(SWITCH_CAPS)]
    diags = [{"name": s, "ini_key": ini_key(s), "source": found[s], "release_default": "off",
              "player_can_turn_off": True, "note": DIAGNOSTIC[s]}
             for s in sorted(DIAGNOSTIC)]
    return {
        "schema": 1,
        "generated_by": "tools/capabilities.py",
        "registry_schema_version": reg["schema_version"],
        "registry_sha256": hashlib.sha256(raw.replace(b"\r\n", b"\n")).hexdigest(),
        "release_policy": "Every capability switch is on by default in every release package and "
                          "is a visible setting the player can turn off. The diagnostic switch is not "
                          "a feature and is off by default. Only game.loadout.async was seen once in game; "
                          "nothing else here was run in game.",
        "switches": switches,
        "diagnostics": diags,
        "not_set_by_launcher": NOT_SET_BY_LAUNCHER,
        "capabilities": caps,
    }


def cell(s):
    return str(s).replace("|", "\\|").replace("\n", " ")


def render_md(t):
    L = ["<!-- SPDX-License-Identifier: GPL-3.0-only -->",
         "<!-- Generated by tools/capabilities.py. Do not edit; run the tool. -->",
         "# Capability switches", "",
         "Some game features sit behind a switch. In every release package each switch is **on by "
         "default** and shows as a setting you can turn **off**. **Only `game.loadout.async` was seen once "
         "in game; none of the other features has been run in game.** The evidence column says what "
         "exists instead.", "",
         "Each switch is a key in `sc-offline.ini`. The launcher sets the matching variable to the value `1` "
         "for a key that is on and removes the variable for a key that is off, so a value left in your "
         "shell or in Windows cannot turn a switch on. Core reads a switch as on only when its value is "
         "exactly `1`. A switch never bypasses Core's own checks: a capability still needs its signature "
         "rows and a fresh offline admission.", "",
         "## Switches", "",
         "| Key in `sc-offline.ini` | Variable | Asks Core to turn on | Release default | Evidence |",
         "|---|---|---|---|---|"]
    by = {c["capability"]: c for c in t["capabilities"]}
    for s in t["switches"]:
        ev = "; ".join(f"`{c}`: {LEVEL_TEXT[by[c]['evidence_level']]}" for c in s["capabilities"])
        caps = ", ".join(f"`{c}`" for c in s["capabilities"])
        note = SWITCH_NOTE.get(s["name"], "")
        L.append(f"| `{s['ini_key']}` | `{s['name']}` | {caps} | on, you can turn it off | {cell(ev)}. "
                 f"{cell(note + ' ' if note else '')}"
                 f"{next((SEEN_ONCE[c] for c in s['capabilities'] if c in SEEN_ONCE), 'Not run in game.')} |")
    L += ["", "Diagnostic (not a capability and not a feature; off by default):", ""]
    for d in t["diagnostics"]:
        L.append(f"- `{d['ini_key']}` (`{d['name']}`): {d['note']}")
    L += ["", "## Every capability", "",
          "Core default is what the SDK registry ships. Release default is what the launcher "
          "package sets.", "",
          "| Capability | Service | Key in `sc-offline.ini` | Core default | Release default | Evidence |",
          "|---|---|---|---|---|---|"]
    for c in t["capabilities"]:
        sw = f"`{ini_key(c['switch'])}`" if c["switch"] else "none"
        if c["switch"]:
            rel = "on, you can turn it off"
        else:
            rel = "set by Core" if c["registry_default"] == "ON" else "off (no switch)"
        L.append(f"| `{c['capability']}` | `{c['service']}` | {sw} | {c['registry_default']} | {rel} | "
                 f"{cell(LEVEL_TEXT[c['evidence_level']])}. {SEEN_ONCE.get(c['capability'], 'Not run in game.')} |")
    if t.get("not_set_by_launcher"):
        L += ["", "Core switches the launcher never sets:", ""]
        L += [f"- `{n}`: {why}" for n, why in t["not_set_by_launcher"].items()]
    L += ["", "Where the Core default is OFF, a switch changes only what the launcher asks for. "
              "Core still decides whether the capability turns on.", "",
          f"Source: Core `sdk/status_registry.json` (schema {t['registry_schema_version']}, "
              f"sha256 `{t['registry_sha256'][:16]}`).", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--core", required=True, type=Path, help="sco-core source directory")
    ap.add_argument("--check", action="store_true", help="fail when generated files are stale")
    a = ap.parse_args()
    core = a.core.resolve()
    if not (core / "sdk/status_registry.json").is_file():
        sys.exit(f"core dir not found or submodule not initialised: {core}")
    t = build(core)
    drift = launcher_problems()
    if drift:
        sys.exit("\n".join(drift))
    js = json.dumps(t, indent=2, ensure_ascii=False) + "\n"
    md = render_md(t)
    # Registry evidence strings are quoted from Core verbatim and may say "not ... game verified";
    # the wording check covers only text this tool writes.
    own = json.loads(js)
    for c in own["capabilities"]:
        c["evidence"] = ""
    problems = (wording_problems(json.dumps(own), "capabilities.json")
                + wording_problems(md, "capabilities.md") + evidence_problems(md))
    if problems:
        sys.exit("\n".join(problems))
    if a.check:
        bad = [str(p.relative_to(ROOT)) for p, want in ((JSON_OUT, js), (MD_OUT, md))
               if not p.exists() or p.read_bytes().decode("utf-8").replace("\r\n", "\n") != want]
        if bad:
            sys.exit("stale generated file(s): " + ", ".join(bad) + "; run tools/capabilities.py --core <core>")
        print(f"capabilities: {len(t['capabilities'])} capabilities, {len(t['switches'])} switches, up to date")
        return
    JSON_OUT.write_text(js, encoding="utf-8", newline="\n")
    MD_OUT.write_text(md, encoding="utf-8", newline="\n")
    print(f"wrote {JSON_OUT.relative_to(ROOT)} and {MD_OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
