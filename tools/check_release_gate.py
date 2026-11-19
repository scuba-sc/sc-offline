#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release job's gate: which SDK tag a product tag may be built from. Python 3 standard library only.

    python tools/check_release_gate.py --product-tag v0.8.0-rc.1 --sdk-tag sdk-v2.4.1-rc.1 --sdk-version 2.4.1
    python tools/check_release_gate.py ... --state "draft=false prerelease=true"

The rules (the product tag is the tag being released, the SDK tag is the one the repository pins, see
`tools/bump_sdk_pin.py --print-tag`):

  - A final product tag (v0.8.0) needs the final SDK tag sdk-v<version>, published as a full release:
    not a draft, not a pre-release.
  - A release-candidate product tag (it contains -rc.<number>, v0.8.0-rc.1) may pin either the final SDK tag
    (a full release, as above) or a candidate of that version, sdk-v<version>-rc.<number>, published as a
    pre-release: not a draft, and gh reports isPrerelease true.
  - Anything else is refused: another SDK version, another suffix, a draft, a candidate pinned by a final tag.

Without --state only the tag pairing is checked (before anything is fetched); with --state, the release state
as `gh release view <sdk tag> --json isDraft,isPrerelease --jq '"draft=\\(.isDraft) prerelease=\\(.isPrerelease)"'`
prints it is checked too. It prints `candidate` or `release` (the kind of the SDK tag) and exits 0, or prints
the reason on stderr and exits 1.
"""
from __future__ import annotations

import argparse
import re
import sys

PRODUCT_TAG = re.compile(r"^v\d+\.\d+\.\d+(?:\.\d+)?(?:-[0-9A-Za-z.]+)?$")
CANDIDATE = re.compile(r"-rc\.\d+$")
STATE = re.compile(r"^draft=(true|false) prerelease=(true|false)$")


class Refused(Exception):
    pass


def product_is_candidate(tag: str) -> bool:
    return bool(CANDIDATE.search(tag)) and "-rc." in tag


def sdk_kind(sdk_tag: str, sdk_version: str) -> str:
    """'release' for sdk-v<version>, 'candidate' for sdk-v<version>-rc.<n>."""
    base = "sdk-v" + sdk_version
    if sdk_tag == base:
        return "release"
    if sdk_tag.startswith(base + "-rc.") and re.fullmatch(r"\d+", sdk_tag[len(base) + 4:]):
        return "candidate"
    raise Refused(f"the pinned SDK tag {sdk_tag} is neither {base} nor a release candidate {base}-rc.<number> of it")


def check(product_tag: str, sdk_tag: str, sdk_version: str, state: str | None = None) -> str:
    if not PRODUCT_TAG.match(product_tag):
        raise Refused(f"the product tag {product_tag!r} is not v<number>.<number>.<number>[-suffix]")
    kind = sdk_kind(sdk_tag, sdk_version)
    candidate_product = product_is_candidate(product_tag)
    if kind == "candidate" and not candidate_product:
        raise Refused(f"the product tag {product_tag} is a final release but pins the SDK candidate {sdk_tag}. "
                      f"A final product release needs the final SDK tag sdk-v{sdk_version}, published as a full release. "
                      "Tag a release candidate (-rc.<number>) or publish the final SDK first.")
    if state is not None:
        found = STATE.match(state.strip())
        if not found:
            raise Refused(f"cannot read the release state {state!r} of {sdk_tag}")
        draft, pre = found.group(1) == "true", found.group(2) == "true"
        if draft:
            raise Refused(f"the {sdk_tag} release is a draft")
        if kind == "candidate" and not pre:
            raise Refused(f"the {sdk_tag} release is not marked as a pre-release (draft=false prerelease=false). "
                          "A candidate must be published as a pre-release: gh release edit "
                          f"{sdk_tag} -R scuba-sc/sco-core --prerelease")
        if kind == "release" and pre:
            raise Refused(f"the {sdk_tag} release is a pre-release, not a full release (draft=false prerelease=true)")
    return kind


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--product-tag", required=True)
    ap.add_argument("--sdk-tag", required=True)
    ap.add_argument("--sdk-version", required=True)
    ap.add_argument("--state", help='"draft=<bool> prerelease=<bool>" of the SDK release')
    args = ap.parse_args(argv)
    try:
        print(check(args.product_tag, args.sdk_tag, args.sdk_version, args.state))
    except Refused as error:
        print(f"Not packaging: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
