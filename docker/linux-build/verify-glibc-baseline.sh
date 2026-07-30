#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <ELF-file-or-directory> [maximum-glibc-version]" >&2
    exit 2
fi

INPUT_PATH="$1"
MAXIMUM_VERSION="${2:-2.31}"

verify_elf() {
    local elf_file="$1"
    local version=""
    local highest_version="none"
    local -a required_versions=()

    mapfile -t required_versions < <(
        readelf --version-info "${elf_file}" \
            | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' \
            | sed 's/^GLIBC_//' \
            | sort -Vu \
            || true
    )

    for version in "${required_versions[@]}"; do
        if dpkg --compare-versions "${version}" gt "${MAXIMUM_VERSION}"; then
            echo "${elf_file} requires GLIBC_${version}; maximum allowed is GLIBC_${MAXIMUM_VERSION}" >&2
            return 1
        fi
    done

    if [[ ${#required_versions[@]} -gt 0 ]]; then
        highest_version="${required_versions[-1]}"
    fi
    echo "Verified ${elf_file}: highest required glibc version is ${highest_version}"
}

if [[ -f "${INPUT_PATH}" ]]; then
    if ! readelf -h "${INPUT_PATH}" >/dev/null 2>&1; then
        echo "Not an ELF file: ${INPUT_PATH}" >&2
        exit 1
    fi
    verify_elf "${INPUT_PATH}"
    exit 0
fi

if [[ ! -d "${INPUT_PATH}" ]]; then
    echo "File or directory does not exist: ${INPUT_PATH}" >&2
    exit 1
fi

ELF_COUNT=0
while IFS= read -r -d '' candidate; do
    if readelf -h "${candidate}" >/dev/null 2>&1; then
        verify_elf "${candidate}"
        ELF_COUNT=$((ELF_COUNT + 1))
    fi
done < <(find "${INPUT_PATH}" -type f -print0)

if [[ ${ELF_COUNT} -eq 0 ]]; then
    echo "No ELF files found under ${INPUT_PATH}" >&2
    exit 1
fi
