# Launcher language

The launcher has an active English fallback for its actions, status, hints, plugin-page controls
and window messages. It starts in English. Command names, child output and questions in the
Output page remain English. Parent-authored launcher notices around that output are localized;
their fixed style tags and the child's confirmation protocol remain separate from catalogue text.
Plugin names, descriptions, setting labels, enum values and stored
identifiers remain owned by each plugin.

Set `language = en`, `ru` or `auto` in `sc-offline.ini`, or use the language selector at the top
of the launcher. `auto` is an explicit choice: it uses Russian for a Russian Windows UI language,
and English otherwise. The selector saves only the language setting. It is unavailable while
a command or game is running. A failed save retains the previous choice and original ini.
Manual ini edits take effect on the next launcher start.
The selector refuses a reparse-linked ini so it cannot replace the user's link; edit that file
manually and restart instead.

## Install the initial Russian catalogue

The source examples are [English](../locales/launcher/en.ini) and
[Russian](../locales/launcher/ru.ini). They contain project-authored launcher text; they do not
contain a game translation or unpublished fork assets. These examples are outside the release
data tree and are **not automatically staged, installed or updated** by this change.

With the launcher closed, create `data/locales` beside `sc-offline.exe` and copy the source
`locales/launcher/ru.ini` to `data/locales/ru.ini`. Start the launcher and select Russian, or set
`language = ru` before starting it. The file is read once at startup; close and restart the
launcher to adopt catalogue edits. English needs no catalogue file.

Missing or rejected catalogues use compiled English. Missing individual keys also use English.
The selector explains when a requested Russian catalogue or required font glyphs are unavailable.
Body, bold and output fonts must cover the catalogue; otherwise the launcher uses English.
The current system-font choices are Segoe UI and Consolas, with the existing default-font fallback.
This is an English/Russian launcher phase. Shaping and right-to-left layout are not provided.

## Add or update text

Stable keys and English templates live in [locale_strings.h](../launcher/locale_strings.h).
Change a template there, update both source examples, then run
`python3 tools/check_launcher_localization.py`. The check requires complete source examples,
reports missing or obsolete keys and validates placeholder contracts. Runtime catalogues can be
partial; contributor examples must be complete. The English example must equal compiled English.

The format is flat UTF-8 `launcher.key = value`, with optional leading UTF-8 BOM, LF or CRLF,
and whole-line `#` comments. Keys are explicit ASCII names; values are trimmed at their edges.
Use `\n` for a newline and `\\` for a literal backslash. Other escapes, empty values, duplicate
or unknown keys, malformed UTF-8, NUL/control bytes and malformed placeholders reject the whole
catalogue. Limits are 256 KiB per file, 8,192 bytes per line, 96 bytes per key and 4,096 decoded
bytes per value. The error diagnostic identifies the reason and line when available (0 means file-level); accepted partial
catalogues list missing keys. Values are text, never translated printf format strings.

Named placeholders must preserve the English names and counts. For example,
`launcher.action.running = Running {command}` may translate the surrounding words while keeping
`{command}` exactly once. Placeholder values, output length and argument names are checked before
substitution; a refused format displays the locale's fallback error text. Raw command and plugin identifiers
are never translated or rewritten.

Visible text is separate from interactive identity. Existing original English ID strings, including
`##` suffixes, remain the inputs to `GetID` and `InvisibleButton`. New language choices use fixed
`###` IDs. Changing language does not change the action, command, plugin ID or persisted value.

The test helper exercises catalogue refusal/fallback, formatting, language decisions, pinned ImGui
ID semantics and UTF-8 text fitting. With an installed Cyrillic system font it checks glyph coverage;
it also checks the default font fallback. Windows additionally tests atomic saves, duplicate ini
keys, Unicode paths, refused writes and handle cleanup using owned synthetic files.

## SDK boundary and acceptance

There is no plugin-visible language service in this change. A shared mod/plugin language API
needs an additive named service in sco-core, public lifetime/namespace contracts and consumer
integration. Launcher catalogues do not bypass that SDK boundary. User-installed game translations
are not imported.

Headless and filesystem tests do not establish Wine C:/Z: behavior, game acceptance, staging
adoption or release approval. **Not run in game.**
