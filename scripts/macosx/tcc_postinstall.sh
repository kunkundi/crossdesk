#!/bin/bash
set -e

APP_IDENTIFIER="cn.crossdesk.app"

# Keep known historical identifiers here. tccutil only resets identifiers that
# Launch Services can currently resolve, so path/db cleanup below remains a
# best-effort fallback for stale entries from unsigned or removed builds.
BUNDLE_IDENTIFIERS=(
    "cn.crossdesk.app"
    "cn.crossdesk.CrossDesk"
    "com.crossdesk.app"
    "com.crossdesk.CrossDesk"
    "com.kunkundi.crossdesk"
    "com.kunkundi.CrossDesk"
)

TCC_SERVICES=(
    "ScreenCapture"
    "Accessibility"
    "Microphone"
    "AudioCapture"
)

run_tccutil() {
    local user_name="$1"
    local user_id="$2"
    local service="$3"
    local bundle_id="$4"

    if [ -n "$user_name" ] && [ -n "$user_id" ]; then
        /bin/launchctl asuser "$user_id" \
            /usr/bin/sudo -u "$user_name" \
            /usr/bin/tccutil reset "$service" "$bundle_id" >/dev/null 2>&1
    else
        /usr/bin/tccutil reset "$service" "$bundle_id" >/dev/null 2>&1
    fi
}

reset_bundle_tcc() {
    local user_name="$1"
    local user_id="$2"
    local bundle_id
    local service

    for bundle_id in "${BUNDLE_IDENTIFIERS[@]}"; do
        if run_tccutil "$user_name" "$user_id" "All" "$bundle_id"; then
            continue
        fi

        for service in "${TCC_SERVICES[@]}"; do
            run_tccutil "$user_name" "$user_id" "$service" "$bundle_id" || true
        done
    done
}

cleanup_tcc_db() {
    local db_path="$1"

    if [ ! -f "$db_path" ] || ! command -v sqlite3 >/dev/null 2>&1; then
        return
    fi

    /usr/bin/sqlite3 "$db_path" <<'SQL' >/dev/null 2>&1 || true
DELETE FROM access
WHERE service IN (
  'kTCCServiceScreenCapture',
  'kTCCServiceAccessibility',
  'kTCCServiceMicrophone',
  'kTCCServiceAudioCapture'
)
AND (
  client IN (
    'cn.crossdesk.app',
    'cn.crossdesk.CrossDesk',
    'com.crossdesk.app',
    'com.crossdesk.CrossDesk',
    'com.kunkundi.crossdesk',
    'com.kunkundi.CrossDesk'
  )
  OR lower(client) LIKE '%crossdesk%'
);
SQL
}

cleanup_user_tcc_db() {
    local user_name="$1"
    local home_dir

    home_dir=$(/usr/bin/dscl . -read "/Users/${user_name}" NFSHomeDirectory 2>/dev/null |
        /usr/bin/awk '{print $2}')
    if [ -z "$home_dir" ]; then
        return
    fi

    cleanup_tcc_db "${home_dir}/Library/Application Support/com.apple.TCC/TCC.db"
}

CONSOLE_USER=$(/usr/bin/stat -f "%Su" /dev/console 2>/dev/null || true)
if [ -n "$CONSOLE_USER" ] &&
   [ "$CONSOLE_USER" != "root" ] &&
   [ "$CONSOLE_USER" != "loginwindow" ]; then
    CONSOLE_UID=$(/usr/bin/id -u "$CONSOLE_USER" 2>/dev/null || true)
    reset_bundle_tcc "$CONSOLE_USER" "$CONSOLE_UID"
    cleanup_user_tcc_db "$CONSOLE_USER"
fi

# Also clear any system/root-scoped decisions as a harmless fallback.
reset_bundle_tcc "" ""
cleanup_tcc_db "/Library/Application Support/com.apple.TCC/TCC.db"

exit 0
