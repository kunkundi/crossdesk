#!/bin/bash

set -euo pipefail

# The shared CrossDeskMobile scheme archives Release for physical iOS devices.
# Leave other actions to their existing Xcode build phase and SDK selection.
if [[ "${CI_XCODEBUILD_ACTION:-}" != "archive" || "${CI_PRODUCT_PLATFORM:-}" != "iOS" ]]; then
    echo "Skipping native prebuild for ${CI_XCODEBUILD_ACTION:-unknown} (${CI_PRODUCT_PLATFORM:-unknown})"
    exit 0
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
IOS_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="${IOS_DIR}/.xmake/xcode-cloud-logs"
mkdir -p "$LOG_DIR"

collect_dependency_logs() {
    local log_root="$1"
    local label="$2"
    local log_file relative_path destination
    [[ -d "$log_root" ]] || return 0

    while IFS= read -r -d '' log_file; do
        relative_path="${log_file#"${log_root}/"}"
        destination="${LOG_DIR}/dependencies/${label}/${relative_path}"
        mkdir -p "$(dirname "$destination")"
        cp "$log_file" "$destination"

        # Cloud does not publish arbitrary files as downloadable artifacts.
        # Include GLib's complete install log in the retained step output, plus
        # the tail of Meson and other failed package logs for context.
        if [[ "$log_file" == */glib/*/logs/install.txt ]]; then
            echo "GLib install log: ${log_file}"
            cat "$log_file"
        elif [[ "$log_file" == */installdir.failed/logs/* || \
                "$log_file" == */glib/*/meson-logs/meson-log.txt ]]; then
            echo "Last 200 lines: ${log_file}"
            tail -n 200 "$log_file"
        fi
    done < <(find "$log_root" -type f \( \
        -path '*/logs/*.txt' -o -path '*/meson-logs/meson-log.txt' \) -print0)
}

MONITOR_PID=""
finish() {
    local build_exit_code=$?
    trap - EXIT
    set +e
    if [[ -n "$MONITOR_PID" ]]; then
        kill "$MONITOR_PID" 2>/dev/null
        wait "$MONITOR_PID" 2>/dev/null
    fi
    if [[ "$build_exit_code" -ne 0 ]]; then
        echo "iOS native prebuild failed with exit code ${build_exit_code}"
        collect_dependency_logs "${XMAKE_GLOBALDIR:-${HOME}}/.xmake/cache/packages" cache
        collect_dependency_logs "${IOS_DIR}/.xmake/packages" installed
        echo "Native build diagnostics: ${LOG_DIR}"
    fi
    exit "$build_exit_code"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Run outside xcodebuild so downloads and compiler output are streamed instead
# of buffered by a Run Script phase. Keep the step active during quiet commands.
(
    SLEEP_PID=""
    trap 'kill "${SLEEP_PID}" 2>/dev/null || true; wait "${SLEEP_PID}" 2>/dev/null || true; exit 0' TERM INT
    while true; do
        sleep 60 &
        SLEEP_PID=$!
        wait "$SLEEP_PID" || exit 0
        echo "iOS native prebuild still running at $(date -u +%FT%TZ)"
        ps -axo pid,ppid,state,etime,pcpu,comm | \
            awk 'NR == 1 || /xmake|clang|cmake|ninja|meson|python|git|curl|make|zsh/' || true
    done
) &
MONITOR_PID=$!

echo "Prebuilding iOS native dependencies for Release / iphoneos / arm64"
CONFIGURATION=Release PLATFORM_NAME=iphoneos CURRENT_ARCH=arm64 \
    CROSSDESK_NATIVE_VERBOSE=1 \
    "${IOS_DIR}/scripts/build_minirtc_ios.sh" 2>&1 | tee "${LOG_DIR}/ios-native-build.log"
echo "iOS native prebuild finished; Archive will reuse the native build products"
