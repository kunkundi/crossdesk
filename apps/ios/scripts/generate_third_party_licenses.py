#!/usr/bin/env python3
"""Collect iOS notices from pinned source archives; validate them offline at build time."""

import argparse
from collections import defaultdict
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

IOS = Path(__file__).resolve().parents[1]
REPO = IOS.parents[1]
LOCK = IOS / "licenses/sources.json"
OUTPUT = IOS / "CrossDeskMobile/Resources/ThirdPartyLicenses.json"
COMMENT = re.compile(rb"/\*.*?\*/|(?://[^\n]*(?:\n|$))+", re.S)
RIGHTS = re.compile(rb"copyright|SPDX-License|permission is hereby|author disclaims|public domain", re.I)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode()


def checked(data, expected, label):
    if digest(data) != expected:
        raise ValueError(f"Source checksum mismatch: {label}")
    return data


def archive_path(component, cache):
    source = component["source"]
    target = cache / source["sha256"]
    if target.exists():
        checked(target.read_bytes(), source["sha256"], component["id"])
        return target
    cache.mkdir(parents=True, exist_ok=True)
    # Xmake may remove extracted sources or COPYING; read the original archive.
    candidates = (Path.home() / ".xmake/cache/packages").glob(
        f"*/{component['id'][0]}/{component['id']}/{component['version']}/*")
    for candidate in candidates:
        if candidate.is_file() and candidate.name.endswith((".tar.gz", ".tar.xz", ".zip")):
            data = candidate.read_bytes()
            if digest(data) == source["sha256"]:
                target.write_bytes(data)
                return target
    request = urllib.request.Request(source["url"], headers={"User-Agent": "CrossDesk-license-collector"})
    with urllib.request.urlopen(request, timeout=90) as response:
        data = checked(response.read(), source["sha256"], component["id"])
    target.write_bytes(data)
    return target


@contextmanager
def source_files(component, cache):
    if component["source"]["kind"] == "repository":
        yield {}, lambda name: (REPO / name).read_bytes()
        return
    path = archive_path(component, cache)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            names = {name.split("/", 1)[1]: name for name in archive.namelist()
                     if "/" in name and not name.endswith("/")}
            yield names, lambda name: archive.read(names[name])
    else:
        with tarfile.open(path) as archive:
            names = {member.name.split("/", 1)[1]: member.name for member in archive
                     if "/" in member.name and not member.isdir()}
            # No filesystem extraction. tarfile resolves COPYING symlinks within
            # the verified archive, including GLib's LICENSES directory.
            yield names, lambda name: archive.extractfile(names[name]).read()


def component_documents(component, names, read):
    documents = []
    for item in component["documents"]:
        data = checked(read(item["path"]), item["sha256"], item["path"])
        text = data.decode(item.get("encoding", "utf-8"))
        if "end_before" in item:
            if item["end_before"] not in text:
                raise ValueError(f"Missing notice boundary: {item['path']}")
            text = text.split(item["end_before"], 1)[0]
        documents.append({"title": item["path"], "text": text})
    notices = defaultdict(list)
    for name in sorted(names):
        path = Path(name)
        selected = any(name.startswith(prefix) for prefix in component.get("notice_prefixes", []))
        selected |= component.get("notice_root_files", False) and "/" not in name
        if not selected or path.suffix not in (".h", ".hpp", ".c", ".cc", ".cpp", ".S", ".s", ".inc"):
            continue
        if any(part.lower() in ("test", "tests", "benchmarks", "examples") for part in path.parts):
            continue
        for block in COMMENT.findall(read(name)[:16384]):
            if RIGHTS.search(block):
                # Preserve complete original comment blocks, including bundled
                # fmt, semaphore and per-file copyright/permission exceptions.
                encoding = component.get("notice_encodings", {}).get(name, "utf-8")
                notices[block.decode(encoding)].append(name)
    if notices:
        documents.append({"title": "Source copyright and license notices", "text": "\n\n".join(
            "Files: " + ", ".join(files) + "\n\n" + text for text, files in notices.items())})
    if not documents:
        raise ValueError(f"No notices collected for {component['id']}")
    return documents


def generate(lock, cache):
    components = []
    for component in lock["components"]:
        with source_files(component, cache) as (names, read):
            documents = component_documents(component, names, read)
        source = component["source"]
        source_url = source["url"]
        if source["kind"] == "repository" and source.get("revision"):
            source_url += "/tree/" + source["revision"]
        entry = {key: component[key] for key in ("id", "name", "version", "license")} | {
            "sourceURL": source_url, "documents": documents}
        recipe = component.get("recipe_path", "")
        if recipe.startswith("deps/submodules/minirtc/"):
            mini = next(c for c in lock["components"] if c["id"] == "minirtc")
            entry["buildSourceURL"] = (mini["source"]["url"] + "/tree/" +
                mini["source"]["revision"] + "/" +
                str(Path(recipe).relative_to("deps/submodules/minirtc").parent))
        components.append(entry)
        print(f"Collected {component['name']} {component['version']}")
    return json_bytes({"schemaVersion": 1, "components": components})


def verify(lock, inputs, target_info, xmake_repository=None, xmake_version=None):
    checked(OUTPUT.read_bytes(), lock["bundle_sha256"], str(OUTPUT))
    if xmake_version is not None:
        version = re.search(r"xmake v(\d+\.\d+\.\d+)", xmake_version.read_text())
        if not version or version[1] != lock["toolchain"]["xmake_version"]:
            raise ValueError("Xmake version differs from the reviewed iOS toolchain; see apps/ios/README.md")
    if xmake_repository is not None:
        revision = subprocess.check_output(
            ["git", "-C", str(xmake_repository), "rev-parse", "HEAD"], text=True).strip()
        if revision != lock["toolchain"]["xmake_repository_revision"]:
            raise ValueError("Xmake package repository differs from the reviewed iOS source catalog")
        changes = subprocess.check_output(
            ["git", "-C", str(xmake_repository), "status", "--porcelain", "--untracked-files=normal"],
            text=True).strip()
        if changes:
            raise ValueError("Xmake package repository has local changes; use the unmodified pinned revision")
    for component in lock["components"]:
        recipe = component.get("recipe_path", "")
        if recipe.startswith("deps/"):
            checked((REPO / recipe).read_bytes(), component["recipe_sha256"], recipe)
        elif recipe.startswith("xmake-repo/") and xmake_repository is not None:
            checked((xmake_repository / Path(recipe).relative_to("xmake-repo")).read_bytes(),
                    component["recipe_sha256"], recipe)
        if component["source"]["kind"] == "repository":
            for item in component["documents"]:
                checked((REPO / item["path"]).read_bytes(), item["sha256"], item["path"])
    if inputs is None:
        return
    if target_info is None:
        raise ValueError("--target-info is required with --inputs (includes header-only dependencies)")
    known = {c["package"]: c["version"] for c in lock["components"] if "package" in c}
    seen = {}
    # Package names/versions come from resolved Xmake link and include paths,
    # never from an inventory of all packages left in the installation cache.
    pattern = r"/packages/[^/\s]+/([^/\s]+)/([^/\s]+)/[a-f0-9]+/"
    for name, version in re.findall(pattern, inputs.read_text() + target_info.read_text()):
        if name not in known or known[name] != version:
            raise ValueError(f"Unreviewed iOS dependency: {name} {version}; regenerate licenses from its source")
        seen[name] = version
    if set(seen) != set(known):
        raise ValueError(f"License/build dependency mismatch; missing: {sorted(set(known) - set(seen))}")
    for line in inputs.read_text().splitlines():
        sha, path = line.split(maxsplit=1)
        checked(Path(path).read_bytes(), sha, path)
    revision = subprocess.check_output(
        ["git", "-C", str(REPO / "deps/submodules/minirtc"), "rev-parse", "HEAD"], text=True).strip()
    mini = next(c for c in lock["components"] if c["id"] == "minirtc")
    if revision != mini["source"]["revision"]:
        raise ValueError("MiniRTC revision changed; review embedded notices and update sources.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", action="store_true", help="download/check pinned sources and regenerate the bundled JSON")
    parser.add_argument("--cache", type=Path, default=Path.home() / ".cache/crossdesk/licenses")
    parser.add_argument("--inputs", type=Path, help="native archive .inputs.sha256 manifest")
    parser.add_argument("--target-info", type=Path, help="resolved Xmake minirtc target information")
    parser.add_argument("--xmake-repository", type=Path, help="verify the active pinned package repository and recipes")
    parser.add_argument("--xmake-version", type=Path, help="Xmake version output recorded by the native build")
    args = parser.parse_args()
    lock = json.loads(LOCK.read_text())
    if args.generate:
        data = generate(lock, args.cache)
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_bytes(data)
        lock["bundle_sha256"] = digest(data)
        LOCK.write_bytes(json_bytes(lock))
    verify(lock, args.inputs, args.target_info, args.xmake_repository, args.xmake_version)
    print(f"Verified {len(lock['components'])} iOS license components")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Third-party notices: {error}", file=sys.stderr)
        sys.exit(1)
