# Launcher language

The launcher window, its status, hints, plugin-page controls and messages come in English and Russian. By default (`language = auto`) it follows the Windows UI language: Russian on a Russian system, English otherwise. Command names, a child command's output and its questions on the Output page stay in English. The launcher's own notices around that output are translated. Plugin names, descriptions, setting labels, enum values and stored identifiers belong to each plugin and are not translated.

## Choose a language

Set `language = en`, `ru` or `auto` in `sc-offline.ini`, or use the language selector in the launcher window. The selector saves only the language setting and is unavailable while a command or the game runs. If the save fails, the previous choice and the original ini stay. A manual ini edit takes effect the next time the launcher starts. If the ini is a reparse link, the selector refuses to replace it; edit the file by hand and restart.

## Built-in Russian and an optional override

Russian text is compiled into the executable from the [source catalogue](../locales/launcher/ru.ini), so no separate file is needed. English is compiled in too, and the [English example](../locales/launcher/en.ini) documents the stable keys and fallback text. These are strings written for this project, not a game translation.

To change the Russian text:

1. Close the launcher.
2. Create `data/locales` beside `sc-offline.exe` and put a UTF-8 `ru.ini` there.
3. Start the launcher. A present file overrides the built-in catalogue and is read once at startup, so restart after each edit.

Remove the file to restore the built-in text. An unreadable or rejected override uses English until you fix or remove it. Keys missing from a valid partial override use English. The selector reports a rejected override or missing glyphs. The launcher's fonts must cover the catalogue, or it uses English. It tries Segoe UI and Consolas, then the default font. Only English and Russian are supported. There is no complex-script shaping and no right-to-left layout.

## Change or add text

Stable keys and English templates live in [locale_strings.h](../launcher/locale_strings.h). To change a template:

1. Edit the template there and update both source examples (`locales/launcher/en.ini` and `ru.ini`).
2. Run `python3 tools/generate_launcher_locale.py --write` to refresh the built-in Russian header.
3. Run `python3 tools/check_launcher_localization.py`. It requires complete source examples, reports missing and obsolete keys, validates placeholders, and requires the English example to equal the compiled English.

Runtime catalogues can be partial. The contributor examples must be complete.

**File format.** Flat UTF-8 lines of the form `launcher.key = value`, with an optional BOM, LF or CRLF line ends, and whole-line `#` comments. Keys are ASCII names. Values are trimmed at both ends. Use `\n` for a newline and `\\` for a backslash. The whole catalogue is rejected for any other escape, an empty value, a duplicate or unknown key, malformed UTF-8, NUL or control bytes, or a malformed placeholder. Limits: 256 KiB per file, 8,192 bytes per line, 96 bytes per key and 4,096 decoded bytes per value. The error names the reason and the line (0 means the whole file), and an accepted partial catalogue lists its missing keys. Values are text, never printf format strings.

**Placeholders.** Keep the English names and counts. For example, `launcher.action.running = Running {command}` may translate the words around `{command}` but must keep it exactly once. Values, output length and argument names are checked before substitution, and a refused format shows the locale's fallback error text. Raw command and plugin identifiers are never translated.

**Interactive identity.** Visible text is separate from the ids behind it. Existing English ID strings, including `##` suffixes, stay as the inputs to `GetID` and `InvisibleButton`. New language choices use fixed `###` IDs. Changing the language never changes an action, a command, a plugin id or a stored value.

## Tests and limits

The test helper covers catalogue refusal and fallback, formatting, language decisions, ImGui ID behavior and UTF-8 text fitting. With a Cyrillic system font installed it also checks glyph coverage, and it checks the default-font fallback. On Windows it tests atomic saves, duplicate ini keys, Unicode paths, refused writes and handle cleanup, using synthetic files.

Plugins have no language service. A shared language API would need a new named service in sco-core with public lifetime and namespace contracts, and consumers that adopt it; launcher catalogues do not bypass that boundary. Game translations installed by users are not imported.

These tests do not establish Wine `C:` and `Z:` behavior, staging or release approval, and none of this has been run in game ([Status](status.md)).
