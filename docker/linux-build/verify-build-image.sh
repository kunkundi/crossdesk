#!/usr/bin/env bash
set -euo pipefail

EXPECTED_ARCH="${EXPECTED_ARCH:-}"
EXPECTED_XMAKE_VERSION="${EXPECTED_XMAKE_VERSION:-3.0.9}"
EXPECTED_RUST_VERSION="${EXPECTED_RUST_VERSION:-1.92.0}"

# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID}" != "ubuntu" || "${VERSION_ID}" != "22.04" ]]; then
    echo "Expected Ubuntu 22.04, found ${ID:-unknown} ${VERSION_ID:-unknown}" >&2
    exit 1
fi

GLIBC_VERSION="$(getconf GNU_LIBC_VERSION | awk '{print $2}')"
if [[ "${GLIBC_VERSION}" != "2.35" ]]; then
    echo "Expected glibc 2.35, found ${GLIBC_VERSION}" >&2
    exit 1
fi

ACTUAL_ARCH="$(dpkg --print-architecture)"
if [[ -n "${EXPECTED_ARCH}" && "${ACTUAL_ARCH}" != "${EXPECTED_ARCH}" ]]; then
    echo "Expected architecture ${EXPECTED_ARCH}, found ${ACTUAL_ARCH}" >&2
    exit 1
fi

XMAKE_VERSION_OUTPUT="$(xmake --version 2>&1)"
grep -F "v${EXPECTED_XMAKE_VERSION}" <<<"${XMAKE_VERSION_OUTPUT}" >/dev/null
rustc --version | grep -F "rustc ${EXPECTED_RUST_VERSION}" >/dev/null
cargo --version >/dev/null
cmake --version >/dev/null
g++ --version >/dev/null
pkg-config --exists xft

for package_path in \
    "s/slint" \
    "l/libdatachannel" \
    "o/openssl3" \
    "s/spdlog"; do
    if [[ ! -d "${XMAKE_GLOBALDIR}/.xmake/packages/${package_path}" ]]; then
        echo "Prebuilt xmake package is missing: ${package_path}" >&2
        exit 1
    fi
done

if ! find "${XMAKE_GLOBALDIR}/.xmake/packages/s/slint" \
    -type f -name 'slint-compiler*' -print -quit | grep -q .; then
    echo "The prebuilt Slint compiler is missing" >&2
    exit 1
fi

if [[ "${ACTUAL_ARCH}" == "amd64" && ! -f "${CUDA_PATH}/include/cuda.h" ]]; then
    echo "The amd64 image is missing CUDA headers under ${CUDA_PATH}" >&2
    exit 1
fi

echo "Verified CrossDesk build image: Ubuntu ${VERSION_ID}, ${ACTUAL_ARCH}, glibc ${GLIBC_VERSION}"
