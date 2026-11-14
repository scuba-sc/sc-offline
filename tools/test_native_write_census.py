#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail when a native write site in src/ is not classified in native_write_census.json.

This is a static source census. It does not run Windows, observe the game, or
establish runtime behavior or a native barrier; activation under the process-retained composition is unverified in game.

Matching is by file + enclosing function + call label, with counts: a function
with three InstallDetour( calls needs census entries summing to count 3.

Known limits of the scan (a source scan, not a parser):
  * Comments and string/char literals are blanked first; macros that expand to a
    write are only seen at their #define, not at their use.
  * The enclosing function is the OUTERMOST function-looking brace block. Member
    functions of local structs and lambdas are attributed to the function that
    contains them. Constructor initializer lists and K&R definitions are not
    recognized; #if/#else branches with unbalanced braces make the scan fail.
  * Only src/*.cpp and src/*.h are scanned; subdirectories (builtins/,
    third_party/) are not. Writes made inside sco-core (for example the game
    pack's own hooks) never appear in this source tree.
  * Direct stores are found only by the DIRECT_STORES patterns below. Stores
    into product-allocated buffers, game-object value writes and calls into
    game functions that mutate state are out of scope.
  * Helper definitions (HELPER_BODIES) are excluded; their call sites are the
    census sites. A new write inside a helper body is not detected.
  * An 'unreachable' entry is checked only for a direct caller by name.
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parents[1]

CLASSES = ('gate_code', 'late_code', 'slot', 'per_request', 'os_hook',
           'core_registration', 'entry_gate', 'unreachable')
ADMISSIONS = ('none', 'owner read', 'hook callback lease', 'mission route')
REQUIRED = ('file', 'function', 'call', 'target', 'class', 'installed_from', 'admission')
OPTIONAL = ('note', 'count', 'table', 'rows')

# call label -> regex on comment/string-stripped code. A leading qualifier is part of the match.
QUAL = r'(?<![\w:])(?:[A-Za-z_]\w*::)*'
CALL_PATTERNS = {
    'InstallDetour': QUAL + r'InstallDetour\s*\(',
    'HookFunction': QUAL + r'HookFunction\s*\(',
    'WriteCode': QUAL + r'WriteCode\s*\(',
    'AllocateNear': QUAL + r'AllocateNear\s*\(',
    'SwapSlot': QUAL + r'SwapSlot\s*\(',
    'SwapHubSlot': QUAL + r'SwapHubSlot\s*\(',
    'SetWindowsHookExW': QUAL + r'SetWindowsHookExW\s*\(',
    'SetUnhandledExceptionFilter': QUAL + r'SetUnhandledExceptionFilter\s*\(',
    'sco::Subscribe': r'(?<![\w:])sco::Subscribe\s*\(',
    'sco::SetLogSink': r'(?<![\w:])sco::SetLogSink\s*\(',
    # Product-local wrappers around the above (hooks.cpp, fleet.cpp, services.cpp, patches.cpp).
    'Detour': r'(?<![\w:])Detour\s*\(',
    'EnsureHook': r'(?<![\w:])EnsureHook\s*\(',
    'EnsureHangarHook': r'(?<![\w:])EnsureHangarHook\s*\(',
    'NearData': r'(?<![\w:])NearData\s*\(',
}
# Direct stores and callback placements (label -> regex).
DIRECT_STORES = {
    'object field store': r'\*\s*reinterpret_cast<uint64_t\*>\(\s*entry\s*\)\s*=(?!=)',
    'cave pointer store': r'\*\s*reinterpret_cast<StubObject\*\*>\(\s*slot\s*\)\s*=(?!=)',
    'Platform callback': r'(?<![\w.])(?:pf\.(?:setCapabilities|scripts)|options\.platform\.(?:hostMenu|hostService))\s*=(?!=)',
    'Options callback': r'(?<![\w.])options\.inspect\s*=(?!=)',
}
ALL_PATTERNS = {**CALL_PATTERNS, **DIRECT_STORES}

# (file, enclosing function) -> labels excluded there because the body IS the helper.
HELPER_BODIES = {
    ('src/hooks.cpp', 'Detour'): {'InstallDetour'},
    ('src/hooks.cpp', 'HookFunction'): {'Detour'},
    ('src/hooks.cpp', 'NearData'): {'AllocateNear'},
    ('src/fleet.cpp', 'EnsureHook'): {'HookFunction'},
    ('src/services.cpp', 'EnsureHangarHook'): {'HookFunction'},
}

NOT_FUNCTION = {'if', 'for', 'while', 'switch', 'catch', '__except', '__try', 'else', 'do', 'try', 'return',
                'sizeof', 'decltype', 'alignas', 'static_assert'}
QUALIFIERS = re.compile(r'(?:\s+(?:const|noexcept|override|final|volatile)|\s*->\s*[\w:<>*&\s]+?)+\s*$')


def strip_code(text):
    """Blank comments and string/char literal contents; keep length and newlines."""
    out = list(text)
    i, n = 0, len(text)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != '\n':
                out[k] = ' '

    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.compile(r'R"([^()\\ ]{0,16})\(').match(text, i)
            if not m:
                i += 1
                continue
            end = text.find(')' + m.group(1) + '"', m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            blank(i + 2, end - 1)
            i = end
        elif c == '"' or (c == "'" and not re.search(r'(?<![\w])\d\w*$', text[max(0, i - 24):i])):
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            blank(i + 1, j)
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def blank_preprocessor(code):
    lines = code.split('\n')
    cont = False
    for k, line in enumerate(lines):
        if cont or line.lstrip().startswith('#'):
            cont = line.rstrip().endswith('\\')
            lines[k] = ' ' * len(line)
        else:
            cont = False
    return '\n'.join(lines)


def classify_header(header):
    """'ns', 'type', 'func:<name>' or 'other' for the text in front of a '{'."""
    h = header.strip()
    if re.match(r'(?:inline\s+)?namespace\b', h) or re.match(r'extern\s*"', h) or h == 'extern':
        return 'ns', None
    if not h or h[-1] in '=,(' or re.search(r'\breturn$', h):
        return 'other', None
    if re.search(r'\b(?:struct|class|union|enum)\b', h) and '(' not in h.split('{')[0]:
        return 'type', None
    h = QUALIFIERS.sub('', h)
    if h.endswith(')'):
        depth = 0
        for k in range(len(h) - 1, -1, -1):
            depth += (h[k] == ')') - (h[k] == '(')
            if depth == 0:
                before = h[:k].rstrip()
                if before.endswith(']'):
                    return 'other', None          # lambda
                m = re.search(r'((?:[A-Za-z_~]\w*::)*(?:operator\s*\(\)|operator\S+|~?[A-Za-z_]\w*))$', before)
                if m:
                    name = m.group(1).split('::')[-1]
                    if name not in NOT_FUNCTION:
                        return 'func', name
                break
    return 'other', None


def function_spans(view, display):
    """Outermost function blocks as (start, end, name). Raises on unbalanced braces."""
    spans, stack, last = [], [], 0
    func = None
    for i, c in enumerate(view):
        if c in ';{}':
            if c == '{':
                if func is not None:
                    stack.append('other')
                else:
                    kind, name = classify_header(view[last:i])
                    stack.append(kind)
                    if kind == 'func':
                        func = (i, name)
            elif c == '}':
                if not stack:
                    raise ValueError(f'{display}: unbalanced closing brace')
                kind = stack.pop()
                if kind == 'func' and func is not None and not any(k == 'func' for k in stack):
                    spans.append((func[0], i, func[1]))
                    func = None
            last = i + 1
    if stack:
        raise ValueError(f'{display}: unbalanced braces (an #if/#else branch can cause this)')
    return spans


def is_declaration(code, start):
    """True for 'bool Name(...)' definitions/declarations: only type words in front on the line."""
    line_start = code.rfind('\n', 0, start) + 1
    prefix = code[line_start:start].strip()
    if not prefix:
        return False
    if re.search(r'[=(),!|?]|&&', prefix):
        return False
    words = re.findall(r'[\w:<>*&]+', prefix)
    if not words or words[-1] in ('return', 'else', 'case', 'throw', 'new', 'delete', 'co_return', 'goto'):
        return False
    return all(w not in ('return', 'else', 'case', 'throw') for w in words)


def scan(src_dir):
    """Return (occurrences, codes). occurrences: list of (key, line), key = (file, function, label)."""
    found, codes = [], {}
    files = sorted(list(src_dir.glob('*.cpp')) + list(src_dir.glob('*.h')))
    for path in files:
        rel = f'src/{path.name}'
        code = strip_code(path.read_text(encoding='utf-8', errors='replace'))
        codes[rel] = code
        spans = function_spans(blank_preprocessor(code), rel)
        for label, pattern in ALL_PATTERNS.items():
            for m in re.finditer(pattern, code):
                if label in CALL_PATTERNS and is_declaration(code, m.start()):
                    continue
                func = next((name for a, b, name in spans if a < m.start() < b), '<file scope>')
                if label in HELPER_BODIES.get((rel, func), ()):
                    continue
                found.append(((rel, func, label), code.count('\n', 0, m.start()) + 1))
    return found, codes


def table_rows(code, symbol):
    m = re.search(r'\b' + re.escape(symbol) + r'\s*\[[^\]]*\]\s*=\s*\{', code)
    if not m:
        return None
    depth, rows = 1, 0
    for c in code[m.end():]:
        if c == '{':
            depth += 1
            rows += depth == 2
        elif c == '}':
            depth -= 1
            if depth == 0:
                return rows
    return None


def has_direct_caller(codes, function):
    pattern = re.compile(r'(?<![\w])' + re.escape(function) + r'\s*\(')
    for rel, code in codes.items():
        for m in pattern.finditer(code):
            if not is_declaration(code, m.start()) and not re.match(r'\s*\([^)]*\)\s*(?:const\s*)?\{', code[m.end() - 1:]):
                return rel
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--census', type=Path, default=root / 'tools/native_write_census.json')
    ap.add_argument('--src', type=Path, default=root / 'src')
    args = ap.parse_args()

    problems = []
    try:
        entries = json.loads(args.census.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        sys.exit(f'native write census: FAIL\n  cannot read census {args.census.name}: {e}')
    if not isinstance(entries, list):
        sys.exit('native write census: FAIL\n  census must be a JSON array of objects')
    try:
        occurrences, codes = scan(args.src)
    except ValueError as e:
        sys.exit(f'native write census: FAIL\n  {e}')

    classified = Counter()
    per_class = Counter()
    late = []
    for index, e in enumerate(entries):
        where = f'entry #{index}'
        if not isinstance(e, dict):
            problems.append(f'{where}: not an object')
            continue
        missing = [k for k in REQUIRED if not isinstance(e.get(k), str) or not e[k].strip()]
        if missing:
            problems.append(f'{where} ({e.get("file")}:{e.get("function")}): missing or empty fields: {", ".join(missing)}')
            continue
        where = f'entry #{index} ({e["file"]}:{e["function"]} {e["call"]})'
        extra = sorted(set(e) - set(REQUIRED) - set(OPTIONAL))
        if extra:
            problems.append(f'{where}: unknown fields: {", ".join(extra)}')
        if e['class'] not in CLASSES:
            problems.append(f'{where}: invalid class "{e["class"]}" (valid: {", ".join(CLASSES)})')
        if e['admission'] not in ADMISSIONS:
            problems.append(f'{where}: invalid admission "{e["admission"]}" (valid: {" | ".join(ADMISSIONS)})')
        if e['call'] not in ALL_PATTERNS:
            problems.append(f'{where}: unknown call label "{e["call"]}"')
        if re.search(r'\bline\b\s*\d|:\d+\b', e['function'] + e['target'] + e['installed_from']):
            problems.append(f'{where}: line numbers rot; do not put them in the census')
        count = e.get('count', 1)
        if not isinstance(count, int) or isinstance(count, bool) or count < 1:
            problems.append(f'{where}: "count" must be a positive integer')
            continue
        if e['class'] == 'unreachable':
            if not e.get('note', '').strip():
                problems.append(f'{where}: class unreachable needs a note stating the evidence')
            caller = has_direct_caller(codes, e['function'])
            if caller:
                problems.append(f'{where}: marked unreachable but {e["function"]}( is called in {caller}; reclassify it')
        if 'table' in e:
            rows = table_rows(codes.get(e['file'], ''), e['table'])
            if rows is None or rows != e.get('rows'):
                problems.append(f'{where}: table {e["table"]} has {rows} rows in source, census says {e.get("rows")}')
        classified[(e['file'], e['function'], e['call'])] += count
        per_class[e['class']] += count
        if e['class'] == 'late_code':
            late.append(e)

    seen = Counter(key for key, _ in occurrences)
    for key in sorted(seen):
        if seen[key] > classified[key]:
            lines = ', '.join(str(ln) for k, ln in occurrences if k == key)
            problems.append(f'UNCLASSIFIED: {key[0]} function {key[1]} call {key[2]}: '
                            f'{seen[key]} in source, {classified[key]} in census (source lines {lines})')
    for key in sorted(classified):
        if classified[key] > seen[key]:
            problems.append(f'STALE: {key[0]} function {key[1]} call {key[2]}: '
                            f'{classified[key]} in census, {seen[key]} in source')

    if problems:
        print('native write census: FAIL')
        for p in problems:
            print('  ' + p)
        print(f'  ({len(problems)} problem(s); add or fix entries in {args.census.name}, '
              'classifying each write by class and timing)')
        sys.exit(1)

    total = sum(per_class.values())
    print('native write census summary')
    for cls in CLASSES:
        print(f'  {cls:<18}{per_class[cls]:>4}')
    print(f'  {"total":<18}{total:>4}')
    print(f'late_code entries (actionable): {len(late)}')
    for e in late:
        print(f'  {e["file"]} {e["function"]} -> {e["target"]} [{e["installed_from"]}]')
    print(f'native write census: PASS (static; {total} sites)')


if __name__ == '__main__':
    main()
