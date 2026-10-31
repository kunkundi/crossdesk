#!/usr/bin/env python3
"""Check release resources and known restricted imports in a built iOS app."""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys

from generate_openssl_symbols import verify_symbols


def verify(app, require_release=False, dsym_dir=None):
    info = plistlib.loads((app / "Info.plist").read_bytes())
    binary = app / info["CFBundleExecutable"]
    subprocess.run(["xcrun", "lipo", str(binary), "-verify_arch", "arm64"], check=True)
    symbols = subprocess.check_output(["xcrun", "nm", "-u", str(binary)], text=True)
    imports = set(line.split()[-1] for line in symbols.splitlines() if line.strip())
    disk_apis = {"_statfs", "_fstatfs", "_statvfs", "_fstatvfs", "_getfsstat"}
    if imports & disk_apis:
        raise ValueError(f"Unreviewed disk-space APIs remain in iOS app: {sorted(imports & disk_apis)}")
    defined = subprocess.check_output(["xcrun", "nm", "-g", str(binary)], text=True)
    if re.search(r"\b_Wels(?:CreateDecoder|CreateSVCEncoder|GetCodecVersion)\b", defined):
        raise ValueError("OpenH264 must not be present in the iOS app")
    if re.search(r"\b_of_(?:hweight\w*|hw8table|popcount_3)\b", defined):
        raise ValueError("OpenFEC's optional CC-BY-SA-3.0 hamming-weight code requires a separate distribution review")
    privacy = plistlib.loads((app / "PrivacyInfo.xcprivacy").read_bytes())
    if not (app / "PRIVACY.md").read_text().strip():
        raise ValueError("The offline privacy policy must be bundled for pre-consent access")
    reasons = {entry["NSPrivacyAccessedAPIType"]: entry["NSPrivacyAccessedAPITypeReasons"]
               for entry in privacy["NSPrivacyAccessedAPITypes"]}
    required = {
        "NSPrivacyAccessedAPICategoryUserDefaults": {"CA92.1"},
        "NSPrivacyAccessedAPICategoryFileTimestamp": {"C617.1", "3B52.1"},
        "NSPrivacyAccessedAPICategorySystemBootTime": {"35F9.1", "8FFB.1"},
    }
    for category, codes in required.items():
        if set(reasons.get(category, [])) != codes:
            raise ValueError(f"Missing or changed reviewed privacy reasons: {category}")
    framework = app / "Frameworks/OpenSSL.framework"
    sdk_info = plistlib.loads((framework / "Info.plist").read_bytes())
    sdk_privacy_data = (framework / "PrivacyInfo.xcprivacy").read_bytes()
    sdk_privacy = plistlib.loads(sdk_privacy_data)
    expected_privacy = {
        "NSPrivacyTracking": False,
        "NSPrivacyTrackingDomains": [],
        "NSPrivacyCollectedDataTypes": [],
        "NSPrivacyAccessedAPITypes": [],
    }
    if sdk_privacy != expected_privacy:
        raise ValueError("OpenSSL SDK privacy declaration differs from the reviewed iOS configuration")
    sdk_build = json.loads((framework / "BuildMetadata.json").read_text())
    if (sdk_info["CFBundleShortVersionString"] != sdk_build["version"]
            or sdk_build["privacySHA256"] != hashlib.sha256(sdk_privacy_data).hexdigest()):
        raise ValueError("OpenSSL SDK resources do not match its build metadata")
    embedded_binary = framework / sdk_info["CFBundleExecutable"]
    # Xcode removes the archive and may generate a small codeless-framework
    # stub. It must not ship a second static archive or dynamic OpenSSL copy.
    if embedded_binary.exists():
        if embedded_binary.read_bytes().startswith(b"!<arch>\n"):
            raise ValueError("Xcode must remove the static OpenSSL archive when embedding its resources")
        sdk_symbols = subprocess.check_output(["xcrun", "nm", "-gU", str(embedded_binary)], text=True)
        if re.search(r"\b_(?:SSL_new|OPENSSL_init_crypto)\b", sdk_symbols):
            raise ValueError("Unexpected dynamic OpenSSL implementation in resource framework")
    # Archive validation includes the exact files that Organizer uploads.
    if dsym_dir is None and app.parents[2].suffix == ".xcarchive":
        dsym_dir = app.parents[2] / "dSYMs"
    if dsym_dir is not None:
        verify_symbols(binary, dsym_dir / f"{app.name}.dSYM")
        if embedded_binary.exists():
            verify_symbols(embedded_binary, dsym_dir / "OpenSSL.framework.dSYM")
        print("Verified app and embedded OpenSSL dSYM UUIDs")
    linked = subprocess.check_output(["xcrun", "otool", "-L", str(binary)], text=True)
    if "OpenSSL.framework" in linked:
        raise ValueError("OpenSSL must be statically linked into the application")
    source = json.loads((app / "SourceMetadata.json").read_text())
    if require_release and (source["isModified"] or not source["tag"]):
        raise ValueError("The app was not built from an unmodified version tag")
    if not re.fullmatch(r"[0-9a-f]{40}", source["revision"]):
        raise ValueError("Invalid source revision")
    print("Verified arm64 app, source metadata, app privacy declarations and OpenSSL SDK privacy packaging")
    print("Signing, App Store validation and license authorization require separate review.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--require-release", action="store_true")
    parser.add_argument("--dsym-dir", type=Path)
    args = parser.parse_args()
    verify(args.app.resolve(), args.require_release, args.dsym_dir)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"iOS bundle verification: {error}", file=sys.stderr)
        sys.exit(1)
