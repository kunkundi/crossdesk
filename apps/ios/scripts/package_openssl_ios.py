#!/usr/bin/env python3
"""Package source-built iOS OpenSSL as a static framework with privacy resources."""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile

IOS = Path(__file__).resolve().parents[1]
PRIVACY = IOS / "OpenSSL/PrivacyInfo.xcprivacy"
# The reviewed iOS configuration removes these calls rather than declaring
# unrelated purposes for system entropy devices and certificate directories.
FORBIDDEN_APIS = {
    "stat", "fstat", "lstat", "fstatat", "stat64", "fstat64", "lstat64",
    "getattrlist", "fgetattrlist", "getattrlistbulk", "getdirentriesattr",
    "statfs", "fstatfs", "statvfs", "fstatvfs", "getfsstat",
    "mach_absolute_time", "mach_continuous_time", "sysctl",
}


def package(inputs, output, platform="iphoneos"):
    catalog = json.loads((IOS / "CrossDeskMobile/Resources/ThirdPartyLicenses.json").read_text())
    component = next(c for c in catalog["components"] if c["id"] == "openssl3")
    archives = {}
    for line in inputs.read_text().splitlines():
        sha, name = line.split(maxsplit=1)
        path = Path(name)
        if path.name in {"libssl.a", "libcrypto.a"}:
            if path.name in archives:
                raise ValueError(f"Ambiguous OpenSSL input: {path.name}")
            if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
                raise ValueError(f"OpenSSL input checksum mismatch: {path}")
            archives[path.name] = (path, sha)
    if set(archives) != {"libssl.a", "libcrypto.a"}:
        raise ValueError("Both source-built OpenSSL archives must be present")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="openssl-framework-", dir=output.parent) as temp:
        binary = Path(temp) / "OpenSSL"
        subprocess.run(["/usr/bin/libtool", "-static", "-o", str(binary),
                        *(str(archives[name][0]) for name in sorted(archives))], check=True)
        subprocess.run(["xcrun", "lipo", str(binary), "-verify_arch", "arm64"], check=True)
        symbols = subprocess.check_output(["xcrun", "nm", "-u", str(binary)], text=True)
        imports = {line.split()[-1].lstrip("_").split("$")[0]
                   for line in symbols.splitlines() if line.strip() and not line.endswith(":")}
        unexpected = imports & FORBIDDEN_APIS
        if unexpected:
            raise ValueError(f"OpenSSL privacy configuration has unreviewed APIs: {sorted(unexpected)}")
        # armcap.c queries only CPU instruction support/brand to select crypto
        # implementations. This is not boot time, disk space or fingerprinting.
        member = ""
        for line in symbols.splitlines():
            if line.endswith(":"):
                member = line
            elif "_sysctlbyname" in line and member != "libcrypto-lib-armcap.o:":
                raise ValueError(f"Unreviewed OpenSSL sysctl caller: {member}")
        # getrandom uses Darwin's getentropy via OpenSSL's existing DSO lookup;
        # the random device fallback is excluded at compile time.
        output.mkdir(parents=True, exist_ok=True)
        files = {
            "OpenSSL": binary.read_bytes(),
            "PrivacyInfo.xcprivacy": PRIVACY.read_bytes(),
            "Info.plist": plistlib.dumps({
                "CFBundleIdentifier": "cn.crossdesk.OpenSSL",
                "CFBundleName": "OpenSSL",
                "CFBundleExecutable": "OpenSSL",
                "CFBundlePackageType": "FMWK",
                "CFBundleInfoDictionaryVersion": "6.0",
                "CFBundleVersion": component["version"],
                "CFBundleShortVersionString": component["version"],
                "CFBundleSupportedPlatforms": ["iPhoneSimulator" if platform == "iphonesimulator" else "iPhoneOS"],
                "MinimumOSVersion": "16.0",
            }),
            "LICENSE.txt": "\n\n".join(d["text"] for d in component["documents"]).encode(),
            "BuildMetadata.json": (json.dumps({
                "version": component["version"],
                "sourceURL": component["sourceURL"],
                "buildSourceURL": component["buildSourceURL"],
                "archives": {name: sha for name, (_, sha) in sorted(archives.items())},
                "privacySHA256": hashlib.sha256(PRIVACY.read_bytes()).hexdigest(),
            }, indent=2) + "\n").encode(),
        }
        for name, data in files.items():
            target = output / name
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
        print(f"Packaged source-built OpenSSL {component['version']}: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--platform", choices=("iphoneos", "iphonesimulator"), default="iphoneos")
    args = parser.parse_args()
    package(args.inputs, args.output, args.platform)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"OpenSSL framework: {error}", file=sys.stderr)
        sys.exit(1)
