# CrossDesk Mobile (native iOS)

This target is a native MiniRTC controller. It uses the same WebSocket
signaling, libnice ICE, SRTP/RTP and data-stream protocol as the desktop app.
There is no `WKWebView` or browser runtime.

## Requirements

- Xcode 16 or newer
- Xmake available on `PATH`
- A physical arm64 iPhone or iPad running iOS 16 or newer

## Build

Open `CrossDeskMobile.xcodeproj`, select the `CrossDeskMobile` scheme and a
physical device, then build. The first build phase compiles MiniRTC and the
shared CrossDesk protocol, then merges their iPhoneOS static dependencies into
a local archive under `Vendor/`.

You can also verify the target without code signing:

```sh
xcodebuild -project ios/CrossDeskMobile.xcodeproj \
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

Every iOS build includes VideoToolbox, OpenH264, dav1d, libaom, and SVT-AV1. H.264 uses
VideoToolbox by default; the in-app **Settings > Video Codec** selector switches
H.264 software processing to OpenH264. AV1 encoding uses SVT-AV1 and AV1 decoding
uses dav1d; VideoToolbox AV1 hardware decoding is currently unsupported.

## Physical-device test checklist

Run the app from Xcode on a physical device and connect to a current desktop
build. Test the features in this order so a media problem is not confused with
a data-channel problem:

1. **Video:** after the connection turns green, the waiting panel should be
   replaced by the remote desktop. The status bar reports the decoded size and
   frame count.
2. **Audio:** play continuous sound on the remote computer, then toggle the
   speaker button. Audio is Opus-decoded by MiniRTC and played as 48 kHz mono
   16-bit PCM through `AVAudioEngine`.
3. **Clipboard:** copy a short text value on the phone and choose **Send local
   clipboard**. Copy a different value on the desktop and confirm that it is
   written to the iOS pasteboard. Text is limited to 128 KiB.
4. **Displays:** open the display menu, switch every listed monitor, and check
   that the frame counter restarts and the selected monitor appears after a
   new key frame.
5. **Files:** use the folder button to pick a small file, then send a file back
   from the desktop. Progress is ACK-driven. Received files are stored in the
   app's `Documents/Received` directory and can be exported with the share
   button or Finder's Files tab.

For black-screen diagnosis, keep Xcode's device console open and filter for
`CrossDesk`. A working pipeline prints both `VideoToolbox decoded frame` from
MiniRTC and `CrossDesk received decoded frame` from the iOS bridge. If only the
first message appears, verify that the selected stream is named `Display1`,
`Display2`, and so on. If neither appears, update and rebuild the desktop side
and request a key frame.
