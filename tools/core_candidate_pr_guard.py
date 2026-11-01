#!/usr/bin/env python3
"""Accept only the reviewed core candidate PR while open or after its merge."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime
from typing import Any


def _valid_merge_time(value: Any) -> bool:
    if not isinstance(value, str) or re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", value) is None:
        return False
    try:
        datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return False
    return True


def _valid_commit_sha(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value) is not None


def accepted_state(payload: Any, expected_head: str, expected_base: str = "main") -> str | None:
    if not isinstance(payload, dict):
        return None
    head = payload.get("head")
    if not isinstance(head, dict) or head.get("sha") != expected_head:
        return None
    base = payload.get("base")
    if not isinstance(base, dict) or base.get("ref") != expected_base:
        return None

    state = payload.get("state")
    if state == "open" and payload.get("merged") is False:
        return "open"
    if (
        state == "closed"
        and payload.get("merged") is True
        and _valid_merge_time(payload.get("merged_at"))
        and _valid_commit_sha(payload.get("merge_commit_sha"))
    ):
        return "merged"
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-head", required=True)
    parser.add_argument("--expected-base", default="main")
    args = parser.parse_args()
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, UnicodeDecodeError):
        print("core candidate PR response was not valid JSON", file=sys.stderr)
        return 1

    result = accepted_state(payload, args.expected_head, args.expected_base)
    if result is None:
        print("core candidate PR is not open at, or merged from, the expected head", file=sys.stderr)
        return 1
    print(f"accepted {result} core candidate at {args.expected_head}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
