#!/usr/bin/env python3
"""Inspect and update CrossDesk component versions without building or publishing."""

import argparse
import difflib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")
COMPONENTS = ("desktop", "android", "ios", "harmony", "cli", "mcp", "pi")
MOBILE = ("android", "ios", "harmony")
CLIENTS = ("desktop", *MOBILE)
CLIENT_GROUPS = ("client", "mobile")
TOOLS = ("cli", "mcp")
COMPONENT_CHOICES = (*CLIENT_GROUPS, *COMPONENTS)


def validate_version(value, prerelease=False):
    match = SEMVER.fullmatch(value)
    if not match or (match[4] and not prerelease):
        raise ValueError(f"Invalid version: {value!r}; use X.Y.Z" + (" or X.Y.Z-rc.N" if prerelease else ""))
    if match[4] and any(part.isdigit() and len(part) > 1 and part[0] == "0" for part in match[4].split(".")):
        raise ValueError(f"Invalid prerelease version: {value!r}; numeric identifiers cannot have leading zeros")
    return match


def json_span(text, keys, start=0):
    """Find a JSON scalar by object path, preserving all unrelated formatting."""
    decoder = json.JSONDecoder()

    def skip(index):
        while index < len(text) and text[index].isspace():
            index += 1
        return index

    index = skip(start)
    if text[index:index + 1] != "{":
        raise ValueError(f"Expected a JSON object at {keys!r}")
    index = skip(index + 1)
    matches = []
    while text[index:index + 1] != "}":
        key, index = decoder.raw_decode(text, index)
        index = skip(index)
        if text[index:index + 1] != ":":
            raise ValueError("Expected ':' in JSON object")
        begin = skip(index + 1)
        value, end = decoder.raw_decode(text, begin)
        if key == keys[0]:
            matches.append(json_span(text, keys[1:], begin) if len(keys) > 1 else (begin, end, value))
        index = skip(end)
        if text[index:index + 1] == "}":
            break
        if text[index:index + 1] != ",":
            raise ValueError("Expected ',' in JSON object")
        index = skip(index + 1)
    if len(matches) != 1:
        raise ValueError(f"Expected one JSON field at {'.'.join(keys)!r}, found {len(matches)}")
    return matches[0]


class Field:
    def __init__(self, file, pattern=None, keys=None, multiple=False):
        self.file, self.pattern, self.keys, self.multiple = file, pattern, keys, multiple

    def spans(self, text):
        if self.keys is not None:
            json.loads(text)
            return [json_span(text, self.keys)]
        matches = list(re.finditer(self.pattern, text, re.MULTILINE))
        if not matches or (not self.multiple and len(matches) != 1):
            raise ValueError(f"{self.file}: expected {'at least ' if self.multiple else ''}one version field, found {len(matches)}")
        return [(m.start('value'), m.end('value'), m['value']) for m in matches]

    def values(self, snapshot):
        values = [value for _, _, value in self.spans(snapshot.read(self.file))]
        if any(not isinstance(value, str) for value in values):
            raise ValueError(f"{self.file}: expected a string version")
        return values

    def update(self, snapshot, value):
        text = snapshot.read(self.file)
        for start, end, _ in reversed(self.spans(text)):
            text = text[:start] + (json.dumps(value) if self.keys is not None else value) + text[end:]
        snapshot.set(self.file, text)


def package_fields(snapshot, directory):
    fields = [Field(f"{directory}/package.json", keys=("version",))]
    if (snapshot.root / directory / "package-lock.json").exists():
        fields.extend([Field(f"{directory}/package-lock.json", keys=("version",)),
                       Field(f"{directory}/package-lock.json", keys=("packages", "", "version"))])
    return fields


def fields(snapshot, component):
    if component in ("cli", "mcp", "pi"):
        directory = "apps/cli/plugins/pi" if component == "pi" else f"apps/{component}"
        result = {"version": package_fields(snapshot, directory)}
        if component == "cli":
            result["version"].append(Field("apps/cli/xmake.lua", r'set_version\("(?P<value>[^"\n]+)"\)'))
        elif component == "mcp":
            result["version"].append(Field("apps/mcp/integrations/deepseek-harness/package.json", keys=("version",)))
            result["native_version"] = [Field("apps/mcp/src/native.ts", r'^export const NATIVE_VERSION = "(?P<value>[^"\n]+)";')]
        return result
    if component == "desktop":
        return {"version": [Field("apps/desktop/xmake/options.lua", r'option\("CROSSDESK_VERSION"\)\s*set_default\("(?P<value>[^"\n]+)"\)')]}
    if component == "android":
        return {key: [Field("apps/android/gradle.properties", rf'^{prop}=(?P<value>[^\r\n]+)\r?$')]
                for key, prop in (("version", "crossdeskVersionName"), ("build", "crossdeskVersionCode"))}
    if component == "ios":
        return {key: [Field("apps/ios/CrossDeskMobile.xcodeproj/project.pbxproj",
                            rf'^\s*{prop} = (?P<value>[^;\r\n]+);', multiple=True)]
                for key, prop in (("version", "MARKETING_VERSION"), ("build", "CURRENT_PROJECT_VERSION"))}
    if component == "harmony":
        return {"version": [Field("apps/harmony/AppScope/app.json5", r'"versionName"\s*:\s*"(?P<value>[^"\n]+)"')],
                "build": [Field("apps/harmony/AppScope/app.json5", r'"versionCode"\s*:\s*(?P<value>[0-9]+)(?=\s*[,}])')]}
    raise ValueError(f"Unknown component: {component}")


def validate_build(component, value):
    # Keep an integer build number for all three mobile applications.
    maximum = {"android": 2100000000, "ios": 9999, "harmony": 2147483647}[component]
    if not re.fullmatch(r"[1-9][0-9]*", value) or int(value) > maximum:
        raise ValueError(f"{component}: build must be an integer from 1 to {maximum}")


class Snapshot:
    def __init__(self, root=ROOT):
        self.root = Path(root)
        self.original = {}
        self.current = {}

    def read(self, file):
        if file not in self.original:
            text = (self.root / file).read_bytes().decode("utf-8")
            self.original[file] = self.current[file] = text
        return self.current[file]

    def set(self, file, value):
        self.read(file)
        self.current[file] = value

    def state(self, component, check=True):
        if component in CLIENT_GROUPS:
            clients = {platform: self.state(platform, check=check) for platform in CLIENTS}
            if len({data["version"] for data in clients.values()}) != 1:
                versions = ", ".join(f"{platform}={data['version']}" for platform, data in clients.items())
                raise ValueError(f"Client versions differ: {versions}; use set client X.Y.Z to synchronize them")
            return {"version": clients["desktop"]["version"],
                    **{f"{platform}_build": clients[platform]["build"] for platform in MOBILE}}
        result = {}
        for key, group in fields(self, component).items():
            values = [value for field in group for value in field.values(self)]
            if check and len(set(values)) != 1:
                raise ValueError(f"{component}.{key} is inconsistent: {', '.join(values)}; use set to synchronize it")
            value = values[0]
            if key == "build":
                validate_build(component, value)
            else:
                validate_version(value, component in ("cli", "mcp", "pi"))
                if component == "desktop" and any(int(part) > 65535 for part in value.split(".")):
                    raise ValueError("Desktop version components must fit Windows version resources (0–65535)")
            result[key] = value
        return result

    def changes(self):
        return {file: text for file, text in self.current.items() if text != self.original[file]}

    def save(self, dry_run=False):
        changes = self.changes()
        for file, text in changes.items():
            if dry_run:
                print("".join(difflib.unified_diff(self.original[file].splitlines(keepends=True),
                      text.splitlines(keepends=True), fromfile=file, tofile=file)), end="")
        if dry_run or not changes:
            return
        for file in changes:
            if (self.root / file).read_bytes().decode("utf-8") != self.original[file]:
                raise ValueError(f"{file} changed while preparing the update; retry")
        written = []
        try:
            for file, text in changes.items():
                atomic_write(self.root / file, text)
                written.append(file)
        except OSError:
            for file in reversed(written):
                if (self.root / file).read_bytes().decode("utf-8") == changes[file]:
                    atomic_write(self.root / file, self.original[file])
            raise
        for file in changes:
            print(f"Updated {file}")


def atomic_write(file, text):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=file.parent, prefix=f".{file.name}.", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text.encode("utf-8"))
        os.chmod(temporary, stat.S_IMODE(file.stat().st_mode))
        os.replace(temporary, file)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def check_tools(snapshot):
    cli, mcp = (snapshot.state(component) for component in TOOLS)
    if not (cli["version"] == mcp["version"] == mcp["native_version"]):
        raise ValueError(f"CLI/MCP versions differ: cli={cli['version']}, mcp={mcp['version']}, "
                         f"mcp.native_version={mcp['native_version']}; use set cli X.Y.Z or set mcp X.Y.Z to synchronize them")


def update_component(snapshot, component, version, build=None):
    previous = snapshot.state(component, check=False)
    validate_version(version, component in ("cli", "mcp", "pi"))
    if build is not None and component not in MOBILE:
        raise ValueError("--build is only available for android, ios and harmony")
    values = {"version": version}
    if component in MOBILE:
        build = build if build is not None else str(int(previous["build"]) + (version != previous["version"]))
        validate_build(component, build)
        if int(build) < int(previous["build"]) or (version != previous["version"] and build == previous["build"]):
            raise ValueError(f"{component}: a changed app version requires a higher build number; build numbers cannot decrease")
        values["build"] = build
    if component == "mcp":
        values["native_version"] = version
    for key, value in values.items():
        for field in fields(snapshot, component)[key]:
            field.update(snapshot, value)
    snapshot.state(component)


def update(snapshot, component, version, build=None,
           android_build=None, ios_build=None, harmony_build=None):
    if component in CLIENT_GROUPS or component in CLIENTS:
        if build is not None and component not in MOBILE:
            raise ValueError("--build requires android, ios or harmony; use platform-specific build options with client")
        if component not in CLIENT_GROUPS and any(value is not None for value in (android_build, ios_build, harmony_build)):
            raise ValueError("Use client with --android-build/--ios-build/--harmony-build, or --build for the selected platform")
        builds = {"android": android_build, "ios": ios_build, "harmony": harmony_build}
        if component in MOBILE:
            builds[component] = build
        for platform in CLIENTS:
            update_component(snapshot, platform, version, builds.get(platform))
        snapshot.state("client")
    else:
        if any(value is not None for value in (android_build, ios_build, harmony_build)):
            raise ValueError("Platform-specific build options are only available for client")
        if component in TOOLS:
            for tool in TOOLS:
                update_component(snapshot, tool, version, build)
            check_tools(snapshot)
        else:
            update_component(snapshot, component, version, build)


def parser():
    result = argparse.ArgumentParser(description=__doc__,
        epilog="Desktop, Android, iOS and HarmonyOS share one client version; build numbers remain independent. mobile is an alias for client. CLI, MCP, DSH and MCP's native CLI dependency share another version; set/bump cli or mcp updates all of them.")
    sub = result.add_subparsers(dest="command", required=True)
    show = sub.add_parser("show", help="Show current versions")
    show.add_argument("component", nargs="?", choices=COMPONENT_CHOICES)
    show.add_argument("--json", action="store_true", help="Print machine-readable values")
    show.add_argument("--field", choices=("version", "build", "native_version", "android_build", "ios_build", "harmony_build"), help="Print one field of one component")
    check = sub.add_parser("check", help="Validate version formats and synchronized fields")
    check.add_argument("component", nargs="?", choices=COMPONENT_CHOICES)
    for name, description in (("set", "Set an exact version"), ("bump", "Increment a version or mobile build number")):
        command = sub.add_parser(name, help=description)
        command.add_argument("component", choices=COMPONENT_CHOICES)
        if name == "set":
            command.add_argument("version")
        else:
            command.add_argument("part", choices=("major", "minor", "patch", "build"))
        command.add_argument("--build", help="Explicit mobile build number; app version changes otherwise increment it")
        command.add_argument("--android-build", help="Android build number when setting/bumping client")
        command.add_argument("--ios-build", help="iOS build number when setting/bumping client")
        command.add_argument("--harmony-build", help="HarmonyOS build number when setting/bumping client")
        command.add_argument("--dry-run", action="store_true", help="Preview the diff without writing files")
    return result


def main(argv=None, root=ROOT):
    args = parser().parse_args(argv)
    snapshot = Snapshot(root)
    if args.command in ("show", "check"):
        selected = (args.component,) if args.component else COMPONENTS
        values, errors = {}, []
        for component in selected:
            try:
                values[component] = snapshot.state(component)
            except (ValueError, OSError) as error:
                errors.append(f"{component}: {error}")
        if any(component in CLIENTS for component in selected) and not errors:
            try:
                snapshot.state("client")
            except (ValueError, OSError) as error:
                errors.append(str(error))
        if any(component in TOOLS for component in selected) and not errors:
            try:
                check_tools(snapshot)
            except (ValueError, OSError) as error:
                errors.append(str(error))
        if errors:
            raise ValueError("\n".join(errors))
        if args.command == "check":
            print(f"Version checks passed: {', '.join(selected)}")
        elif args.field:
            if not args.component or args.json or args.field not in values[args.component]:
                raise ValueError("--field requires one component with that field and cannot be combined with --json")
            print(values[args.component][args.field])
        elif args.json:
            print(json.dumps(values[args.component] if args.component else values, indent=2))
        else:
            for component, data in values.items():
                print(f"{component:8} " + "  ".join(f"{key}={value}" for key, value in data.items()))
        return
    version, build = getattr(args, "version", None), args.build
    if args.command == "bump":
        if args.component in CLIENTS:
            snapshot.state("client")
        if args.component in TOOLS:
            check_tools(snapshot)
        current = snapshot.state(args.component)
        version = current["version"]
        if args.part == "build":
            if args.component not in MOBILE or any(value is not None for value in (build, args.android_build, args.ios_build, args.harmony_build)):
                raise ValueError("bump build requires android, ios or harmony and cannot be combined with explicit build numbers")
            build = str(int(current["build"]) + 1)
        else:
            match = validate_version(version, prerelease=True)
            if match[4]:
                raise ValueError("Use set to choose the next version explicitly when the current version is a prerelease")
            numbers = [int(match[index]) for index in range(1, 4)]
            index = ("major", "minor", "patch").index(args.part)
            numbers[index] += 1
            numbers[index + 1:] = [0] * (2 - index)
            version = ".".join(map(str, numbers))
    build_only = args.command == "bump" and args.part == "build"
    if build_only:
        update_component(snapshot, args.component, version, build)
    else:
        update(snapshot, args.component, version, build,
               args.android_build, args.ios_build, args.harmony_build)
    snapshot.save(args.dry_run)
    reported = "client" if (args.component in CLIENTS or args.component in CLIENT_GROUPS) and not build_only else args.component
    label = "cli/mcp/dsh" if reported in TOOLS else reported
    print(f"{'Preview' if args.dry_run else 'Current'} {label}: " + json.dumps(snapshot.state(reported)))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print(f"version: {error}", file=sys.stderr)
        sys.exit(1)
