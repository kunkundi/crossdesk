#!/usr/bin/env bash
set -euo pipefail

EXPECTED_ARCH="${EXPECTED_ARCH:-}"
EXPECTED_CMAKE_VERSION="${EXPECTED_CMAKE_VERSION:-3.31.6}"
EXPECTED_XMAKE_VERSION="${EXPECTED_XMAKE_VERSION:-3.0.9}"
EXPECTED_RUST_VERSION="${EXPECTED_RUST_VERSION:-1.92.0}"
BUILD_CC="${CC:-/usr/bin/gcc-10}"
BUILD_CXX="${CXX:-/usr/bin/g++-10}"

# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID}" != "ubuntu" || "${VERSION_ID}" != "20.04" ]]; then
    echo "Expected Ubuntu 20.04, found ${ID:-unknown} ${VERSION_ID:-unknown}" >&2
    exit 1
fi

GLIBC_VERSION="$(getconf GNU_LIBC_VERSION | awk '{print $2}')"
if [[ "${GLIBC_VERSION}" != "2.31" ]]; then
    echo "Expected glibc 2.31, found ${GLIBC_VERSION}" >&2
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
cmake --version | grep -F "cmake version ${EXPECTED_CMAKE_VERSION}" >/dev/null
"${BUILD_CC}" --version | grep -F 'gcc-10' >/dev/null
"${BUILD_CXX}" --version | grep -F 'g++-10' >/dev/null
printf '%s\n' \
    '#include <concepts>' \
    '#include <span>' \
    'template <typename T> concept TestConcept = true;' \
    'int main() {' \
    '    int value = 0;' \
    '    std::span<int> values(&value, 1);' \
    '    static_assert(TestConcept<int>);' \
    '    return values.empty();' \
    '}' \
    | "${BUILD_CXX}" -std=c++20 -fsyntax-only -x c++ -
for pkg_config_package in \
    dbus-1 \
    libdrm \
    xft; do
    if ! pkg-config --exists "${pkg_config_package}"; then
        echo "Required pkg-config package is missing: ${pkg_config_package}" >&2
        exit 1
    fi
done

for sdk_header in \
    /opt/crossdesk-pipewire-sdk/include/pipewire-0.3/pipewire/pipewire.h \
    /opt/crossdesk-pipewire-sdk/include/pipewire-0.3/pipewire/version.h \
    /opt/crossdesk-pipewire-sdk/include/spa-0.2/spa/param/video/format-utils.h; do
    if [[ ! -f "${sdk_header}" ]]; then
        echo "Required PipeWire build SDK header is missing: ${sdk_header}" >&2
        exit 1
    fi
done

printf '%s\n' \
    '#include <pipewire/pipewire.h>' \
    '#include <spa/param/video/format-utils.h>' \
    'static_assert(PW_CHECK_VERSION(0, 3, 48));' \
    'int main() { return 0; }' \
    | "${BUILD_CXX}" -std=c++17 -fsyntax-only -x c++ \
        -I/opt/crossdesk-pipewire-sdk/include/pipewire-0.3 \
        -I/opt/crossdesk-pipewire-sdk/include/spa-0.2 -

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
