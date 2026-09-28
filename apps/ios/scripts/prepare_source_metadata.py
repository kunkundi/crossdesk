#!/usr/bin/env python3
"""Record the source corresponding to an iOS build without changing the checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from urllib.parse import quote

REPO = Path(__file__).resolve().parents[3]
REPOSITORY_URL = "https://github.com/kunkundi/crossdesk"
MINIRTC_URL = "https://github.com/kunkundi/minirtc"


def git(*args, root=REPO):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def source_metadata(require_release=False, tag=None):
    revision = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=normal", "--ignore-submodules=none"))
    tags = git("tag", "--points-at", "HEAD").splitlines()
    if tag:
        if tag not in tags:
            raise ValueError(f"Source tag {tag!r} does not point to the build commit")
    else:
        # 'latest' is a moving alias, never a corresponding-source identifier.
        versions = sorted(t for t in tags if t.startswith("v"))
        tag = versions[0] if versions else None
    if require_release and (dirty or not tag or not tag.startswith("v")):
        raise ValueError("Release source must be a clean checkout of a version tag, with matching submodules")
    mini_revision = git("rev-parse", "HEAD", root=REPO / "deps/submodules/minirtc")
    lock_path = REPO / "apps/ios/licenses/sources.json"
    lock = json.loads(lock_path.read_text())
    ref = quote(tag if tag and not dirty else revision, safe="")
    return {
        "schemaVersion": 1,
        "repositoryURL": REPOSITORY_URL,
        "revision": revision,
        "tag": tag,
        "isModified": dirty,
        "sourceURL": f"{REPOSITORY_URL}/tree/{ref}",
        "buildInstructionsURL": f"{REPOSITORY_URL}/blob/{ref}/apps/ios/README.md",
        "privacyPolicyURL": f"{REPOSITORY_URL}/blob/{ref}/PRIVACY.md",
        "miniRTCRevision": mini_revision,
        "miniRTCSourceURL": f"{MINIRTC_URL}/tree/{mini_revision}",
        "licenseCatalogSHA256": lock["bundle_sha256"],
        "sourceCatalogSHA256": hashlib.sha256(lock_path.read_bytes()).hexdigest(),
        "toolchain": lock["toolchain"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--tag", default=os.environ.get("CROSSDESK_SOURCE_TAG") or None)
    parser.add_argument("--require-release", action="store_true",
                        default=os.environ.get("CROSSDESK_RELEASE_BUILD") == "YES")
    args = parser.parse_args()
    metadata = source_metadata(args.require_release, args.tag)
    data = (json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != data:
        args.output.write_bytes(data)
    print(f"Recorded iOS source: {metadata['revision']} (modified: {metadata['isModified']})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"iOS source metadata: {error}", file=sys.stderr)
        sys.exit(1)
