#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One targeted mining workflow: module plus deliberate synthetic settings import."""
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from import_settings import import_values, source_values


def main():
    subprocess.run(sys.argv[1:], check=True, timeout=30)
    with tempfile.TemporaryDirectory(prefix="mining-import-", dir=os.environ.get("TMPDIR")) as tmp:
        root = Path(tmp)
        ini = root / "legacy.ini"
        ini.write_text("mining = on\nmining_debug = off\n")
        values = source_values(ini)
        assert values == {"sco.settings.enabled": b"bool:true", "sco.settings.debug": b"bool:false"}
        assert import_values(root, values) == "dry-run"
        assert not (root / "storage").exists()
        import_values(root, values, apply=True)
        dbpath = root / "storage/mining.db"
        def kept():
            with closing(sqlite3.connect(dbpath)) as db:
                return dict(db.execute("SELECT key,value FROM sco_kv"))
        assert kept() == values
        with closing(sqlite3.connect(dbpath)) as db, db:
            db.execute("INSERT INTO sco_kv VALUES('unrelated', X'7A')")
        ini.write_text("mining = off\nmining_debug = on\n")
        changed = source_values(ini)
        import_values(root, changed, apply=True)
        assert kept()["sco.settings.enabled"] == b"bool:true"  # repeated import doesn't clobber settings
        import_values(root, changed, apply=True, replace=True)
        assert kept() == dict(changed, unrelated=b"z")
        ini.write_text("mining = true\nmining_debug = bad\n")
        try:
            source_values(ini)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid setting admitted")
        assert kept() == dict(changed, unrelated=b"z")
        ini.write_text("mining = on\nmining = off\n")
        try:
            source_values(ini)
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate setting admitted")
        os.environ["SC_OFFLINE_MINING"] = "yes"
        os.environ["SC_OFFLINE_MINING_DEBUG"] = "no"
        assert source_values(environment=True) == values
        alias = root / "alias"
        alias.mkdir()
        (alias / "storage").symlink_to(root / "storage", target_is_directory=True)
        try:
            import_values(alias, values, apply=True)
        except ValueError:
            pass
        else:
            raise AssertionError("storage symlink admitted")
    print("mining deliberate import dry-run/idempotence/replace/refusals PASS (synthetic)")


if __name__ == "__main__":
    main()
