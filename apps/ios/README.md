# CrossDesk for iPhone and iPad

[Project home](../../README_EN.md) · [User guide](../../docs/USER_GUIDE_EN.md#ios) · [FAQ](../../docs/FAQ.md#english)

Use your iPhone or iPad to control a remote computer. The mobile app is a controller; it cannot share the iPhone or iPad as a remote desktop.

## Install and connect

1. Install from the [App Store](https://apps.apple.com/app/id6828859211). Check the listing for supported iOS versions.
2. Run CrossDesk on the computer and have its 9-digit device ID and connection password ready.
3. Read the privacy notice in the mobile app and choose whether to allow network access. Enter the computer’s ID, connect, and enter its password.
4. Save the password if you want to reconnect from recent devices without entering it each time.

For a self-hosted server, configure the same server and port on both devices. See [self-hosting](../../docs/SELF_HOSTING_EN.md).

## Touch, mouse, and keyboard

Choose **Relative Position** in Settings to use the screen like a trackpad, or **Absolute Position** to point directly at the touched location. Pinch to zoom and use two fingers to pan the enlarged view. Open the floating menu for the virtual mouse, keyboard, display selection, sound, and video settings.

The system keyboard supports your installed input methods, including candidates, emoji, and dictation. Text is sent after you confirm it. Remote text input depends on the host version and platform; use Clipboard when a character is not supported. **Computer keyboard** provides individual keys and shortcuts.

**More** offers Common, Keys, System, and Mine groups. Copy/Paste follows the remote platform: Ctrl on Windows/Linux and Cmd on macOS. Linux system shortcuts require choosing GNOME or KDE Plasma. You can save up to 16 custom combinations and pin up to 8 shortcuts for each remote platform.

On iPhone, enable **Settings → Screen orientation → Lock landscape after connecting** to keep sessions in landscape. It is off by default. iPad supports rotation and resizable multitasking windows; resizing preserves the active session.

## Video, audio, and privacy screen

Use the floating menu to switch monitors, change video quality or frame rate, and toggle remote audio. Available choices depend on the host. Lower video settings if playback is slow or the device becomes warm.

**Privacy screen** toggles the computer’s local cover and blocks its local keyboard and mouse when supported. The button shows the host-confirmed state; errors appear in the menu. Automatic activation also requires enabling the corresponding setting on the computer.

## Clipboard and files

Use **Clipboard** in the floating menu to send local text to the computer, up to 128 KiB per transfer. The app can also receive remote text into the iOS clipboard.

Select files from the floating menu to send them. Received files are stored in the app’s `Documents/Received` folder; use the share action beside the transfer status to export them. Wait for a transfer to finish before disconnecting.

## Switching apps and Picture in Picture

After remote video appears, switching apps starts Picture in Picture when the device and system settings allow it. Return from the small window to continue the same session.

Background connectivity depends on active Picture in Picture or remote audio playback. Pausing or closing Picture in Picture, disabling automatic Picture in Picture, locking the phone, losing the network, or force-quitting can interrupt the session. A connection is not guaranteed to remain active indefinitely in the background.

## Trusted devices

Open **Settings → This device’s pairing info**, copy the pairing information, and import it under **Manage trusted devices → Add a trusted device** on the computer. Compare fingerprints before trusting the device. You can then connect with **Only allow trusted devices** enabled; the connection password is still required.

Restarting, clearing caches, or changing servers preserves the pairing identity. A different iPhone or iPad needs its own pairing. If the app cannot read its stored identity, follow the error message rather than assuming the computer’s password is incorrect.

## Privacy, language, and updates

**Settings → Privacy → Privacy and Authorization** lets you read the policy or withdraw consent. Withdrawal disconnects the session. Remote desktop previews are off by default; enabling them saves local previews from subsequent connections. Turning saving off or withdrawing consent clears them.

The app follows the system or per-app language setting. Supported languages are listed in the [language guide](../../localization/README.md). Interface language does not translate the remote computer’s contents.

Update through the App Store. About provides offline licenses and source information, including the OpenFEC notice and component terms. CrossDesk uses [GPL-3.0-only](../../LICENSE); dependencies retain their own licenses. See the [open-source rights notice](licenses/OPEN_SOURCE_NOTICE.txt). A self-signed build must be updated using a compatible signing identity.

## Build and sign your own copy (optional)

Skip this section if you installed from the App Store. Source builds require a Mac with Xcode 16+, Xmake 3.1.1+, CMake, and Python 3 with setuptools/wheel. The source target requires iOS 16+ and an arm64 device, or an iOS simulator on Apple Silicon.

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

For an existing checkout, run `git submodule update --init --recursive`. To reproduce a released build, use the source tag or commit listed in the app’s About page.

1. Open `apps/ios/CrossDeskMobile.xcodeproj` and select the `CrossDeskMobile` scheme.
2. Under **Signing & Capabilities**, select your own team and a bundle identifier available to that team. The checked-in team and `com.chunkdash.crossdesk` identifier belong to ChunkDash.
3. Select your connected iPhone or iPad, complete device trust and Developer Mode prompts, and run the app.

The first build downloads and compiles native dependencies; later builds reuse them. Ensure Xmake is on PATH, or set `XMAKE_BIN` to its full path.

An unsigned build can be generated from the repository root:

```sh
xcodebuild -project apps/ios/CrossDeskMobile.xcodeproj \
  -scheme CrossDeskMobile -configuration Debug \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO build
```

This output, like an unsigned GitHub Actions artifact, needs signing before it can be installed on a device. The App Store is the direct installation option.

For an Apple Silicon simulator:

```sh
xcodebuild -project apps/ios/CrossDeskMobile.xcodeproj \
  -scheme CrossDeskMobile -configuration Debug \
  -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  CODE_SIGNING_ALLOWED=YES CODE_SIGN_IDENTITY=- build
```

Keep the simulator’s local ad-hoc signing enabled so Keychain can store the app’s identity. Disabling signing can cause error `-34018` before connection. Simulator behavior does not establish physical-device video performance or background reliability.

## Troubleshooting

Check the [FAQ](../../docs/FAQ.md#english) for connection failures, blank video, and Windows lock-screen input. When reporting a problem, include both client versions, the iPhone/iPad model and iOS version, the error, and the steps to reproduce it.
