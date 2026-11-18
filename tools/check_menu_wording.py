#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Plain-language check for the menu pages players see (docs/plugin-ui.md, "Writing a page for players").

Reads the string literals in the source files that build the shipped pages (PAGE_SOURCES) and fails
on internal words: ids and command names, capability names, versions, gate numbers and words from
the code. Log lines, command registrations, includes and bare identifiers are skipped, because they
never reach the page. A literal that has to keep one of the words is listed in ALLOW with the reason.

It reads source text only: it does not build or render the pages, and it is not an in-game check.
Result sentences built at run time in the command handlers (spawn.cpp, crew.cpp, npc.cpp, build.cpp)
are not listed: there every result has a technical text for the reply and log next to the plain one,
which a source scan can't tell apart. The plugins' workflow tests scan the words of every panel they
receive for the same internal words instead (picker_workflow_test, loadout_widget_test,
gear_presets_workflow_test).
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Every source file that builds a page in menu_navigation.h's player sections. The Inspector is a
# developer page (hidden unless "Show developer pages" is on) and is not checked.
PAGE_SOURCES = [
    'plugins/spawn/picker.inc',
    'plugins/spawn/asop_picker.inc',
    'plugins/crew/picker.inc',
    'plugins/npc/picker.inc',
    'plugins/build/picker.h',
    'plugins/spatial/travel_plugin.h',
    'plugins/quantum/controls.h',
    'plugins/loadout/loadout.cpp',
    'plugins/gear_presets/plugin.cpp',
    'src/menu.cpp',
    # Player text only: sidebar names and summaries, readable item and slot names, plain reasons.
    'plugins/spatial/travel_model.h',
    'plugins/loadout/slot_catalogue.h',
    'plugins/loadout/readable_name.h',
    'src/menu_navigation.h',
    'src/menu_shell.cpp',
]

# (word, pattern). Patterns run on one literal, case-insensitively.
BANNED = [
    ('archetype', r'archetype'),
    ('owned', r'\bowned\b'),
    ('ticket', r'\btickets?\b'),
    ('phase', r'\bphase\b'),
    ('catalog', r'\bcatalog(ue)?\b'),
    ('pagination', r'paginat'),
    ('payload', r'payload'),
    ('job', r'\bjobs?\b'),
    ('slot id', r'\bslot ids?\b'),
    ('retained', r'\bretained\b'),
    ('orchestration', r'orchestrat'),
    ('SDK', r'\bsdk\b'),
    ('adapter', r'\badapter\b'),
    ('GameRules', r'gamerules'),
    ('gate number', r'\bG\d{1,2}\b'),
    ('version in a label', r'\(\d+\.\d+\)|requires [a-z_]+ \d+\.\d+'),
    ('command or capability name', r'\b(game|sco|spawn|build|loadout|npc|crew|quantum|teleport|creative)\.[a-z_]+(\.[a-z_]+)*\b'),
    ('"(unavailable)" control', r'\(unavailable\)|\[unknown/unavailable\]'),
    ('ASCII limit', r'ascii characters'),
]

# (file, literal substring) -> why it never reaches a page. These pages keep two texts for every
# result: the command reply and mod.log line (technical, asserted by the SDK checkers) and a plain
# sentence for the page (Reply/Answer's second text). Only the technical half is listed here; where
# the SDK checkers assert it, it has to stay as it is.
REPLY = 'command reply and mod.log only; the page shows the plain sentence passed with it'
ALLOW = {
    ('plugins/build/picker.h', 'Invalid grouped list: no partial catalogue loaded'): 'item-list source line: mod.log only; the page shows state.note',
    ('plugins/build/picker.h', 'Unavailable: catalogue allocation/parse refused'): 'item-list source line: mod.log only; the page shows state.note',
    ('plugins/spatial/travel_plugin.h', 'Unavailable: sco.content 1.0 item/read required'): 'curated status: travel.status reply only',
    ('plugins/spatial/travel_plugin.h', '(game.relocation status unavailable); arrival not confirmed'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'game.relocation refused the move; not moved'): REPLY,
    ('plugins/spatial/travel_plugin.h', ' (game.relocation); arrival is reported when the game confirms it'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'Quick-save storage failed; prior spot retained'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'Usage: quantum.travel <destination>'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'check lists/locations.txt or run quantum.scan'): REPLY,
    ('plugins/spatial/travel_plugin.h', '; game.relocation '): 'travel.status reply only; the page shows a plain summary',
    ('plugins/spatial/travel_plugin.h', 'Bookmark storage failed; in-memory bookmarks retained'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'Delete failed; in-memory bookmarks retained'): REPLY,
    ('plugins/spatial/travel_plugin.h', 'Legacy state import/read failed; prior database retained'): REPLY,
    ('plugins/quantum/controls.h', 'G8 enabled=%u held=%u'): 'quantum.control_status reply only',
    ('plugins/quantum/controls.h', 'G8 state unknown/unavailable'): 'quantum.control_status reply only',
    ('plugins/spatial/travel_model.h', 'game.relocation service or request is unavailable'): 'technical reason text; the page uses its *_plain twin',
}

SKIP_LINE = re.compile(
    r'(->log\(|\blog\(|\bLog\(|LogUi|#include|sizeof\(sco_command\)|register_command|RegisterBuiltinCommand'
    r'|Open<|query_service|SCO_[A-Z_]+_NAME|static_assert|assert\()')
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
# Literals that can't be visible text: ids, file names, format-only strings, and the class-name
# prefixes that name cleanup strips ("NPC_Archetypes-").
NOT_TEXT = re.compile(r'^([a-z0-9_.\-/]*|%[^ ]*|[A-Za-z0-9_]+\.(txt|ini|ini\.tmp|dll|json)|[A-Za-z0-9_\-]+[-_])$')


def literals(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith(('//', '/*', '*')):
            continue
        if SKIP_LINE.search(stripped) and not re.search(r'Node\(|set_panel\(|label|reply|snprintf', line):
            continue
        for match in LITERAL.finditer(line):
            value = match.group(1)
            if not value or NOT_TEXT.match(value):
                continue
            yield number, value


def check(root):
    problems = []
    scanned = 0
    for name in PAGE_SOURCES:
        path = root / name
        if not path.is_file():
            problems.append(f'{name}: page source is missing (update PAGE_SOURCES)')
            continue
        for number, value in literals(path):
            scanned += 1
            for word, pattern in BANNED:
                if not re.search(pattern, value, re.IGNORECASE):
                    continue
                if any(f == name and part in value for (f, part) in ALLOW):
                    continue
                problems.append(f'{name}:{number}: {word}: "{value}"')
    return scanned, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    scanned, problems = check(args.root.resolve())
    for problem in problems:
        print(problem)
    print(f'menu wording: {scanned} literals in {len(PAGE_SOURCES)} page sources, {len(problems)} problem(s) '
          '(source text only; not rendered, not run in game)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
