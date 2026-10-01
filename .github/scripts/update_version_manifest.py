#!/usr/bin/env python3
"""Merge a release's server downloads into the shared version.json."""

import argparse
import datetime
import json
from pathlib import Path
import re


DOWNLOAD_BASE = "https://downloads.crossdesk.cn/"
ARTIFACTS = {
    "windows-x64": ("win-x64", "exe"),
    "windows-x64-portable": ("win-x64-portable", "zip"),
    "macos-x64": ("macos-x64", "pkg"),
    "macos-arm64": ("macos-arm64", "pkg"),
    "linux-amd64": ("linux-amd64", "deb"),
    "linux-arm64": ("linux-arm64", "deb"),
    "ios-arm64": ("ios-arm64-unsigned", "zip"),
    "android-arm64": ("android-arm64-unsigned", "apk"),
}


def version_info(version, fallback_date=None):
    version = version.removeprefix("v")
    base = r"[0-9]+(?:\.[0-9]+){1,3}"
    for pattern in (
        rf"(?P<base>{base})-(?P<patch>[0-9]+)-(?P<date>[0-9]{{8}})",
        rf"(?P<base>{base})-(?P<date>[0-9]{{8}})(?:-(?P<patch>[0-9]+))?",
        rf"(?P<base>{base})(?:-(?P<patch>[0-9]+))?",
    ):
        match = re.fullmatch(pattern, version)
        if match:
            fields = match.groupdict()
            date = fields.get("date")
            release_date = (datetime.datetime.strptime(date, "%Y%m%d").date().isoformat()
                            if date else fallback_date)
            if not release_date:
                raise ValueError(f"Release date is missing for {version}")
            datetime.date.fromisoformat(release_date)
            return {"latest_version": version, "version": version,
                    "releaseDate": release_date, "patch": int(fields.get("patch") or 0)}
    raise ValueError(f"Invalid release version: {version}")


def artifact_downloads(directory):
    downloads = {}
    for path in sorted(Path(directory).iterdir()):
        if not path.is_file():
            continue
        for key, (prefix, extension) in ARTIFACTS.items():
            match = re.fullmatch(rf"crossdesk-{prefix}-v([0-9][0-9.\-]*)\.{extension}", path.name)
            if not match:
                continue
            if key in downloads:
                raise ValueError(f"Multiple release artifacts for {key}")
            metadata = version_info(match[1])
            downloads[key] = {"url": DOWNLOAD_BASE + path.name, "filename": path.name,
                              "version": metadata["version"], "releaseDate": metadata["releaseDate"]}
            break
    if not downloads:
        raise ValueError("No supported release artifacts found")
    return downloads


def merge_release(current, tag, name, notes, downloads, published_date=None):
    if not isinstance(current, dict) or not isinstance(current.get("downloads"), dict):
        raise ValueError("Current version.json must contain a downloads object")
    mobile = re.fullmatch(r"(ios|android)-(v.+)", tag)
    if mobile:
        platform, version = mobile.groups()
        expected_key = f"{platform}-arm64"
        if set(downloads) != {expected_key}:
            raise ValueError(f"{tag} must contain only the {expected_key} release artifact")
        metadata = version_info(version, published_date)
        if downloads[expected_key]["version"] != metadata["version"]:
            raise ValueError("Mobile source tag does not match the artifact version")
    else:
        if not tag.startswith("v"):
            raise ValueError(f"Expected a version tag, got {tag!r}")
        metadata = version_info(tag, published_date)

    result = dict(current)
    result.pop("platforms", None)
    result["downloads"] = {**current["downloads"], **downloads}
    if not mobile:
        result.update({**metadata, "releaseName": name, "releaseNotes": notes, "tagName": tag})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--tag")
    parser.add_argument("--name")
    parser.add_argument("--notes", type=Path)
    parser.add_argument("--release-json", type=Path)
    args = parser.parse_args()
    if args.release_json:
        release = json.loads(args.release_json.read_text())
        tag = release["tagName"]
        name = release.get("name") or tag
        notes = release.get("body") or ""
        published_date = (release.get("publishedAt") or "").split("T")[0] or None
    else:
        if not args.tag or not args.name or not args.notes:
            parser.error("Supply --release-json or --tag, --name and --notes")
        tag, name, notes = args.tag, args.name, args.notes.read_text()
        published_date = None
    current = json.loads(args.current.read_text())
    downloads = artifact_downloads(args.artifacts)
    result = merge_release(current, tag, name, notes, downloads, published_date)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(f"Updated {tag}: {', '.join(downloads)}")


if __name__ == "__main__":
    main()
