#!/bin/bash
set -u
export LC_ALL=C

# Migration is best effort. Installing the application must remain possible
# even when its old privacy records or the migration directory are unavailable.
if [[ $# -lt 3 ]]; then
    echo "CrossDesk permission migration skipped because installer arguments are missing." >&2
    exit 0
fi

APP_BUNDLE="${3%/}${2%/}/CrossDesk.app"
if [[ ! -d "$APP_BUNDLE" ]]; then
    exit 0
fi

BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
    "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null || true)"
case "$BUNDLE_ID" in
    cn.crossdesk.app|cn.crossdesk.CrossDesk|com.crossdesk.app|com.crossdesk.CrossDesk|com.kunkundi.crossdesk|com.kunkundi.CrossDesk)
        ;;
    *) exit 0 ;;
esac

if SIGNATURE="$(/usr/bin/codesign --display --verbose=4 "$APP_BUNDLE" 2>&1)"; then
    if ! /usr/bin/grep -Fxq 'Signature=adhoc' <<< "$SIGNATURE"; then
        # Keep any unfinished migration for users who have not launched yet.
        exit 0
    fi
elif [[ "$SIGNATURE" != *": code object is not signed at all" ]]; then
    echo "Could not identify the old CrossDesk signature. Preserving migration state." >&2
    exit 0
fi

STATE_DIR="${3%/}/Library/Application Support/CrossDesk"
if [[ "$EUID" -ne 0 || -L "$STATE_DIR" ]]; then
    echo "Could not safely record the CrossDesk permission migration." >&2
    exit 0
fi
if ! /bin/mkdir -p "$STATE_DIR" ||
   [[ "$(/usr/bin/stat -f '%u' "$STATE_DIR")" != 0 ]] ||
   ! /bin/chmod 755 "$STATE_DIR"; then
    echo "CrossDesk migration directory is unavailable. Use permission repair after installation." >&2
    exit 0
fi

TEMP_FILE="$(/usr/bin/mktemp "$STATE_DIR/.permission-migration.XXXXXX")" || exit 0
trap '/bin/rm -f "$TEMP_FILE"' EXIT
MIGRATION_ID="$(/usr/bin/uuidgen)" || exit 0
if /usr/bin/plutil -create xml1 "$TEMP_FILE" &&
   /usr/bin/plutil -insert schema -integer 1 "$TEMP_FILE" &&
   /usr/bin/plutil -insert migration_id -string "$MIGRATION_ID" "$TEMP_FILE" &&
   /usr/bin/plutil -insert legacy_bundle_id -string "$BUNDLE_ID" "$TEMP_FILE" &&
   /usr/bin/plutil -insert app_path -string "${2%/}/CrossDesk.app" "$TEMP_FILE" &&
   /usr/bin/plutil -convert json "$TEMP_FILE" &&
   /bin/chmod 644 "$TEMP_FILE" &&
   /bin/mv -f "$TEMP_FILE" "$STATE_DIR/permission-migration.json"; then
    echo "Recorded CrossDesk permission migration. Quit the old app and launch the new version to continue."
else
    echo "Could not record CrossDesk permission migration. Use permission repair after installation." >&2
fi

exit 0
