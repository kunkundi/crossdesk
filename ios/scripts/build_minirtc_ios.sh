#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
IOS_DIR="${SCRIPT_DIR:h}"
REPO_DIR="${IOS_DIR:h}"
MINIRTC_DIR="${REPO_DIR}/submodules/minirtc"
CONFIG_NAME="${CONFIGURATION:-Debug}"
MODE="${CONFIG_NAME:l}"

# Keep iOS dependencies isolated from desktop Xmake packages. Besides avoiding
# cross-project cache collisions, this guarantees every archive is compiled
# with the iOS 16 deployment target instead of the active SDK version.
export XMAKE_PKG_INSTALLDIR="${IOS_DIR}/.xmake/packages"

if [[ "${MODE}" != "debug" && "${MODE}" != "release" ]]; then
  MODE="release"
fi

ARCH_NAME="${CURRENT_ARCH:-arm64}"
if [[ "${ARCH_NAME}" == "undefined_arch" ]]; then
  ARCH_NAME="arm64"
fi
if [[ "${ARCH_NAME}" != "arm64" ]]; then
  print -u2 "CrossDesk Mobile currently supports physical iOS arm64 builds only."
  exit 64
fi

XMAKE_BIN="${XMAKE_BIN:-}"
if [[ -z "${XMAKE_BIN}" ]]; then
  XMAKE_BIN="$(command -v xmake 2>/dev/null || true)"
fi
if [[ -z "${XMAKE_BIN}" ]]; then
  for candidate in /opt/homebrew/bin/xmake /usr/local/bin/xmake "${HOME}/.local/bin/xmake"; do
    if [[ -x "${candidate}" ]]; then
      XMAKE_BIN="${candidate}"
      break
    fi
  done
fi
if [[ -z "${XMAKE_BIN}" || ! -x "${XMAKE_BIN}" ]]; then
  print -u2 "xmake is required. Install it from https://xmake.io first."
  exit 69
fi

# Xcode build phases export the iOS SDK, compiler and linker settings into the
# script environment. Letting Xmake inherit those variables also targets its
# build-machine tools (for example NASM and Meson helpers) at iOS, so they
# cannot run on the macOS build host. Give Xmake a clean host environment and
# pass the iOS target exclusively through its command-line configuration.
XMAKE_DEVELOPER_DIR="${DEVELOPER_DIR:-$(xcode-select -p)}"
XMAKE_TOOLCHAIN_BIN="${XMAKE_DEVELOPER_DIR}/Toolchains/XcodeDefault.xctoolchain/usr/bin"
run_xmake() {
  /usr/bin/env -i \
    HOME="${HOME}" \
    PATH="${XMAKE_TOOLCHAIN_BIN}:${PATH}" \
    TMPDIR="${TMPDIR:-/tmp}" \
    USER="${USER:-}" \
    LOGNAME="${LOGNAME:-${USER:-}}" \
    LANG="${LANG:-en_US.UTF-8}" \
    TERM="${TERM:-dumb}" \
    NO_COLOR="${NO_COLOR:-}" \
    DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" \
    XMAKE_PKG_INSTALLDIR="${XMAKE_PKG_INSTALLDIR}" \
    "${XMAKE_BIN}" "$@"
}

OUTPUT_DIR="${IOS_DIR}/Vendor/iphoneos/${CONFIG_NAME}"
OUTPUT_LIBRARY="${OUTPUT_DIR}/libCrossDeskMiniRTC.a"
MINIRTC_BUILD_DIR="${IOS_DIR}/.xmake/minirtc-build"
MINIRTC_LIBRARY="${MINIRTC_BUILD_DIR}/iphoneos/arm64/${MODE}/libminirtc.a"
CROSSDESK_SOURCE_DIR="${REPO_DIR}/src"
CROSSDESK_BUILD_DIR="${IOS_DIR}/.xmake/crossdesk-build/iphoneos/arm64/${MODE}"

mkdir -p "${OUTPUT_DIR}"

# Xmake stores the configured build directory relative to the process working
# directory. Xcode does not guarantee that directory for build phases, so keep
# configuration, compilation and inspection anchored to the MiniRTC project.
(
  cd "${MINIRTC_DIR}"
  run_xmake f -P "${MINIRTC_DIR}" -c -o "${MINIRTC_BUILD_DIR}" \
    -p iphoneos -a arm64 -m "${MODE}" \
    --as="${XMAKE_TOOLCHAIN_BIN}/clang" \
    --target_minver=16.0 --USE_CUDA=false -y
  run_xmake b -P "${MINIRTC_DIR}" minirtc
)

if [[ ! -f "${MINIRTC_LIBRARY}" ]]; then
  print -u2 "MiniRTC archive was not produced at ${MINIRTC_LIBRARY}."
  exit 66
fi

# Xmake owns the package hashes, so query its resolved target instead of
# embedding machine-specific ~/.xmake paths in the Xcode project.
TARGET_INFO="$(cd "${MINIRTC_DIR}" && TERM=dumb NO_COLOR=1 \
  run_xmake show -P "${MINIRTC_DIR}" -t minirtc)"
CLEAN_INFO="$(print -r -- "${TARGET_INFO}" | sed $'s/\033\\[[0-9;]*[[:alpha:]]//g')"
LINK_DIRS=("${(@f)$(print -r -- "${CLEAN_INFO}" | sed -nE 's|.*-> (/.*)/lib -> package.*|\1/lib|p' | sort -u)}")

REQUIRED_LINKS=(
  nice glib-2.0 gobject-2.0 gmodule-2.0 gio-2.0 gthread-2.0 intl
  ffi pcre2-8 pcre2-posix z ssl crypto srtp2 openfec opus yuv kcp
  datachannel usrsctp openh264 dav1d aom SvtAv1Enc
)
DEPENDENCY_ARCHIVES=()

for link_name in "${REQUIRED_LINKS[@]}"; do
  archive_path=""
  for link_dir in "${LINK_DIRS[@]}"; do
    candidate="${link_dir}/lib${link_name}.a"
    if [[ -f "${candidate}" ]]; then
      archive_path="${candidate}"
      break
    fi
  done
  if [[ -z "${archive_path}" ]]; then
    print -u2 "Unable to resolve static dependency lib${link_name}.a from Xmake."
    exit 66
  fi
  DEPENDENCY_ARCHIVES+=("${archive_path}")
done

# Build the CrossDesk message and file-transfer codecs for the same iOS
# architecture, then merge them into the aggregate archive consumed by Xcode.
NLOHMANN_HEADER="$(find "${XMAKE_PKG_INSTALLDIR}/n/nlohmann_json" \
  -path '*/include/nlohmann/json.hpp' -print -quit 2>/dev/null || true)"
if [[ -z "${NLOHMANN_HEADER}" ]]; then
  print -u2 "Unable to locate the nlohmann_json headers installed by Xmake."
  exit 66
fi
NLOHMANN_INCLUDE="${NLOHMANN_HEADER:h:h}"
IOS_SDK_PATH="$(xcrun --sdk iphoneos --show-sdk-path)"
mkdir -p "${CROSSDESK_BUILD_DIR}"

CROSSDESK_CXXFLAGS=(
  -std=c++17 -arch arm64 -isysroot "${IOS_SDK_PATH}"
  -miphoneos-version-min=16.0
  -I"${CROSSDESK_SOURCE_DIR}/device_controller"
  -I"${CROSSDESK_SOURCE_DIR}/common"
  -I"${CROSSDESK_SOURCE_DIR}/tools"
  -I"${NLOHMANN_INCLUDE}"
)
if [[ "${MODE}" == "debug" ]]; then
  CROSSDESK_CXXFLAGS+=(-O0 -g)
else
  CROSSDESK_CXXFLAGS+=(-O2 -DNDEBUG)
fi

CROSSDESK_SOURCES=(
  "${CROSSDESK_SOURCE_DIR}/device_controller/remote_action.cpp"
  "${CROSSDESK_SOURCE_DIR}/tools/file_transfer_protocol.cpp"
)
CROSSDESK_OBJECTS=()
for source_path in "${CROSSDESK_SOURCES[@]}"; do
  object_path="${CROSSDESK_BUILD_DIR}/${source_path:t:r}.o"
  xcrun --sdk iphoneos clang++ "${CROSSDESK_CXXFLAGS[@]}" \
    -c "${source_path}" -o "${object_path}"
  CROSSDESK_OBJECTS+=("${object_path}")
done

TEMP_LIBRARY="${OUTPUT_LIBRARY}.tmp"
rm -f "${TEMP_LIBRARY}"
/usr/bin/libtool -static -o "${TEMP_LIBRARY}" \
  "${MINIRTC_LIBRARY}" "${DEPENDENCY_ARCHIVES[@]}" \
  "${CROSSDESK_OBJECTS[@]}"
mv -f "${TEMP_LIBRARY}" "${OUTPUT_LIBRARY}"

print "Created ${OUTPUT_LIBRARY}"
