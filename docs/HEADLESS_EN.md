# Linux headless operation

[README](../README_EN.md) · [中文](HEADLESS.md)

CrossDesk can reuse an existing X11 desktop or create a separate Xvfb desktop on a Linux host without a monitor. Capture, input, cursor and clipboard use the same display.

## Preserve the existing desktop and applications

Start CrossDesk normally. It automatically discovers and selects your existing desktop, preserving its Dock, panel, wallpaper and open applications. No display number or `DISPLAY` configuration is required:

```bash
crossdesk
# Source build:
xmake r crossdesk
```

With multiple desktops, selection prefers the startup environment's desktop, then the current login session, the active primary desktop, other active desktops, and online desktops. Equal-priority desktops use a stable display order, also used when systemd/logind metadata is unavailable. Inaccessible sessions and bare Xvfb displays without a window manager are excluded from existing-desktop selection.

Reusing a desktop does not require Xvfb. Exiting CrossDesk does not terminate that desktop or its applications. A locked session initially shows the system lock screen; unlock it with that user's system password.

Discovery combines the current user's login-session metadata with their X11 sockets and verifies a live window manager. It works without systemd/logind as well. It respects `XAUTHORITY` or Xlib's default location when unset, and can try the user's GDM authority file. Other users' desktops and login greeters are not automatically selected.

`--headless` explicitly requests automatic selection, for example when the shell has stale Wayland environment variables. The display override is retained only for advanced use and debugging:

```bash
crossdesk --headless-display :10  # Example display, not needed for normal startup.
```

The override fails if the specified desktop is inaccessible or has no window manager; it never substitutes a blank Xvfb.

Do not add `--headless-size` or `--headless-session` to reuse an existing desktop: those options create a separate Xvfb. The existing display server controls its resolution.

## Terminal console

Headless mode keeps routine logs in files and displays the device ID, connection password, online status and command results in the terminal. SSH launches without a graphical environment enable this automatically; to request it explicitly:

```bash
xmake r crossdesk --headless
```

Enter commands in that terminal while CrossDesk is running:

| Command | Action |
| --- | --- |
| `status` | Show the device ID, current password and connection state |
| `settings` | Open the numbered settings menu; `back` returns |
| `settings show` | Show settings without entering the menu |
| `settings set KEY VALUE` | Change a setting directly |
| `password` | Prompt for a new password; six ASCII letters or digits, or `cancel` |
| `password Ab12Cd` | Submit a password directly (example only) |
| `random` | Generate and submit a random password |
| `help` | Print command help |
| `quit` | Exit CrossDesk, preserving a reused desktop |

The menu shares the GUI's `config.ini`. Each valid change is saved immediately; `cancel` discards the value being edited. Console status, menus, password messages and help follow the saved Chinese, English or Russian language setting. Command names remain the same.

| Key | Values / format | When applied |
| --- | --- | --- |
| `language` | `zh-CN` / `en-US` / `ru-RU` | Immediately, including the GUI; retained on restart |
| `codec` | `h264` / `av1` | Reconnect after saving |
| `hardware` | `on` / `off` | Reconnect; enabling requires a build with hardware codec support |
| `turn` | `off` / `auto` / `udp` / `tcp` | Reconnect; UDP/TCP force relay use |
| `server_host` | Hostname or IP without URL scheme/path | Reconnect if self-hosting is enabled; otherwise saved for later |
| `server_port` | `1`–`65535` | Same as server host |
| `self_hosted` | `on` / `off` | Configure host/port first, then enable; reconnect |
| `privacy` | `on` / `off` | Automatic privacy screen on subsequent connections |
| `file_path` | Existing writable absolute directory, or `default` | Subsequent received files; spaces need no extra quotes |
| `autostart` | `on` / `off` | Next graphical desktop login, not a system boot service |
| `daemon` | `on` / `off` | Next normal-mode start; the headless console remains in the foreground |

Example commands inside the running console:

```text
settings
1
en-US
back
settings set codec av1
settings set file_path /home/user/Received Files
```

Connection settings cannot change while a remote session is connected/connecting or a password change is unresolved. Switching signaling servers may change the device identity; use `status` after reconnection. Quality/frame rate are controlled by the viewer, and headless Linux capture uses X11; those options are not additional mutable global settings.

Password changes require a signaling connection. The console reports submission, server confirmation and reconnection, using the GUI's existing persistence and timeout recovery flow. Invalid input, offline changes and pending requests produce concise messages. A password change reconnects the host.

Startup prints the existing log location. Logs retain their original `crossdesk-YYYYMMDD-HHMMSS.log` and `minirtc-YYYYMMDD-HHMMSS.log` names and rotation policy, with no extra headless log file. Bootstrap diagnostics use the CrossDesk logger; terminal log copies and direct third-party stdout/stderr output are silenced. Credential display goes directly to the terminal, not through the logger. Noninteractive output (services or redirection) hides the plaintext password. End-of-input does not terminate a background service.

The interactive console stays in the foreground regardless of the built-in daemon setting. Exit any older instance and start the new build to use these commands.

## Create a separate virtual desktop

Debian/Ubuntu packages require Xvfb, Xfce and the D-Bus session tools through `Depends`. APT installs them even with `--no-install-recommends`. Source builds need the same runtime prerequisites in the development environment; the project build image and CI include them.

For a host without a desktop session, or to start an independent Xfce session (package users do not need to reinstall the dependencies):

```bash
sudo apt install xvfb xfce4 dbus
crossdesk --headless-size 1920x1080 --headless-session startxfce4
```

The default size is 1920×1080; dimensions must be even, between 320 and 8192. `--headless-session` accepts an executable name or path and runs it under a private `dbus-run-session`. Use an executable script ending in `exec` if arguments are required.

For capture debugging, explicitly request a bare display:

```bash
sudo apt install xvfb
crossdesk --headless-session none
```

Bare Xvfb only provides a screen. It has no Dock, panel or existing application windows. Specifying only `--headless-size` also creates a bare display and records a diagnostic in the log. This explains a screen that shows CrossDesk against a black background.

## Selection and limits

- Ordinary graphical launches retain the supplied X11 or Wayland session. X11 root capture works without active RandR outputs or a RandR extension.
- `--headless`, or startup without an accessible X11 display or advertised Wayland session, automatically selects an existing desktop before falling back to Xvfb. Bare Xvfb fallback explicitly reports that it has no desktop environment or existing apps.
- `--headless-display` cannot be combined with `--headless-size` or `--headless-session`.
- `--no-headless` disables automatic discovery/fallback; `--headless-help` prints usage.
- Applications are not migrated between sessions. Creating Xvfb cannot recover applications from an unavailable graphical session.

## Run as a service

When creating a private display, the foreground supervisor owns Xvfb, the optional desktop session and CrossDesk. Application exit, SIGINT/SIGTERM/SIGHUP, or a monitored process exiting tears down that session's child processes and temporary authorization file. Xvfb allocates an unused display number, disables TCP listening and requires a random authentication cookie. The authority file is private to the current user. Startup diagnostic logs record `DISPLAY` and `XAUTHORITY`; use them from a terminal as the same user to launch additional applications on the virtual desktop.

Example `~/.config/systemd/user/crossdesk-headless.service` (adjust the executable path):

```ini
[Unit]
Description=CrossDesk virtual desktop

[Service]
Type=simple
ExecStart=/usr/bin/crossdesk --headless-size 1920x1080 --headless-session startxfce4
Restart=on-failure
RestartSec=5
KillMode=mixed
TimeoutStopSec=15

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now crossdesk-headless.service
journalctl --user -u crossdesk-headless.service -f
```

To reuse an existing desktop instead, set `ExecStart=/usr/bin/crossdesk`; discovery and selection are automatic, but a graphical session must already be logged in. An administrator can enable a user service before login with `sudo loginctl enable-linger USERNAME`. The built-in daemon setting is bypassed when creating a private Xvfb; systemd can restart the whole session. Save work before stopping the service, which also closes applications on the virtual desktop.

## Runtime limitations

The GUI uses software rendering and capture uses X11 → NV12. Video encoding follows the configured codec and available encoders. Audio requires an available user audio service; Xvfb does not provide a sound device. This implementation targets Linux. For Windows hosts without monitors, see the [FAQ](FAQ.md).
