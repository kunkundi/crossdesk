#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

verify=false
if [[ $# -eq 1 && "$1" == "--verify" ]]; then
    verify=true
elif [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--verify]" >&2
    exit 2
fi

minimum_version="${XMAKE_MIN_VERSION:-}"
if [[ ! "$minimum_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "XMAKE_MIN_VERSION must be a numeric major.minor.patch version, for example 3.1.1." >&2
    exit 2
fi

version_at_least() {
    local -a installed_parts required_parts
    local index installed_component required_component
    IFS=. read -r -a installed_parts <<< "$1"
    IFS=. read -r -a required_parts <<< "$2"

    for index in 0 1 2; do
        installed_component="${installed_parts[$index]}"
        required_component="${required_parts[$index]}"
        # Strip leading zeroes and compare lengths first, avoiding both octal
        # interpretation and integer overflow in Bash's arithmetic expansion.
        while [[ ${#installed_component} -gt 1 && "$installed_component" == 0* ]]; do
            installed_component="${installed_component#0}"
        done
        while [[ ${#required_component} -gt 1 && "$required_component" == 0* ]]; do
            required_component="${required_component#0}"
        done
        if [[ ${#installed_component} -gt ${#required_component} ]]; then
            return 0
        elif [[ ${#installed_component} -lt ${#required_component} ]]; then
            return 1
        elif [[ "$installed_component" > "$required_component" ]]; then
            return 0
        elif [[ "$installed_component" < "$required_component" ]]; then
            return 1
        fi
    done
    return 0
}

detected_version=""
needs_upgrade=true
reason="xmake was not found on PATH"
if command -v xmake >/dev/null 2>&1; then
    if version_output="$(xmake --root --version 2>&1)"; then
        # Xmake can color individual words and version components. Strip ANSI
        # CSI sequences before matching, using only Bash 3.2 builtins.
        ansi_pattern=$'\033''\[[0-?]*[ -/]*[@-~]'
        while [[ "$version_output" =~ $ansi_pattern ]]; do
            version_output="${version_output//"${BASH_REMATCH[0]}"/}"
        done
        version_pattern='(^|[[:space:]])xmake[[:space:]]+v([0-9]+\.[0-9]+\.[0-9]+)([+][[:alnum:].-]+)?([,[:space:]]|$)'
        while IFS= read -r line; do
            if [[ "$line" =~ $version_pattern ]]; then
                detected_version="${BASH_REMATCH[2]}"
                break
            fi
        done <<< "$version_output"

        if [[ -z "$detected_version" ]]; then
            reason="xmake --root --version did not report a recognizable Xmake version"
        elif version_at_least "$detected_version" "$minimum_version"; then
            needs_upgrade=false
        else
            reason="installed Xmake $detected_version is below $minimum_version"
        fi
    else
        reason="xmake --root --version failed with exit code $?"
    fi
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        printf 'needs-upgrade=%s\n' "$needs_upgrade"
        printf 'detected-version=%s\n' "$detected_version"
    } >> "$GITHUB_OUTPUT"
else
    printf 'needs-upgrade=%s\n' "$needs_upgrade"
    printf 'detected-version=%s\n' "$detected_version"
fi

if [[ "$needs_upgrade" == true ]]; then
    if [[ "$verify" == true ]]; then
        echo "Xmake verification failed: $reason. Ensure Xmake $minimum_version or newer is installed and on PATH." >&2
        exit 1
    fi
    echo "Xmake upgrade required: $reason. Install Xmake $minimum_version or newer."
else
    echo "Keeping installed Xmake $detected_version; it meets the minimum version $minimum_version."
fi
