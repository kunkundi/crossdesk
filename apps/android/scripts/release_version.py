#!/usr/bin/env python3
"""Resolve Android package and artifact versions before building the APKs."""

import datetime
import os
from pathlib import Path
import re
from zoneinfo import ZoneInfo


def resolve_version(version_name, version_code, tag="", patch="0", today=None):
    build_date = today or datetime.datetime.now(ZoneInfo("Asia/Shanghai")).strftime("%Y%m%d")
    patch = patch if re.fullmatch(r"[0-9]+", patch) else "0"
    if tag:
        independent = tag.startswith("android-v")
        source_version = tag.removeprefix("android-")
        base = r"(?P<base>[0-9]+\.[0-9]+\.[0-9]+)"
        for pattern in (
            rf"v{base}-(?P<patch>[0-9]+)-(?P<date>[0-9]{{8}})",
            rf"v{base}-(?P<date>[0-9]{{8}})(?:-(?P<patch>[0-9]+))?",
            rf"v{base}(?:-(?P<patch>[0-9]+))?",
        ):
            match = re.fullmatch(pattern, source_version)
            if match:
                fields = match.groupdict()
                if independent and fields["base"] != version_name:
                    raise ValueError(f"Android source tag does not match the app version: {tag} vs {version_name}")
                version_name = fields["base"]
                build_date = fields.get("date") or build_date
                patch = fields.get("patch") or patch
                break
        else:
            raise ValueError(f"Invalid Android release tag: {tag!r}")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version_name):
        raise ValueError(f"Invalid Android versionName: {version_name!r}")
    if not re.fullmatch(r"[0-9]+", version_code) or not 0 < int(version_code) <= 2100000000:
        raise ValueError(f"Invalid Android versionCode: {version_code!r}")
    datetime.datetime.strptime(build_date, "%Y%m%d")
    suffix = f"-{patch}" if patch != "0" else ""
    artifact_version = f"v{version_name}{suffix}-{build_date}"
    if tag.startswith("android-v") and tag.removeprefix("android-") != artifact_version:
        raise ValueError(f"Android source tag does not match the artifact version: {tag} vs {artifact_version}")
    return {"ANDROID_VERSION_NAME": version_name, "ANDROID_VERSION_CODE": str(int(version_code)),
            "ANDROID_ARTIFACT_VERSION": artifact_version}


def main():
    properties = dict(line.split("=", 1) for line in
                      (Path(__file__).resolve().parents[1] / "gradle.properties").read_text().splitlines()
                      if "=" in line and not line.lstrip().startswith("#"))
    tag = os.environ.get("CROSSDESK_SOURCE_TAG", "")
    if not tag and os.environ.get("GITHUB_REF_TYPE") == "tag":
        ref_name = os.environ["GITHUB_REF_NAME"]
        # Moving aliases such as "latest" are builds, not versioned releases.
        if ref_name.startswith(("v", "android-v")):
            tag = ref_name
    values = resolve_version(properties["crossdeskVersionName"], properties["crossdeskVersionCode"],
                             tag, os.environ.get("PATCH_NUMBER", "0"))
    text = "".join(f"{key}={value}\n" for key, value in values.items())
    if os.environ.get("GITHUB_ENV"):
        with open(os.environ["GITHUB_ENV"], "a") as stream:
            stream.write(text)
    print(text, end="")


if __name__ == "__main__":
    main()
