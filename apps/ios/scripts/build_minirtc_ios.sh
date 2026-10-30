#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
IOS_DIR="${SCRIPT_DIR:h}"
REPO_DIR="${IOS_DIR:h:h}"
MINIRTC_DIR="${REPO_DIR}/deps/submodules/minirtc"
CONFIG_NAME="${CONFIGURATION:-Debug}"
MODE="${CONFIG_NAME:l}"
MINIRTC_ENABLE_AOM="${MINIRTC_ENABLE_AOM:-false}"
XMAKE_LOG_FLAGS=()
if [[ "${CROSSDESK_NATIVE_VERBOSE:-0}" == "1" ]]; then
  XMAKE_LOG_FLAGS=(-vD)
fi
if [[ "${MINIRTC_ENABLE_AOM}" != "true" && "${MINIRTC_ENABLE_AOM}" != "false" ]]; then
  print -u2 "MINIRTC_ENABLE_AOM must be true or false."
  exit 64
fi

# Keep iOS dependencies isolated from desktop Xmake packages. Besides avoiding
# cross-project cache collisions, this guarantees every archive is compiled
# with the iOS 16 deployment target instead of the active SDK version.
SDK_NAME="${PLATFORM_NAME:-iphoneos}"
APPLE_DEVICE="iphone"
CACHE_SUFFIX=""
if [[ "${SDK_NAME}" == "iphonesimulator" ]]; then
  APPLE_DEVICE="simulator"
  CACHE_SUFFIX="-simulator"
elif [[ "${SDK_NAME}" != "iphoneos" ]]; then
  print -u2 "Unsupported iOS SDK: ${SDK_NAME}"
  exit 64
fi
export XMAKE_PKG_INSTALLDIR="${IOS_DIR}/.xmake/packages${CACHE_SUFFIX}"

if [[ "${MODE}" != "debug" && "${MODE}" != "release" ]]; then
  MODE="release"
fi

ARCH_NAME="${CURRENT_ARCH:-arm64}"
if [[ "${ARCH_NAME}" == "undefined_arch" ]]; then
  ARCH_NAME="arm64"
fi
if [[ "${ARCH_NAME}" != "arm64" ]]; then
  print -u2 "CrossDesk Mobile supports arm64 devices and Apple Silicon simulators."
  exit 64
fi

# Post-clone scripts run in a separate process, so select their installed host
# tools again here instead of relying on exported PATH changes surviving.
if [[ "${CI_XCODE_CLOUD:-}" == "TRUE" ]]; then
  CLOUD_HOST_BIN="${IOS_DIR}/.xmake/xcode-cloud-host/bin"
  if [[ ! -x "${CLOUD_HOST_BIN}/xmake" || ! -x "${CLOUD_HOST_BIN}/python3" ]]; then
    print -u2 "Xcode Cloud build tools are missing. Run ci_scripts/ci_post_clone.sh before building."
    exit 69
  fi
  export PATH="${CLOUD_HOST_BIN}:${PATH}"
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
# Xcode also prepends Developer/usr/bin, whose Python 3.9 shadows the host
# Python selected by setup-python or the caller. Keep Apple's compiler first,
# but let host tools resolve from the original non-Xcode PATH entries.
XMAKE_HOST_PATH="${XMAKE_TOOLCHAIN_BIN}"
for host_bin in "${(@s/:/)PATH}"; do
  if [[ "${host_bin}" != "${XMAKE_DEVELOPER_DIR}/"* ]]; then
    XMAKE_HOST_PATH+=":${host_bin}"
  fi
done
run_xmake() {
  # Match desktop repository selection: use Xmake defaults unless explicitly
  # overridden by the caller. Only compiled iOS packages use a separate cache.
  local xmake_env=()
  if [[ -n "${XMAKE_GLOBALDIR:-}" ]]; then
    xmake_env+=("XMAKE_GLOBALDIR=${XMAKE_GLOBALDIR}")
  fi
  if [[ -n "${XMAKE_MAIN_REPO:-}" ]]; then
    xmake_env+=("XMAKE_MAIN_REPO=${XMAKE_MAIN_REPO}")
  fi
  # Cloud dependency downloads use Apple's HTTP proxy. Keep those settings
  # while still stripping Xcode's iOS compiler and SDK environment variables.
  if [[ "${CI_XCODE_CLOUD:-}" == "TRUE" ]]; then
    local proxy_name
    for proxy_name in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
      if (( ${+parameters[$proxy_name]} )); then
        xmake_env+=("${proxy_name}=${(P)proxy_name}")
      fi
    done
  fi
  /usr/bin/env -i \
    HOME="${HOME}" \
    PATH="${XMAKE_HOST_PATH}" \
    TMPDIR="${TMPDIR:-/tmp}" \
    USER="${USER:-}" \
    LOGNAME="${LOGNAME:-${USER:-}}" \
    LANG="${LANG:-en_US.UTF-8}" \
    TERM="${TERM:-dumb}" \
    NO_COLOR="${NO_COLOR:-}" \
    DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" \
    XMAKE_PKG_INSTALLDIR="${XMAKE_PKG_INSTALLDIR}" \
    CROSSDESK_SOURCE_DIR="${REPO_DIR}" \
    "${xmake_env[@]}" \
    "${XMAKE_BIN}" "$@"
}

OUTPUT_DIR="${IOS_DIR}/Vendor/${SDK_NAME}/${CONFIG_NAME}"
OUTPUT_LIBRARY="${OUTPUT_DIR}/libCrossDeskMiniRTC.a"
mkdir -p "${OUTPUT_DIR}"
run_xmake --version > "${OUTPUT_DIR}/xmake-version.txt"
PATH="${XMAKE_HOST_PATH}" python3 -B "${SCRIPT_DIR}/verify_native_build.py" \
  --xmake-version "${OUTPUT_DIR}/xmake-version.txt"
MINIRTC_BUILD_DIR="${IOS_DIR}/.xmake/minirtc-build${CACHE_SUFFIX}"
MINIRTC_LIBRARY="${MINIRTC_BUILD_DIR}/iphoneos/arm64/${MODE}/libminirtc.a"
WIRE_BUILD_DIR="${IOS_DIR}/.xmake/wire-build${CACHE_SUFFIX}"
WIRE_WORK_DIR="${IOS_DIR}/.xmake/wire-work${CACHE_SUFFIX}"
WIRE_MANIFEST_FILE="${WIRE_WORK_DIR}/xmake.lua"
WIRE_LIBRARY="${WIRE_BUILD_DIR}/iphoneos/arm64/${MODE}/libcrossdesk_wire.a"

mkdir -p "${OUTPUT_DIR}" "${WIRE_WORK_DIR}"
if ! cmp -s "${REPO_DIR}/xmake.lua" "${WIRE_MANIFEST_FILE}"; then
  cp "${REPO_DIR}/xmake.lua" "${WIRE_MANIFEST_FILE}"
fi

# Reuse Xmake's compiler/dependency probes unless the build environment changes.
# Still configure on every invocation: Xmake must see manifest edits and restore
# the iOS configuration if MiniRTC was built standalone for another platform.
CONFIG_SIGNATURE="$(print -r -- "${XMAKE_BIN}" "${XMAKE_DEVELOPER_DIR}" \
  "${XMAKE_HOST_PATH}" \
  "${MODE}" "${ARCH_NAME}" "${SDK_NAME}" "${APPLE_DEVICE}" "16.0" "${XMAKE_PKG_INSTALLDIR}" "${MINIRTC_ENABLE_AOM}" "${XMAKE_GLOBALDIR:-}" "${XMAKE_MAIN_REPO:-}"
  run_xmake --version | sed -n '1p'
  DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" xcodebuild -version
  DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" xcrun --sdk "${SDK_NAME}" --show-sdk-path
  DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" xcrun --sdk "${SDK_NAME}" --show-sdk-version)"

configure_xmake() {
  local project_dir="$1"
  local build_dir="$2"
  shift 2
  local stamp="${build_dir}/ios-config-signature"
  local clean_flags=()
  if [[ ! -f "${stamp}" || "$(<"${stamp}")" != "${CONFIG_SIGNATURE}" ]]; then
    clean_flags=(-c)
  fi
  print "Configuring iOS native project: ${project_dir}"
  run_xmake f -P "${project_dir}" "${clean_flags[@]}" "${XMAKE_LOG_FLAGS[@]}" -o "${build_dir}" \
    -p iphoneos -a arm64 --appledev="${APPLE_DEVICE}" -m "${MODE}" --target_minver=16.0 \
    --policies=package.precompiled:n -y "$@"
  mkdir -p "${build_dir}"
  print -r -- "${CONFIG_SIGNATURE}" > "${stamp}"
}

# Xmake stores the configured build directory relative to the process working
# directory. Xcode does not guarantee that directory for build phases, so keep
# configuration, compilation and inspection anchored to the MiniRTC project.
(
  cd "${MINIRTC_DIR}"
  configure_xmake "${MINIRTC_DIR}" "${MINIRTC_BUILD_DIR}" \
    --as="${XMAKE_TOOLCHAIN_BIN}/clang" --USE_CUDA=false \
    --MINIRTC_ENABLE_AOM="${MINIRTC_ENABLE_AOM}"
  print "Building MiniRTC for ${SDK_NAME} (${MODE})"
  run_xmake b -P "${MINIRTC_DIR}" "${XMAKE_LOG_FLAGS[@]}" minirtc
)

if [[ ! -f "${MINIRTC_LIBRARY}" ]]; then
  print -u2 "MiniRTC archive was not produced at ${MINIRTC_LIBRARY}."
  exit 66
fi

# Build the platform-independent wire archive from the root project's target.
# The copied manifest only isolates Xmake's generated configuration; there is
# no second project definition to maintain.
(
  cd "${WIRE_WORK_DIR}"
  configure_xmake "${WIRE_WORK_DIR}" "${WIRE_BUILD_DIR}"
  print "Building CrossDesk wire for ${SDK_NAME} (${MODE})"
  run_xmake b -P "${WIRE_WORK_DIR}" "${XMAKE_LOG_FLAGS[@]}" crossdesk_wire
)

if [[ ! -f "${WIRE_LIBRARY}" ]]; then
  print -u2 "CrossDesk wire archive was not produced at ${WIRE_LIBRARY}."
  exit 66
fi

# Xmake owns the package hashes, so query its resolved target instead of
# embedding machine-specific ~/.xmake paths in the Xcode project.
TARGET_INFO="$(cd "${MINIRTC_DIR}" && TERM=dumb NO_COLOR=1 \
  run_xmake show -P "${MINIRTC_DIR}" -t minirtc)"
CLEAN_INFO="$(print -r -- "${TARGET_INFO}" | sed $'s/\033\\[[0-9;]*[[:alpha:]]//g')"
LINK_DIRS=("${(@f)$(print -r -- "${CLEAN_INFO}" | sed -nE 's|.*-> (/.*)/lib -> package.*|\1/lib|p' | sort -u)}")

print -r -- "${CLEAN_INFO}" > "${OUTPUT_DIR}/minirtc-target-info.txt"

REQUIRED_LINKS=(
  nice glib-2.0 gobject-2.0 gmodule-2.0 gio-2.0 gthread-2.0 intl
  ffi pcre2-8 pcre2-posix z ssl crypto srtp2 openfec opus yuv kcp
  datachannel usrsctp dav1d SvtAv1Enc
)
if [[ "${MINIRTC_ENABLE_AOM}" == "true" ]]; then
  REQUIRED_LINKS+=(aom)
fi
DEPENDENCY_ARCHIVES=()
MERGE_ARCHIVES=()

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
  # OpenSSL is linked separately as a static framework with its SDK privacy
  # resources. Keep both archives in the input manifest for source validation.
  if [[ "${link_name}" != "ssl" && "${link_name}" != "crypto" ]]; then
    MERGE_ARCHIVES+=("${archive_path}")
  fi
done

TEMP_LIBRARY="${OUTPUT_LIBRARY}.tmp"
MERGE_MANIFEST="${OUTPUT_LIBRARY}.inputs.sha256"
TEMP_MANIFEST="${MERGE_MANIFEST}.tmp"
trap 'rm -f "${TEMP_LIBRARY}" "${TEMP_MANIFEST}"' EXIT

# Check the actual archive, including when reusing cached build products.
verify_no_openh264() {
  local symbols
  symbols="$(DEVELOPER_DIR="${XMAKE_DEVELOPER_DIR}" xcrun nm -g "$1")"
  if [[ "${symbols}" == *WelsCreateDecoder* || \
        "${symbols}" == *WelsCreateSVCEncoder* || \
        "${symbols}" == *WelsGetCodecVersion* ]]; then
    print -u2 "OpenH264 symbols must not be present in the iOS native archive: $1"
    exit 65
  fi
}

# Hash contents as well as paths so same-size archives replaced within the same
# timestamp tick cannot leave a stale merged library. Preserve the output mtime
# on no-op builds to avoid relinking the Xcode app.
shasum -a 256 "${SCRIPT_DIR}/build_minirtc_ios.sh" "${MINIRTC_LIBRARY}" \
  "${DEPENDENCY_ARCHIVES[@]}" "${WIRE_LIBRARY}" > "${TEMP_MANIFEST}"
if [[ -f "${OUTPUT_LIBRARY}" ]] && cmp -s "${TEMP_MANIFEST}" "${MERGE_MANIFEST}"; then
  verify_no_openh264 "${OUTPUT_LIBRARY}"
  print "Up to date: ${OUTPUT_LIBRARY}"
  exit 0
fi

/usr/bin/libtool -static -o "${TEMP_LIBRARY}" \
  "${MINIRTC_LIBRARY}" "${MERGE_ARCHIVES[@]}" \
  "${WIRE_LIBRARY}"
verify_no_openh264 "${TEMP_LIBRARY}"
mv -f "${TEMP_LIBRARY}" "${OUTPUT_LIBRARY}"
mv -f "${TEMP_MANIFEST}" "${MERGE_MANIFEST}"

print "Created ${OUTPUT_LIBRARY}"
