# CrossDesk Mobile (native iOS)

This target is a native MiniRTC controller. It uses the same WebSocket
signaling, libnice ICE, SRTP/RTP and data-stream protocol as the desktop app.
There is no `WKWebView` or browser runtime.

## Requirements

- Xcode 16 or newer
- Xmake available on `PATH`
- A physical arm64 iPhone or iPad running iOS 16 or newer

## Build

Clone the repository with its submodules. Run these commands from the parent
directory where you want to place the checkout:

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

For an existing checkout, run `git submodule update --init --recursive` from
the repository root. All remaining commands below use that root directory.

1. Open `apps/ios/CrossDeskMobile.xcodeproj` and select the `CrossDeskMobile`
   target and scheme.
2. Under **Signing & Capabilities**, select your own development team and set
   a bundle identifier available to that team. The checked-in signing team
   and `cn.crossdesk.mobile` identifier belong to the project maintainer.
3. Connect and select a physical iPhone or iPad, configure device trust and
   Developer Mode as prompted by Xcode, then build and run. See Apple's
   [device setup guide](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices).

The first build phase builds MiniRTC and the CrossDesk wire library, downloads
and builds their dependencies, then merges the iPhoneOS archives into a local library under
`Vendor/`. This build path targets physical devices; simulator builds are
not supported. The exact tool versions used by CI are recorded in the
[workflow](../../.github/workflows/build.yml).

You can also build the target without code signing. The resulting app, like
the unsigned CI artifact, requires signing before device installation:

```sh
xcodebuild -project apps/ios/CrossDeskMobile.xcodeproj \
  -scheme CrossDeskMobile \
  -configuration Debug \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO build
```

The first connection to a signaling server provisions and stores an identity
for that server. Remote control sessions then log in as `C-<identity>` and use
the desktop-compatible `DisplayN`, `control_audio`, `mouse`, `keyboard`,
`control_data`, `clipboard`, `file`, and `file_feedback` streams.

## Video codecs

Every iOS build includes VideoToolbox, dav1d, and SVT-AV1. H.264 uses
VideoToolbox exclusively; OpenH264 is excluded from the iOS dependencies and
native archive. The hardware/software processing setting has been removed,
and preferences saved by older versions are no longer read. H.264 codec
initialization failure is reported without a software fallback.
AV1 encoding uses SVT-AV1 and AV1 decoding uses dav1d; VideoToolbox AV1 hardware
decoding is currently unsupported. Desktop builds retain OpenH264.

The native build script checks the merged archive for OpenH264 entry points
and rejects a build if they are present, including in cached archives.

The unused libaom backends are excluded by default. To include them for development,
set the `MINIRTC_ENABLE_AOM` environment variable (or Xcode user-defined build setting)
to `true`. This includes libaom in the merged native archive without changing the
AV1 factories' choice of SVT-AV1 and dav1d.

## Physical-device test checklist

Run the app from Xcode on a physical device and connect to a current desktop
build. Test the features in this order so a media problem is not confused with
a data-channel problem:

1. **Video:** after the session connects, the waiting panel should be replaced
   by the remote desktop. Tap the floating icon to see the connection state,
   P2P/TURN mode, decoded resolution, and bitrate. The current menu does not
   display a frame counter.
2. **Audio:** play continuous sound on the remote computer, then toggle the
   speaker button. Audio is Opus-decoded by MiniRTC and played as 48 kHz mono
   16-bit PCM through `AVAudioEngine`.
3. **Clipboard:** copy a short text value on the remote desktop, then paste it
   into a text field on the phone to verify reception. Text is limited to
   128 KiB. The bridge supports sending local text, but the current UI has no
   **Send local clipboard** action.
4. **Displays:** open the display menu, switch every listed monitor, and check
   that the selected monitor appears and the displayed resolution updates
   after a new key frame.
5. **Files:** choose **发送文件** (Send file) from the floating menu to pick a
   small file, then send a file back
   from the desktop. Progress is ACK-driven. Received files are stored in the
   app's `Documents/Received` directory and can be exported with the share
   button beside the transfer status or Finder's Files tab.

For black-screen diagnosis, keep Xcode's debug console open and look for
`CrossDesk` and `VideoToolbox` messages. With VideoToolbox H.264 decoding selected, look for
`VideoToolbox decoded frame` from MiniRTC and `CrossDesk delivered latest frame`
from the iOS bridge. Other codec paths do not produce the VideoToolbox message.
If decoded frames do not reach the bridge, check that the selected stream is
named `Display1`, `Display2`, and so on. If no frames decode, check the selected
codec and the desktop capture/encoding logs, then reconnect or switch displays
to request a new key frame.
