#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit legacy mining settings import. Run only with launcher and game closed.

Dry-run by default. Never loads plugins/hooks or changes admission. Use --apply
for a deliberate user-initiated import, --replace to overwrite existing settings.
"""
import argparse
import os
from pathlib import Path
import sqlite3

NAMES = {"mining": "enabled", "mining_debug": "debug"}
ENV = {"mining": "SC_OFFLINE_MINING", "mining_debug": "SC_OFFLINE_MINING_DEBUG"}


def switch(text):
    token = text.strip().lower()
    if token in ("1", "on", "yes", "true"):
        return b"bool:true"
    if token in ("0", "off", "no", "false"):
        return b"bool:false"
    raise ValueError("legacy mining values must be on/off, 1/0, yes/no or true/false")


def source_values(ini=None, environment=False):
    raw = {}
    if environment:
        raw = {key: os.environ[name] for key, name in ENV.items() if name in os.environ}
    else:
        if ini is None:
            raise ValueError("explicit ini source required")
        if ini.stat().st_size > 65536:
            raise ValueError("legacy ini exceeds 64 KiB")
        for line in ini.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if not line or line.startswith(("#", ";")) or "=" not in line:
                continue
            key, value = (s.strip() for s in line.split("=", 1))
            key = key.lower()
            if key not in NAMES:
                continue
            if key in raw:
                raise ValueError("duplicate legacy mining setting: " + key)
            raw[key] = value
    if not raw:
        raise ValueError("source contains no legacy mining settings; nothing imported")
    # Validate every value before opening or writing the destination.
    return {"sco.settings." + NAMES[key]: switch(value) for key, value in raw.items()}


def import_values(data_root, values, apply=False, replace=False):
    # The explicit root is trusted operator input; its storage child and database
    # must not alias another namespace. No writes at all during a dry-run.
    root = data_root.resolve(strict=True)
    storage = root / "storage"
    dbpath = storage / "mining.db"
    if storage.is_symlink() or dbpath.is_symlink():
        raise ValueError("refusing symlinked mining storage/database")
    if not apply:
        return "dry-run"
    storage.mkdir(exist_ok=True)
    with sqlite3.connect(dbpath, timeout=0) as db:
        db.execute("BEGIN IMMEDIATE")
        db.execute("CREATE TABLE IF NOT EXISTS sco_kv(key TEXT PRIMARY KEY NOT NULL, value BLOB NOT NULL) WITHOUT ROWID")
        sql = ("INSERT OR REPLACE" if replace else "INSERT OR IGNORE") + " INTO sco_kv(key,value) VALUES(?,?)"
        for key, value in values.items():
            db.execute(sql, (key, value))
    return "applied (existing settings " + ("replaced" if replace else "preserved") + ")"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--ini", type=Path, help="Explicit legacy sc-offline.ini")
    source.add_argument("--environment", action="store_true", help="Explicitly import this process's SC_OFFLINE_MINING* values")
    parser.add_argument("--data-root", type=Path, required=True, help="Existing installation data directory")
    parser.add_argument("--apply", action="store_true", help="Write mining.db while launcher/game are closed")
    parser.add_argument("--replace", action="store_true", help="Replace existing mining settings (otherwise keep them)")
    args = parser.parse_args()
    try:
        values = source_values(args.ini, args.environment)
        outcome = import_values(args.data_root, values, args.apply, args.replace)
    except (OSError, ValueError, sqlite3.Error) as error:
        parser.exit(1, "mining settings import refused: " + str(error) + "\n")
    for key, value in sorted(values.items()):
        print(key + " = " + value.decode())
    print(outcome + "; no hooks/game actions; engine admission unchanged")


if __name__ == "__main__":
    main()
