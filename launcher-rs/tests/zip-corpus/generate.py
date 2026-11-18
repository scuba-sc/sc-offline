#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the shared plugin zip corpus.

    python generate.py            write cases/, manifests/ and expected.tsv next to this script
    python generate.py --check    fail if the files on disk differ from what this script produces

The archives are built byte by byte with the standard library only (no zipfile writer), so the
hostile ones can break exactly one rule each. Payloads are inert test bytes. Output is
deterministic: fixed DOS timestamps, fixed deflate settings.

The expected verdicts are written here by hand. They are not copied from either verifier, so
the corpus can catch a bug that both tools share.

expected.tsv columns (tab separated, one row per case):

    kind         zip (archive, run by both verifiers) or ini (plugin.ini, run by the Rust parser
                 and the SDK checker --manifest-stdin)
    name         file name without extension, under cases/ (zip) or manifests/ (ini)
    verdict      ok or refuse
    code         for a refused zip: the Rust verifier error code. - otherwise
    min_python   oldest Python that has the behaviour (the Python tool is skipped below it). - for any
    description  one line
"""
import argparse
import struct
import sys
import unicodedata
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent

DOS_DATE = (0 << 9) | (1 << 5) | 1  # 1980-01-01
DOS_TIME = 0

S_IFDIR = 0o040000
S_IFREG = 0o100000
S_IFLNK = 0o120000
S_IFIFO = 0o010000
S_IFCHR = 0o020000
S_IFBLK = 0o060000
S_IFSOCK = 0o140000

MIB = 1024 * 1024


def deflate(data):
    """Raw deflate, level 9. Deterministic for a given zlib."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


class E:
    """One archive entry. Every field can be overridden to build a hostile archive."""

    def __init__(self, name, data=b"", *, method=0, stored=None, crc=None, csize=None, usize=None,
                 flags=0, utf8=None, raw_name=None, local_name=None, local_flags=None,
                 local_extra=b"", extra=b"", comment=b"", mode=None, dos=None, create_system=3,
                 extract_version=20, offset=None, pad_after=b"", is_dir=None):
        self.name = name
        self.data = data
        self.method = method
        self.is_dir = name.endswith("/") if is_dir is None else is_dir
        if raw_name is None:
            try:
                raw_name = name.encode("ascii")
                if utf8:
                    flags |= 0x0800
            except UnicodeEncodeError:
                raw_name = name.encode("utf-8")
                if utf8 is not False:
                    flags |= 0x0800
        self.raw_name = raw_name
        self.local_name = raw_name if local_name is None else local_name
        self.flags = flags
        self.local_flags = flags if local_flags is None else local_flags
        self.local_extra = local_extra
        self.extra = extra
        self.comment = comment
        if mode is None:
            mode = S_IFDIR | 0o755 if self.is_dir else S_IFREG | 0o644
        elif mode is False:
            mode = 0
        if dos is None:
            dos = 0x10 if self.is_dir else 0
        self.external_attr = ((mode & 0xFFFF) << 16) | dos
        self.create_system = create_system
        self.extract_version = extract_version
        if stored is None:
            stored = deflate(data) if method == 8 else data
        self.stored = stored
        self.crc = zlib.crc32(data) if crc is None else crc
        self.csize = len(stored) if csize is None else csize
        self.usize = len(data) if usize is None else usize
        self.offset = offset
        self.pad_after = pad_after


def build_zip(entries, *, comment=b"", prefix=b"", gap=b"", eocd=None, pre_eocd=b"", cd_tail=b""):
    """Assemble an archive. `eocd` overrides end-record fields: total, disk_entries, size,
    offset, disk, cd_disk, comment_len."""
    eocd = eocd or {}
    out = bytearray(prefix)
    offsets = []
    for e in entries:
        offsets.append(len(out))
        out += struct.pack("<4s5H3L2H", b"PK\x03\x04", e.extract_version, e.local_flags, e.method,
                           DOS_TIME, DOS_DATE, e.crc, e.csize, e.usize, len(e.local_name),
                           len(e.local_extra))
        out += e.local_name + e.local_extra + e.stored + e.pad_after
    out += gap
    # Offsets in the central directory are relative to the start of the archive proper. A prefix
    # is left unaccounted for on purpose: that is what "prepended data" is.
    cd_start = len(out)
    cd = bytearray()
    for e, off in zip(entries, offsets):
        rel = off - len(prefix)
        cd += struct.pack("<4s4B4H3L5H2L", b"PK\x01\x02", 63, e.create_system, e.extract_version,
                          0, e.flags, e.method, DOS_TIME, DOS_DATE, e.crc, e.csize, e.usize,
                          len(e.raw_name), len(e.extra), len(e.comment), 0, 0, e.external_attr,
                          rel if e.offset is None else e.offset)
        cd += e.raw_name + e.extra + e.comment
    cd += cd_tail
    out += cd
    out += pre_eocd
    total = eocd.get("total", len(entries))
    out += struct.pack("<4s4H2LH", b"PK\x05\x06", eocd.get("disk", 0), eocd.get("cd_disk", 0),
                       eocd.get("disk_entries", total), total, eocd.get("size", len(cd)),
                       eocd.get("offset", cd_start - len(prefix)),
                       eocd.get("comment_len", len(comment)))
    out += comment
    return bytes(out)


# ---- the plugin used by most cases ----------------------------------------------------------

def manifest(entry="pilot.dll", kind="native", extra="", ident="pilot", api="1.0", requires=None):
    text = f"id = {ident}\nname = Pilot\nversion = 1.0\napi = {api}\nkind = {kind}\n"
    if entry is not None:
        text += f"entry = {entry}\n"
    if requires:
        text += f"requires = {requires}\n"
    return (text + extra).encode("utf-8")


INI = manifest()
DLL = b"MZ inert test bytes, never executed\n"


def base(**kw):
    """Minimal good plugin: folder entry, manifest, native entry file."""
    return [E("pilot/"), E("pilot/plugin.ini", INI), E("pilot/pilot.dll", DLL)]


def files(*extra):
    """Good plugin plus extra entries."""
    return base() + list(extra)


def replace(entries, name, new):
    return [new if e.name == name else e for e in entries]


ZIP_CASES = []  # (name, bytes, verdict, code, min_python, description)
INI_CASES = []  # (name, bytes, verdict, description)


def ok(name, entries_or_bytes, desc, min_py="-", **kw):
    data = entries_or_bytes if isinstance(entries_or_bytes, bytes) else build_zip(entries_or_bytes, **kw)
    ZIP_CASES.append((name, data, "ok", "-", min_py, desc))


def bad(name, code, entries_or_bytes, desc, min_py="-", **kw):
    data = entries_or_bytes if isinstance(entries_or_bytes, bytes) else build_zip(entries_or_bytes, **kw)
    ZIP_CASES.append((name, data, "refuse", code, min_py, desc))


def ini(name, verdict, text, desc):
    INI_CASES.append((name, text if isinstance(text, bytes) else text.encode("utf-8"), verdict, desc))


# ---- good archives --------------------------------------------------------------------------

ok("good-minimal", base(), "folder entry, manifest, native entry, stored")
ok("good-no-directory-entries", [E("pilot/plugin.ini", INI), E("pilot/pilot.dll", DLL)],
   "parent folder is implicit")
ok("good-deflated", [E("pilot/plugin.ini", INI, method=8),
                     E("pilot/pilot.dll", b"abcdefgh" * 400, method=8)],
   "deflated members, moderate ratio")
ok("good-mixed-methods", files(E("pilot/data/a.txt", b"hello " * 200, method=8),
                               E("pilot/data/b.txt", b"stored text\n")),
   "stored and deflated members together")
ok("good-lua-plugin", [E("pilot/plugin.ini", manifest("main.lua", "lua")),
                       E("pilot/main.lua", b"-- inert\nreturn {}\n")], "kind lua")
ok("good-data-pack", [E("pilot/plugin.ini", manifest(None, "data")),
                      E("pilot/lists/places.txt", b"Lorville\n"),
                      E("pilot/missions/m1.txt", b"x\n")], "kind data, no entry")
ok("good-settings-block",
   [E("pilot/plugin.ini", manifest(extra=(
       "[settings]\nenabled = bool default true label \"Enabled\" help \"Turn it on\"\n"
       "count = int min 1 max 10 default 5\nratio = float min -1.5 max 1e2 default 0.25\n"
       "title = string default \"a ; b\"\nmode = enum(fast,slow) default slow\n"))),
    E("pilot/pilot.dll", DLL)], "[settings] with every type")
ok("good-requires-list",
   [E("pilot/plugin.ini", manifest(requires="sco.storage, teleport,spawn.ship")),
    E("pilot/pilot.dll", DLL)], "requires list")
ok("good-unknown-keys-and-sections",
   [E("pilot/plugin.ini", manifest(extra="future_key = 1\nfuture_key = 2\n[future]\nid = other\nwhatever\n")),
    E("pilot/pilot.dll", DLL)], "unknown keys skipped, unknown section skipped")
ok("good-bom-and-crlf-manifest",
   [E("pilot/plugin.ini", b"\xef\xbb\xbf; c\r\n" + INI.replace(b"\n", b"\r\n")),
    E("pilot/pilot.dll", DLL)], "UTF-8 BOM, CRLF line ends, comment")
ok("good-unicode-entry-utf8-flag",
   [E("pilot/plugin.ini", manifest("pilot-\u00e9.dll")), E("pilot/pilot-\u00e9.dll", DLL)],
   "entry name with a non-ASCII letter, UTF-8 flag set")
ok("good-cp437-entry-name",
   [E("pilot/plugin.ini", manifest("pilot-\u00e9.dll")),
    E("pilot/pilot-?.dll", DLL, raw_name=b"pilot/pilot-\x82.dll")],
   "name without the UTF-8 flag decodes as cp437 (byte 0x82 is e-acute)")
ok("good-empty-file", files(E("pilot/data/empty.txt", b"")), "zero-byte member")
ok("good-empty-file-deflate-stream",
   files(E("pilot/data/empty.txt", b"", method=8)), "zero-byte member stored as an empty deflate stream")
ok("good-empty-file-64-extra-bytes",
   files(E("pilot/data/empty.txt", b"", stored=b"J" * 64)),
   "zero-byte member with exactly 64 compressed bytes (the limit)")
ok("good-archive-comment", base(), "archive comment", comment=b"built by a test")
ok("good-deep-directories", files(E("pilot/a/b/c/d/e/f/g/h.txt", b"deep\n")), "nested directories")
ok("good-data-descriptor-flag", [E("pilot/plugin.ini", INI, flags=0x0008),
                                 E("pilot/pilot.dll", DLL, flags=0x0008)],
   "flag bit 3 set, sizes known")
ok("good-unknown-extra-field", [E("pilot/plugin.ini", INI, extra=b"\xfe\xca\x04\x00abcd"),
                                E("pilot/pilot.dll", DLL)], "unknown extra field is ignored")
ok("good-extra-7075-same-name",
   [E("pilot/plugin.ini", INI,
      extra=b"\x75\x70" + struct.pack("<H", 5 + len(b"pilot/plugin.ini")) + b"\x01" +
      struct.pack("<L", zlib.crc32(b"pilot/plugin.ini")) + b"pilot/plugin.ini"),
    E("pilot/pilot.dll", DLL)], "unicode path extra field that repeats the name")
ok("good-extra-7075-wrong-crc",
   [E("pilot/plugin.ini", INI,
      extra=b"\x75\x70" + struct.pack("<H", 5 + 3) + b"\x01" + struct.pack("<L", 12345) + b"zzz"),
    E("pilot/pilot.dll", DLL)], "unicode path extra field with a stale CRC is ignored")
ok("good-dos-attributes", [E("pilot/plugin.ini", INI, create_system=0, mode=False, dos=0x20),
                           E("pilot/pilot.dll", DLL, create_system=0, mode=False, dos=0x20)],
   "create system DOS, archive bit")
ok("good-gap-before-central-directory", base(), "junk between the last member and the central directory",
   gap=b"\x00" * 7)
ok("good-entry-case-exact",
   [E("pilot/plugin.ini", manifest("Pilot.DLL")), E("pilot/Pilot.DLL", DLL)], "entry matches case exactly")
ok("good-latin-names", files(E("pilot/data/\u00e9.txt", b"1"), E("pilot/data/\u00f6.txt", b"2")),
   "distinct non-ASCII names")
ok("good-dots-in-name", files(E("pilot/data/a.b.c", b"1")), "dots inside a name")

ok("good-max-manifest-size", [E("pilot/plugin.ini", (INI + b"; pad\n" * 4000)[:16384 - 1] + b"\n"),
                              E("pilot/pilot.dll", DLL)], "plugin.ini of exactly 16384 bytes")

SEG255 = "s" * 255
ok("good-max-segment-255", files(E(f"pilot/{SEG255}", b"1")), "one 255-byte path segment")
LONG = "pilot/" + "/".join(["d" * 200] * 4) + "/" + "f" * 100  # 6 + 4*200 + 4 + 100 = 910
LONG = LONG + "x" * (1024 - len(LONG))
assert len(LONG.encode()) == 1024
ok("good-max-path-1024", files(E(LONG, b"1")), "path of exactly 1024 bytes")

# 4096 entries is the limit.
many = [E("pilot/plugin.ini", INI), E("pilot/pilot.dll", DLL)]
many += [E(f"pilot/d/{i:04}", b"") for i in range(4096 - 2)]
ok("good-4096-entries", many, "exactly 4096 entries")
many.append(E("pilot/d/over", b""))
bad("bad-4097-entries", "too-many-entries", many, "4097 entries")


# Collision work budget: 4 Mi aggregate prefix characters. Same cost function as the tool.
def prefix_cost(path):
    parts = path.rstrip("/").split("/")
    o = c = k = tot = 0
    for i, part in enumerate(parts):
        sep = 1 if i else 0
        o += len(part) + sep
        c += len(unicodedata.normalize("NFC", part).casefold()) + sep
        k += len(unicodedata.normalize("NFKC", part).casefold()) + sep
        tot += o + c + k
    return tot


DEEP = "a/" * 480
BUDGET = 4 * 1024 * 1024
spent = sum(prefix_cost(e.name) for e in base())
under = []
n = 0
while True:
    name = f"pilot/{n:03}/{DEEP}f{n:03}.txt"
    cost = prefix_cost(name)
    if spent + cost > BUDGET:
        over_name = name
        break
    spent += cost
    under.append(E(name, b""))
    n += 1
ok("good-collision-budget-just-under", files(*under), f"{n} deep paths, under the 4 Mi prefix budget")
bad("bad-collision-budget-over", "collision-budget", files(*under, E(over_name, b"")),
    f"{n + 1} deep paths, over the 4 Mi prefix budget")

# ---- hostile: archive structure -------------------------------------------------------------

good = build_zip(base())
bad("bad-empty-file", "archive-empty", b"", "zero-byte file")
bad("bad-too-short", "too-short", b"PK\x05\x06" + b"\x00" * 6, "shorter than an end record")
bad("bad-random-bytes", "no-end-record", bytes((i * 37 + 11) % 251 for i in range(300)), "no ZIP structure")
bad("bad-eocd-comment-length-lies", "no-end-record", good[:-2] + struct.pack("<H", 5),
    "end record says there is a comment, none follows")
bad("bad-trailing-bytes-after-eocd", "no-end-record", good + b"abc", "bytes after the end record")
bad("bad-central-directory-size-mismatch", "prepended-data", base(), "end record size is wrong",
    eocd={"size": 5000})
bad("bad-central-directory-offset-wrong", "prepended-data", base(), "end record offset is wrong",
    eocd={"offset": 3})
bad("bad-prepended-data", "prepended-data", base(), "bytes before the first member", prefix=b"X" * 16)
bad("bad-multi-disk-number", "multi-disk", base(), "disk number 1", eocd={"disk": 1})
bad("bad-multi-disk-cd-disk", "multi-disk", base(), "central directory disk 1", eocd={"cd_disk": 1})
bad("bad-multi-disk-entry-counts", "multi-disk", base(), "entries on this disk differ from total",
    eocd={"disk_entries": 2})
bad("bad-zip64-entry-count-marker", "zip64", base(), "0xFFFF entry count",
    eocd={"total": 0xFFFF, "disk_entries": 0xFFFF})
bad("bad-zip64-offset-marker", "zip64", base(), "0xFFFFFFFF central directory offset",
    eocd={"offset": 0xFFFFFFFF})
bad("bad-zip64-size-marker", "zip64", base(), "0xFFFFFFFF central directory size",
    eocd={"size": 0xFFFFFFFF})
bad("bad-zip64-member-compressed-size", "zip64", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, csize=0xFFFFFFFF)), "member compressed size 0xFFFFFFFF")
bad("bad-zip64-member-size", "zip64", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, usize=0xFFFFFFFF)), "member size 0xFFFFFFFF")
bad("bad-zip64-member-offset", "zip64", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, offset=0xFFFFFFFF)), "member offset 0xFFFFFFFF")
bad("bad-zip64-locator-and-record", "prepended-data", base(),
    "ZIP64 locator and record between the central directory and the end record",
    pre_eocd=(b"PK\x06\x06" + struct.pack("<QHHLLQQQQ", 44, 45, 45, 0, 0, 3, 3, 0, 0) +
              b"PK\x06\x07" + struct.pack("<LQL", 0, 0, 1)))
bad("bad-central-directory-too-large", "central-directory-too-large", base(),
    "declared central directory over 8 MiB", eocd={"size": 8 * MIB + 1})
bad("bad-central-directory-bad-signature", "central-directory-record",
    good.replace(b"PK\x01\x02", b"PK\x01\x0f", 1), "first central record has a wrong signature")
bad("bad-central-directory-name-overruns", "central-directory-truncated", base(),
    "last central record claims more name bytes than exist", cd_tail=b"",
    eocd=None) if False else None
# A record that claims a longer name than the directory holds.
_cd_overrun = bytearray(good)
_marker = _cd_overrun.rfind(b"PK\x01\x02")
struct.pack_into("<H", _cd_overrun, _marker + 28, 400)
bad("bad-central-directory-name-overruns", "central-directory-truncated", bytes(_cd_overrun),
    "last central record claims more name bytes than the directory holds")
bad("bad-central-directory-trailing-junk", "central-directory-truncated", base(),
    "junk after the last central record, inside the declared size", cd_tail=b"J" * 10)
bad("bad-entry-count-too-small", "entry-count-mismatch", base(), "end record counts 2, directory has 3",
    eocd={"total": 2, "disk_entries": 2})
bad("bad-entry-count-too-large", "entry-count-mismatch", base(), "end record counts 4, directory has 3",
    eocd={"total": 4, "disk_entries": 4})
bad("bad-eocd-signature-inside-comment", "end-record-ambiguous", base(),
    "comment holds an end record signature that Python would pick", comment=b"xxPK\x05\x06yy")

# ---- hostile: local headers and offsets -----------------------------------------------------

bad("bad-local-offset-beyond-directory", "local-header-offset",
    replace(base(), "pilot/pilot.dll", E("pilot/pilot.dll", DLL, offset=0x7FFF0000)),
    "member offset beyond the central directory")
bad("bad-local-offset-shared", "shared-local-header",
    replace(base(), "pilot/pilot.dll", E("pilot/pilot.dll", DLL, offset=len(b"") + 0)),
    "two members use the first local header")
_sig = bytearray(good)
_sig[good.find(b"PK\x03\x04", 4):][:4] = b"PK\x03\x0f" if False else b"PK\x03\x04"
_second = good.find(b"PK\x03\x04", 4)
_bad_sig = good[:_second] + b"PK\x03\x0f" + good[_second + 4:]
bad("bad-local-header-signature", "local-header", _bad_sig, "a local header has a wrong signature")
bad("bad-local-name-mismatch", "local-header",
    replace(base(), "pilot/pilot.dll", E("pilot/pilot.dll", DLL, local_name=b"pilot/other.dll")),
    "local name differs from the central name")
bad("bad-local-name-flag-mismatch", "local-header",
    [E("pilot/plugin.ini", manifest("pilot-\u00e9.dll")),
     E("pilot/pilot-\u00e9.dll", DLL, local_flags=0)],
    "central says UTF-8, local does not")
bad("bad-local-directory-name-mismatch", "local-header",
    [E("pilot/", local_name=b"../xx/"), E("pilot/plugin.ini", INI), E("pilot/pilot.dll", DLL)],
    "local name of a directory entry differs")
bad("bad-overlapping-entry", "local-header",
    replace(base(), "pilot/plugin.ini", E("pilot/plugin.ini", INI, csize=len(INI) + 40)),
    "compressed size runs into the next local header", min_py="3.14")

# ---- hostile: flags, methods, data ----------------------------------------------------------

for label, flag in (("bit0", 0x0001), ("bit6", 0x0040), ("bit13", 0x2000)):
    bad(f"bad-encrypted-flag-{label}", "encrypted", replace(base(), "pilot/pilot.dll",
        E("pilot/pilot.dll", DLL, flags=flag)), f"general purpose flag {label}")
bad("bad-patched-data-flag-bit5", "local-header", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, flags=0x0020)), "flag bit 5 (compressed patched data)")
for label, method in (("shrink", 1), ("bzip2", 12), ("lzma", 14), ("aes", 99)):
    bad(f"bad-method-{label}", "compression", replace(base(), "pilot/pilot.dll",
        E("pilot/pilot.dll", DLL, method=method, stored=DLL)), f"method {method}")
bad("bad-extract-version-64", "extract-version", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, extract_version=64)), "version needed 6.4 is beyond what zipfile accepts")
bad("bad-crc-stored", "member-read", replace(base(), "pilot/pilot.dll", E("pilot/pilot.dll", DLL, crc=1)),
    "wrong CRC, stored")
bad("bad-crc-deflated", "member-read", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL * 10, method=8, crc=1)), "wrong CRC, deflated")
bad("bad-crc-empty-file", "member-read", files(E("pilot/data/e.txt", b"", crc=5)), "empty member, CRC not 0")
bad("bad-deflate-garbage", "member-read", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, method=8, stored=b"\xff\xff\xff\xff\xff\xff\xff\xff",
      crc=zlib.crc32(DLL), usize=len(DLL))), "bytes that are not a deflate stream")
_whole = deflate(DLL * 20)
bad("bad-deflate-truncated", "member-read", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL * 20, method=8, stored=_whole[:len(_whole) // 2])), "deflate stream cut short")
bad("bad-size-larger-than-stream", "member-read", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, method=8, usize=len(DLL) + 10)), "declared size is larger than the data")
bad("bad-stored-shorter-than-size", "member-read", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, csize=len(DLL) - 6)), "stored size is smaller than the declared size")
bad("bad-compression-ratio-bomb", "compression-ratio", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", b"0" * MIB, method=8)), "1 MiB of one byte, over 200:1")
bad("bad-zero-compressed-size", "compression-ratio", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, csize=0)), "size without compressed data")
bad("bad-empty-file-65-compressed-bytes", "empty-file-data",
    files(E("pilot/data/e.txt", b"", stored=b"J" * 65)), "empty member with 65 compressed bytes")
bad("bad-file-over-128-mib", "file-too-large", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, usize=128 * MIB + 1, csize=700000)), "declared size over 128 MiB")
bad("bad-total-over-512-mib", "total-too-large",
    files(*[E(f"pilot/data/{i}", DLL, usize=127 * MIB, csize=700000) for i in range(5)]),
    "declared sizes add up to over 512 MiB")
bad("bad-directory-with-size", "directory-not-empty", [E("pilot/", usize=1, csize=1, stored=b"x"),
    E("pilot/plugin.ini", INI), E("pilot/pilot.dll", DLL)], "directory entry with data")
bad("bad-directory-with-crc", "directory-not-empty", [E("pilot/", crc=7), E("pilot/plugin.ini", INI),
    E("pilot/pilot.dll", DLL)], "directory entry with a CRC")

# ---- hostile: plugin folder and manifest ----------------------------------------------------

bad("bad-manifest-missing", "manifest-missing", [E("pilot/pilot.dll", DLL)], "no plugin.ini")
bad("bad-manifest-is-directory", "manifest-missing", [E("pilot/plugin.ini/"), E("pilot/pilot.dll", DLL)],
    "plugin.ini is a directory")
bad("bad-manifest-over-16-kib", "manifest-too-large", [E("pilot/plugin.ini", INI + b";" * (16385 - len(INI))),
    E("pilot/pilot.dll", DLL)], "plugin.ini of 16385 bytes")
bad("bad-two-root-folders", "not-one-plugin-folder", files(E("extra/readme.txt", b"x")), "two top-level folders")
bad("bad-file-at-root", "not-one-plugin-folder", files(E("loose.txt", b"x")), "a file next to the plugin folder")
bad("bad-root-is-a-file", "root-not-folder", [E("pilot", b"x")], "the only entry is a file at the root")
bad("bad-root-file-and-folder", "file-used-as-directory", [E("pilot", b"file")] + base()[1:],
    "a file named like the plugin folder")
bad("bad-entry-missing", "entry-missing", [E("pilot/plugin.ini", INI), E("pilot/other.dll", DLL)],
    "declared entry is not in the archive")
bad("bad-entry-is-directory", "entry-missing", [E("pilot/plugin.ini", INI), E("pilot/pilot.dll/")],
    "declared entry is a directory")
bad("bad-entry-case-differs", "entry-missing", [E("pilot/plugin.ini", manifest("PILOT.DLL")), E("pilot/pilot.dll", DLL)],
    "declared entry differs in case")
bad("bad-manifest-id-differs-from-folder", "manifest-id-mismatch",
    [E("pilot/plugin.ini", manifest(ident="other")), E("pilot/pilot.dll", DLL)], "id does not match the folder")
bad("bad-manifest-id-reserved", "manifest-invalid", [E("sco/plugin.ini", manifest(ident="sco")),
    E("sco/pilot.dll", DLL)], "reserved id")
bad("bad-manifest-syntax", "manifest-invalid", [E("pilot/plugin.ini", INI + b"nonsense line\n"),
    E("pilot/pilot.dll", DLL)], "line without =")
bad("bad-manifest-entry-not-utf8", "manifest-entry-encoding",
    [E("pilot/plugin.ini", manifest().replace(b"pilot.dll", b"pilot\xff.dll")), E("pilot/pilot.dll", DLL)],
    "entry name holds a byte that is not UTF-8")
bad("bad-manifest-bad-setting", "manifest-invalid",
    [E("pilot/plugin.ini", manifest(extra="[settings]\nenabled = bool default maybe\n")),
     E("pilot/pilot.dll", DLL)], "setting default is not a bool")
bad("bad-manifest-data-pack-with-entry", "manifest-invalid",
    [E("pilot/plugin.ini", manifest("a.txt", "data")), E("pilot/a.txt", b"x")], "data pack names an entry")

# ---- hostile: paths -------------------------------------------------------------------------

def path_case(name, path, desc, code="unsafe-path", **kw):
    bad(name, code, files(E(path, b"x", **kw)), desc)


path_case("bad-path-absolute", "/pilot/escape", "leading slash")
path_case("bad-path-dotdot", "pilot/../escape", "parent segment")
path_case("bad-path-dot", "pilot/./x", "dot segment")
path_case("bad-path-empty-segment", "pilot//x", "empty segment")
path_case("bad-path-drive", "C:/escape", "drive letter")
path_case("bad-path-stream", "pilot/a.dll:stream", "alternate data stream")
path_case("bad-path-trailing-dot", "pilot/a.", "trailing dot")
path_case("bad-path-trailing-space", "pilot/a ", "trailing space")
for label, ch in (("question", "?"), ("star", "*"), ("quote", '"'), ("less", "<"), ("greater", ">"), ("pipe", "|")):
    path_case(f"bad-path-char-{label}", f"pilot/bad{ch}.txt", f"{ch!r} in a name")
for label, nm in (("con", "CON"), ("con-ext", "con.txt"), ("prn", "PRN"), ("aux", "AUX"), ("nul-ext", "nul.txt"),
                  ("com1", "COM1"), ("lpt9", "LPT9.log"), ("com-sup1", "COM\u00b9"), ("lpt-sup3", "LPT\u00b3.x"),
                  ("conin", "CONIN$.txt"), ("conout", "CONOUT$"), ("dotless-i", "CON\u0131N$")):
    path_case(f"bad-path-device-{label}", f"pilot/{nm}", f"reserved device name {nm!r}")
path_case("bad-path-control-01", "pilot/a\x01b", "control character U+0001")
path_case("bad-path-control-del", "pilot/a\x7fb", "DEL")
path_case("bad-path-control-c1", "pilot/a\u0085b", "C1 control U+0085")
path_case("bad-path-format-zwsp", "pilot/a\u200bb", "zero width space")
path_case("bad-path-format-rlo", "pilot/a\u202eb", "right-to-left override")
path_case("bad-path-format-bom", "pilot/a\ufeffb", "byte order mark inside a name")
path_case("bad-path-format-soft-hyphen", "pilot/a\u00adb", "soft hyphen")
path_case("bad-path-too-long", "pilot/" + "d" * 100 + "/" + "/".join(["d" * 200] * 5) + "/" + "f" * 100,
          "path over 1024 bytes")
path_case("bad-path-segment-too-long", "pilot/" + "s" * 256, "segment over 255 bytes")
bad("bad-path-backslash", "unsafe-path", files(E("pilot\\escape", b"x", raw_name=b"pilot\\escape")), "backslash")
bad("bad-path-nul", "nul-in-name", files(E("pilot/asset?txt", b"x", raw_name=b"pilot/asset\x00txt")),
    "NUL inside a name")
bad("bad-path-invalid-utf8", "name-encoding", files(E("pilot/x", b"x", raw_name=b"pilot/\xff\xfe", flags=0x0800)),
    "UTF-8 flag set, name is not UTF-8")
bad("bad-path-slash-only", "unsafe-path", files(E("/", b"", is_dir=True)), "a bare slash")
bad("bad-path-double-slash-directory", "unsafe-path", files(E("pilot//", b"")), "directory name with empty segment")
bad("bad-mode-dir-on-file-name", "mode-mismatch", files(E("pilot/x", b"x", mode=S_IFDIR | 0o755)),
    "directory mode on a file name")
bad("bad-mode-file-on-dir-name", "mode-mismatch", files(E("pilot/x/", mode=S_IFREG | 0o644)),
    "file mode on a directory name")

# ---- hostile: duplicates and collisions -----------------------------------------------------

bad("bad-duplicate-file", "duplicate-path", files(E("pilot/a.txt", b"1"), E("pilot/a.txt", b"2")), "same path twice")
bad("bad-duplicate-directory", "duplicate-path", files(E("pilot/d/"), E("pilot/d/")), "same directory twice")
bad("bad-file-and-directory", "file-and-directory", files(E("pilot/a", b"1"), E("pilot/a/")), "file and directory, same path")
bad("bad-file-as-parent", "file-used-as-directory", files(E("pilot/a", b"1"), E("pilot/a/b", b"2")), "file is a parent")
bad("bad-collision-case", "path-collision", files(E("pilot/Data.txt", b"1"), E("pilot/data.txt", b"2")), "case only")
bad("bad-collision-case-directory-prefix", "path-collision", files(E("pilot/Dir/a", b"1"), E("pilot/dir/b", b"2")),
    "case only, in a directory prefix")
bad("bad-collision-case-root", "path-collision", [E("pilot/plugin.ini", INI), E("Pilot/x", b"1")], "case only, in the root")
bad("bad-collision-nfd", "path-collision", files(E("pilot/\u00e9.txt", b"1"), E("pilot/e\u0301.txt", b"2")),
    "composed and decomposed e-acute")
bad("bad-collision-nfkc-ligature", "path-collision", files(E("pilot/\ufb01.txt", b"1"), E("pilot/fi.txt", b"2")),
    "fi ligature and fi")
bad("bad-collision-nfkc-fullwidth", "path-collision", files(E("pilot/\uff21.txt", b"1"), E("pilot/a.txt", b"2")),
    "fullwidth A and a")
bad("bad-collision-sharp-s", "path-collision", files(E("pilot/stra\u00dfe", b"1"), E("pilot/STRASSE", b"2")),
    "sharp s and SS")
bad("bad-collision-kelvin", "path-collision", files(E("pilot/\u212a.txt", b"1"), E("pilot/k.txt", b"2")),
    "Kelvin sign and k")
bad("bad-collision-final-sigma", "path-collision", files(E("pilot/\u03c3", b"1"), E("pilot/\u03c2", b"2")),
    "sigma and final sigma")

# ---- hostile: entry types -------------------------------------------------------------------

bad("bad-symlink", "symlink", files(E("pilot/link", b"pilot.dll", mode=S_IFLNK | 0o777)), "symbolic link")
for label, kind in (("fifo", S_IFIFO), ("chardev", S_IFCHR), ("blockdev", S_IFBLK), ("socket", S_IFSOCK)):
    bad(f"bad-special-{label}", "special-entry", files(E("pilot/dev", b"", mode=kind | 0o644)), label)
bad("bad-dos-reparse-point", "dos-attributes", files(E("pilot/r", b"x", create_system=0, mode=False, dos=0x400)),
    "DOS reparse point attribute")
bad("bad-dos-directory-flag-on-file", "dos-attributes", files(E("pilot/r", b"x", create_system=0, mode=False, dos=0x10)),
    "DOS directory attribute on a file name")

# ---- hostile: extra fields ------------------------------------------------------------------

bad("bad-extra-truncated", "corrupt-extra", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, extra=b"\xfe\xca\x10\x00ab")), "extra field longer than its data")
_name = b"pilot/pilot.dll"
bad("bad-extra-7075-renames", "unicode-path-extra", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, extra=b"\x75\x70" + struct.pack("<H", 5 + len(b"pilot/other.dll")) + b"\x01" +
      struct.pack("<L", zlib.crc32(_name)) + b"pilot/other.dll")), "unicode path field gives a different name",
    min_py="3.14")
bad("bad-extra-7075-corrupt", "unicode-path-extra", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, extra=b"\x75\x70\x03\x00abc")), "unicode path field shorter than 5 bytes", min_py="3.14")
bad("bad-extra-7075-invalid-utf8", "unicode-path-extra", replace(base(), "pilot/pilot.dll",
    E("pilot/pilot.dll", DLL, extra=b"\x75\x70" + struct.pack("<H", 5 + 2) + b"\x01" +
      struct.pack("<L", zlib.crc32(_name)) + b"\xff\xfe")), "unicode path field is not UTF-8", min_py="3.14")


# ---- plugin.ini cases (parser level) --------------------------------------------------------

B = "id = pilot\nname = Pilot\nversion = 1.0\napi = 1.0\nkind = native\nentry = pilot.dll\n"
ini("ok-minimal", "ok", B, "smallest native manifest")
ini("ok-comments-bom-crlf", "ok", b"\xef\xbb\xbf; c\r\n# d\r\n" + B.replace("\n", "\r\n").encode() + b"x = 1 ; t\r\n",
    "BOM, CRLF, both comment marks")
ini("ok-unknown-keys-twice", "ok", B + "future = 1\nfuture = 2\n", "unknown key twice")
ini("ok-unknown-section", "ok", B + "[future]\nid = other\nanything\n", "unknown section skipped")
ini("ok-comment-needs-space", "ok", B.replace("version = 1.0", "version = 1#b"), "# inside a word is text")
ini("ok-author", "ok", B + "author = A B ; c\n", "author key")
ini("ok-lua", "ok", B.replace("native", "lua").replace("pilot.dll", "main.lua"), "kind lua")
ini("ok-data", "ok", "id = pilot\nname = P\nversion = 1\napi = 1.0\nkind = data\n", "kind data")
ini("ok-api-max", "ok", B.replace("api = 1.0", "api = 65535.65535"), "api at the limit")
ini("ok-api-leading-zeros", "ok", B.replace("api = 1.0", "api = 00001.00000"), "api with leading zeros")
ini("ok-id-31", "ok", B.replace("id = pilot", "id = " + "a" * 31), "31 character id (not a match for the folder, see below)")
ini("ok-entry-63", "ok", B.replace("pilot.dll", "e" * 63), "63 byte entry")
ini("ok-entry-del", "ok", B.replace("pilot.dll", "pi\x7flot"), "DEL allowed in entry")
ini("ok-entry-high-bytes", "ok", B.replace("pilot.dll", "pi\xe9lot"), "UTF-8 entry")
ini("ok-requires", "ok", B + "requires = sco.storage, teleport ,spawn.ship\n", "requires list")
ini("ok-requires-16", "ok", B + "requires = " + ",".join(f"r{i}" for i in range(16)) + "\n", "16 requires")
ini("ok-settings-all-types", "ok", B + "[settings]\nenabled = bool default true label \"E\" help \"H\"\n"
    "count = int min 1 max 10 default 5\nratio = float min -1.5 max 1e2 default 0.25\n"
    "title = string default \"x ; y # z\"\nmode = enum(fast,slow) default slow\n", "every setting type")
ini("ok-settings-comments", "ok", B + "[settings]\na = bool ; c\n# whole line\nb = string default \"x # y\" # real\n",
    "comments inside [settings]")
ini("ok-settings-32", "ok", B + "[settings]\n" + "".join(f"s{i} = bool\n" for i in range(32)), "32 settings")
ini("ok-settings-int-edges", "ok", B + "[settings]\na = int default 9223372036854775807\n"
    "b = int default -9223372036854775808\nc = int default +0000000000000000007\n", "int64 edges")
ini("ok-settings-float-forms", "ok", B + "[settings]\na = float default 1\nb = float default -1.5e+3\nc = float default 0.0E0\n",
    "float forms")
ini("ok-settings-enum-16", "ok", B + "[settings]\na = enum(" + ",".join(f"c{i}" for i in range(16)) + ")\n",
    "16 enum choices")
ini("ok-settings-string-255", "ok", B + "[settings]\na = string default \"" + "x" * 255 + "\"\n", "255 byte string")
ini("ok-settings-label-help-max", "ok", B + "[settings]\na = bool label \"" + "x" * 63 + "\" help \"" + "y" * 255 + "\"\n",
    "label 63, help 255")
ini("ok-settings-enum-space", "ok", B + "[settings]\na = enum( a , b ) default b\n", "spaces inside enum list")
ini("ok-settings-key-spacing", "ok", B + "[settings]\n  a   =   bool   default   true  \n", "extra spaces")
ini("ok-section-spaces", "ok", B + "[ settings ]\na = bool\n", "spaces inside [ settings ]")
ini("ok-max-size", "ok", (B + "; pad\n" * 3000).encode()[:16384 - 1] + b"\n", "16384 bytes")

ini("bad-missing-id", "refuse", B.replace("id = pilot\n", ""), "id missing")
ini("bad-missing-name", "refuse", B.replace("name = Pilot\n", ""), "name missing")
ini("bad-missing-version", "refuse", B.replace("version = 1.0\n", ""), "version missing")
ini("bad-missing-api", "refuse", B.replace("api = 1.0\n", ""), "api missing")
ini("bad-missing-kind", "refuse", B.replace("kind = native\n", ""), "kind missing")
ini("bad-missing-entry", "refuse", B.replace("entry = pilot.dll\n", ""), "entry missing for native")
ini("bad-duplicate-key", "refuse", B + "name = Other\n", "name twice")
ini("bad-duplicate-key-spaced", "refuse", B + "  id   =  pilot\n", "id twice")
ini("bad-identity-after-other-section", "refuse", "[other]\n" + B, "keys after a section are not identity")
ini("bad-identity-after-settings", "refuse", "[settings]\n" + B, "keys in [settings] are settings")
ini("bad-no-equals", "refuse", B + "bare\n", "line without =")
ini("bad-section-unclosed", "refuse", B + "[settings\n", "[section without ]")
ini("bad-section-bracket-only", "refuse", B + "[\n", "lone [")
ini("bad-settings-twice", "refuse", B + "[settings]\n[settings]\n", "two [settings]")
ini("bad-id-empty", "refuse", B.replace("id = pilot", "id ="), "empty id")
ini("bad-id-upper", "refuse", B.replace("id = pilot", "id = Pilot"), "upper case id")
ini("bad-id-dash", "refuse", B.replace("id = pilot", "id = pi-lot"), "dash in id")
ini("bad-id-32", "refuse", B.replace("id = pilot", "id = " + "a" * 32), "32 character id")
for rid in ("sco", "host", "menu", "game"):
    ini(f"bad-id-reserved-{rid}", "refuse", B.replace("id = pilot", f"id = {rid}"), f"reserved id {rid}")
ini("bad-name-64", "refuse", B.replace("name = Pilot", "name = " + "n" * 64), "name over 63")
ini("bad-name-control", "refuse", B.replace("name = Pilot", "name = Pi\x01lot"), "control char in name")
ini("bad-name-del", "refuse", B.replace("name = Pilot", "name = Pi\x7flot"), "DEL in name")
ini("bad-version-32", "refuse", B.replace("version = 1.0", "version = " + "v" * 32), "version over 31")
ini("bad-author-64", "refuse", B + "author = " + "a" * 64 + "\n", "author over 63")
for label, api in (("one", "1"), ("trailing-dot", "1."), ("leading-dot", ".1"), ("letters", "a.b"),
                   ("three", "1.0.0"), ("negative", "1.-1"), ("over", "1.65536"), ("six-digits", "123456.0"),
                   ("empty", "")):
    ini(f"bad-api-{label}", "refuse", B.replace("api = 1.0", f"api = {api}"), f"api {api!r}")
ini("bad-kind-case", "refuse", B.replace("kind = native", "kind = Native"), "kind is case sensitive")
ini("bad-kind-unknown", "refuse", B.replace("kind = native", "kind = exe"), "unknown kind")
ini("bad-data-with-entry", "refuse", "id = pilot\nname = P\nversion = 1\napi = 1.0\nkind = data\nentry = a\n",
    "data pack with entry")
ini("bad-data-with-settings", "refuse", "id = pilot\nname = P\nversion = 1\napi = 1.0\nkind = data\n[settings]\na = bool\n",
    "data pack with settings")
for label, ent in (("empty", ""), ("dot", "."), ("dotdot", ".."), ("slash", "a/b"), ("backslash", "a\\b"),
                   ("colon", "C:x"), ("star", "a*"), ("question", "a?"), ("quote", 'a"'), ("less", "a<"),
                   ("greater", "a>"), ("pipe", "a|"), ("64", "e" * 64), ("control", "a\x01b")):
    ini(f"bad-entry-{label}", "refuse", B.replace("pilot.dll", ent), f"entry {ent!r}")
for label, req in (("empty", ""), ("upper", "Teleport"), ("leading-dot", ".a"), ("trailing-dot", "a."),
                   ("double-dot", "a..b"), ("empty-item", "a,,b"), ("trailing-comma", "a,"),
                   ("leading-comma", ",a"), ("space-inside", "a b"), ("duplicate", "a,a"), ("64", "r" * 64)):
    ini(f"bad-requires-{label}", "refuse", B + f"requires = {req}\n", f"requires {req!r}")
ini("bad-requires-17", "refuse", B + "requires = " + ",".join(f"r{i}" for i in range(17)) + "\n", "17 requires")
ini("bad-too-big", "refuse", (B + "; pad\n" * 3000).encode()[:16384] + b";", "16385 bytes")
ini("bad-nul-in-author", "refuse", B.encode() + b"author = a\x00b\n", "NUL byte in a value")

SB = B + "[settings]\n"
for label, body in (
        ("no-equals", "noequals\n"), ("upper-name", "Bad = bool\n"), ("empty-name", "= bool\n"),
        ("no-type", "a =\n"), ("unknown-type", "a = quux\n"), ("quoted-type", 'a = "bool"\n'),
        ("declared-twice", "a = bool\na = bool\n"), ("bool-default", "a = bool default maybe\n"),
        ("default-no-value", "a = bool default\n"), ("default-twice", "a = bool default true default true\n"),
        ("unknown-field", "a = bool wibble 1\n"), ("min-on-bool", "a = bool min 1\n"),
        ("int-default-float", "a = int default 1.5\n"), ("int-default-big", "a = int default 99999999999999999999\n"),
        ("int-min-above-max", "a = int min 5 max 1\n"), ("int-default-above-max", "a = int min 1 max 3 default 4\n"),
        ("int-default-below-min", "a = int min 1 max 3 default 0\n"), ("int-min-text", "a = int min x\n"),
        ("int-overflow", "a = int default 9223372036854775808\n"),
        ("int-underflow", "a = int default -9223372036854775809\n"),
        ("int-20-digits", "a = int default 00000000000000000007\n"),
        ("float-nan", "a = float default nan\n"), ("float-inf", "a = float default inf\n"),
        ("float-hex", "a = float default 0x10\n"), ("float-leading-dot", "a = float default .5\n"),
        ("float-trailing-dot", "a = float default 5.\n"), ("float-overflow", "a = float default 1e999\n"),
        ("float-min-above-max", "a = float min 2 max 1\n"),
        ("string-unterminated", 'a = string default "unterminated\n'), ("string-stray-quote", 'a = string default x"y\n'),
        ("string-text-after-quote", 'a = string default "x"y\n'),
        ("string-256", 'a = string default "' + "x" * 256 + '"\n'),
        ("label-empty", 'a = bool label ""\n'), ("help-empty", 'a = bool help ""\n'),
        ("label-64", 'a = bool label "' + "x" * 64 + '"\n'), ("help-256", 'a = bool help "' + "y" * 256 + '"\n'),
        ("enum-twice", "a = enum(a,a)\n"), ("enum-empty", "a = enum()\n"), ("enum-trailing-comma", "a = enum(a,)\n"),
        ("enum-upper", "a = enum(A)\n"), ("enum-unclosed", "a = enum(a\n"), ("enum-text-after", "a = enum(a)x\n"),
        ("enum-bad-default", "a = enum(a,b) default c\n"),
        ("enum-17", "a = enum(" + ",".join(f"c{i}" for i in range(17)) + ")\n"),
        ("33-settings", "".join(f"s{i} = bool\n" for i in range(33))),
        ("name-32", "n" * 32 + " = bool\n")):
    ini(f"bad-settings-{label}", "refuse", SB + body, f"[settings] {label}")


# ---- output ---------------------------------------------------------------------------------

def outputs():
    """Return {relative path: bytes}."""
    files_out = {}
    names = set()
    for name, data, *_ in ZIP_CASES:
        assert name not in names, name
        names.add(name)
        files_out[f"cases/{name}.zip"] = data
    names = set()
    for name, data, *_ in INI_CASES:
        assert name not in names, name
        names.add(name)
        files_out[f"manifests/{name}.ini"] = data
    rows = ["# kind\tname\tverdict\tcode\tmin_python\tdescription"]
    for name, _, verdict, code, min_py, desc in ZIP_CASES:
        rows.append(f"zip\t{name}\t{verdict}\t{code}\t{min_py}\t{desc}")
    for name, _, verdict, desc in INI_CASES:
        rows.append(f"ini\t{name}\t{verdict}\t-\t-\t{desc}")
    files_out["expected.tsv"] = ("\n".join(rows) + "\n").encode("utf-8")
    return files_out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="compare with the files on disk, write nothing")
    args = ap.parse_args()
    out = outputs()
    if args.check:
        problems = 0
        for rel, data in sorted(out.items()):
            path = HERE / rel
            if not path.is_file() or path.read_bytes() != data:
                print(f"differs or missing: {rel}", file=sys.stderr)
                problems += 1
        for sub in ("cases", "manifests"):
            for path in sorted((HERE / sub).glob("*")):
                if f"{sub}/{path.name}" not in out:
                    print(f"unexpected file: {sub}/{path.name}", file=sys.stderr)
                    problems += 1
        print("corpus is up to date" if not problems else f"{problems} problem(s)")
        return 1 if problems else 0
    for sub in ("cases", "manifests"):
        (HERE / sub).mkdir(exist_ok=True)
        for path in (HERE / sub).glob("*"):
            path.unlink()
    for rel, data in out.items():
        (HERE / rel).write_bytes(data)
    print(f"wrote {len(ZIP_CASES)} archives and {len(INI_CASES)} manifests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
