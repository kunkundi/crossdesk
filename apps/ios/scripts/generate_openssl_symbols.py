#!/usr/bin/env python3
"""Generate matching symbols for Xcode's embedded, codeless OpenSSL framework."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


def uuids(path):
    output = subprocess.check_output(["xcrun", "dwarfdump", "--uuid", str(path)], text=True)
    result = dict((arch, uuid.upper()) for uuid, arch in
                  re.findall(r"UUID: ([0-9A-Fa-f-]{36}) \(([^)]+)\)", output))
    if not result:
        raise ValueError(f"No Mach-O UUIDs found: {path}")
    return result


def verify_symbols(binary, dsym):
    dwarf = dsym / "Contents/Resources/DWARF" / binary.name
    if not dwarf.is_file():
        raise ValueError(f"Missing dSYM DWARF file: {dwarf}")
    if uuids(binary) != uuids(dwarf):
        raise ValueError(f"dSYM UUIDs do not match {binary}: {dwarf}")


def generate(binary, output):
    # This is only for Xcode's resource placeholder. Real OpenSSL code remains
    # statically linked into the app and must not receive an empty dSYM here.
    commands = subprocess.check_output(["xcrun", "otool", "-l", str(binary)], text=True)
    text_sizes = re.findall(
        r"sectname __text\s+segname __TEXT\s+addr \S+\s+size (0x[0-9a-fA-F]+)", commands)
    if not text_sizes or any(int(size, 16) for size in text_sizes):
        raise ValueError(f"Expected a codeless OpenSSL resource stub: {binary}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # No function-level debug info exists in this empty stub. dsymutil still
    # emits a valid UUID-matched DWARF companion; quiet suppresses that expected
    # warning only after verifying that there is no executable code to lose.
    subprocess.run(["xcrun", "dsymutil", "--quiet", str(binary), "-o", str(output)], check=True)
    verify_symbols(binary, output)
    subprocess.run(["xcrun", "dwarfdump", "--verify", str(output)], check=True)
    print(f"Generated UUID-matched OpenSSL resource symbols: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    generate(args.binary, args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"OpenSSL symbols: {error}", file=sys.stderr)
        sys.exit(1)
