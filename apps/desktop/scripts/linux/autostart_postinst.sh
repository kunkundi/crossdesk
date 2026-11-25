#!/bin/sh
# dpkg supplies the previous version for upgrades. User startup entries must
# be written as their owners, including when apt runs without a desktop login.
[ "$1" = configure ] && [ -n "${2:-}" ] || exit 0
while IFS=: read -r account _ uid _ _ user_dir _; do
    [ "$uid" -gt 0 ] && [ -n "$user_dir" ] || continue
    [ -f "$user_dir/.cache/CrossDesk/config.ini" ] ||
        [ -f "$user_dir/.config/autostart/CrossDesk.desktop" ] || continue
    /usr/sbin/runuser -u "$account" -- /usr/bin/env -i HOME="$user_dir" PATH=/usr/bin:/bin \
        /usr/bin/crossdesk --autostart-migrate ||
        echo "CrossDesk will retry startup migration on the next launch for $account." >&2
done < /etc/passwd
exit 0
