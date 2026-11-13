#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish a Loadout list pack without replacing existing destination files."""
import argparse
import os
from pathlib import Path
import stat
import sys
import tempfile


def reject_redirect(path):
    """Reject existing symlinks and Windows reparse points in a trusted path."""
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"Loadout list pack path is a redirect: {part}")


def matches(path, data):
    reject_redirect(path)
    try:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode):
            raise RuntimeError(f"Loadout list pack destination is not a regular file: {path}")
        with path.open("rb") as existing:
            return existing.read() == data
    except FileNotFoundError:
        return None


def stage(root, files):
    # All source contents are captured before any pack writes.
    entries = [(root / relative, Path(source).read_bytes()) for relative, source in files]
    for target, data in entries:
        if matches(target, data) is False:
            raise RuntimeError(f"Loadout list pack file differs: {target}")
    for target, data in entries:
        target.parent.mkdir(parents=True, exist_ok=True)
        reject_redirect(target.parent)
        if matches(target, data) is True:
            continue
        fd, temporary = tempfile.mkstemp(prefix=".loadout-stage-", dir=target.parent)
        owned = os.fstat(fd)
        try:
            if os.name == "posix":
                # mkstemp starts at 0600; ordinary installed data files use
                # 0666 filtered through the process umask.
                mask = os.umask(0)
                os.umask(mask)
                os.fchmod(fd, 0o666 & ~mask)
            with os.fdopen(fd, "wb") as output:
                output.write(data)
                output.flush()
            try:
                # Hard link creates the destination atomically and never replaces it.
                os.link(temporary, target)
            except FileExistsError:
                if matches(target, data) is not True:
                    raise RuntimeError(f"Loadout list pack file differs: {target}")
        finally:
            # Remove only our verified temporary inode; uncertain paths are retained.
            try:
                current = os.lstat(temporary)
                if (stat.S_ISREG(current.st_mode) and current.st_dev == owned.st_dev
                        and current.st_ino == owned.st_ino):
                    os.unlink(temporary)
            except FileNotFoundError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--buildables", choices=("ON", "OFF"), required=True)
    args = parser.parse_args()
    # Match CMake's DESTDIR concatenation, including its Windows drive handling.
    prefix = os.environ.get("DESTDIR", "") + args.prefix
    root = Path(prefix) / "data/plugins/loadout_lists"
    names = ["items", "outfits"] + (["buildables"] if args.buildables == "ON" else [])
    files = [(f"lists/{name}.txt", Path(args.source) / f"{name}.txt") for name in names]
    files.append(("plugin.ini", args.manifest))
    try:
        stage(root, files)
    except (OSError, RuntimeError) as error:
        print(f"Loadout list staging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
