from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
for name in ('inspector_locale.h', 'inspector.lua', 'inspector_test.lua'):
    data = (root / 'plugins' / 'inspector' / name).read_bytes()
    text = data.decode('utf-8', 'strict')
    assert '\ufffd' not in text and '\0' not in text, name
catalogue = (root / 'plugins/inspector/inspector_locale.h').read_text(encoding='utf-8-sig')
entries = re.findall(r'\{"([a-z_]+)",\s*"([^"\n]*)",\s*(?:"([^"\n]*)"|nullptr)\}', catalogue)
assert len(entries) == 13 and len({entry[0] for entry in entries}) == len(entries)
for key, en, ru in entries:
    assert en and not any(c in en + ru for c in '{}%'), key
print('Inspector catalogue: UTF-8, unique keys and no format placeholders PASS')
