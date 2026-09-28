# CrossDesk Mobile (native iOS)

This target is a native MiniRTC controller. It uses the same WebSocket
signaling, libnice ICE, SRTP/RTP and data-stream protocol as the desktop app.
There is no `WKWebView` or browser runtime.

## Requirements

- Xcode 16 or newer
- Xmake 3.1.1 available on `PATH` (or selected with `XMAKE_BIN`)
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
For a released app, select the tag shown in **Settings → About and open-source
licenses** when cloning: `git clone --branch <source-tag> --recurse-submodules
https://github.com/kunkundi/crossdesk.git`. The iOS marketing version is independent
of the repository tag; use the source tag or commit recorded in the app.

Install [Xmake 3.1.1](https://github.com/xmake-io/xmake/releases/tag/v3.1.1),
CMake, and a current Python 3 with setuptools/wheel. Pin the package repository
in a separate Xmake global directory before building:

```sh
export XMAKE_GLOBALDIR="$HOME/.cache/crossdesk/xmake-ios"
xmake repo --update
git -C "$XMAKE_GLOBALDIR/.xmake/repositories/xmake-repo" fetch origin 121109352da272cd1a3d4ed52f2dcda34f839955
git -C "$XMAKE_GLOBALDIR/.xmake/repositories/xmake-repo" checkout --detach 121109352da272cd1a3d4ed52f2dcda34f839955
```

Keep `XMAKE_GLOBALDIR` set for command-line Xcode builds. For builds launched
from the Xcode app, set it as a user-defined build setting on the target, or
pin the default `~/.xmake/repositories/xmake-repo` instead. The build checks
the tool version, package repository commit and reviewed recipe hashes.
Application dependencies are built from source, with Xmake precompiled
package downloads disabled.

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
`Vendor/`. OpenSSL is kept separate and packaged as a static framework with SDK
privacy resources in the Xcode products directory. This build path targets physical devices; simulator builds are
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

## Third-party licenses

**设置 → 关于** shows the app version and links to the application's
license, open-source components, and source/build information. `crossdesk_wire`
is part of CrossDesk's own source and is covered by its application license,
not a separate third-party entry. OpenFEC is acknowledged at the top of the
open-source components page, with its INRIA copyright, CeCILL-C terms and limited
warranty/liability notice. Its embedded BSD and CC-BY-SA notices are preserved
in the source-notices document as well.

`licenses/sources.json` pins the reviewed source archives (SHA-256), notice-review
Git revisions, selected legal files and source-notice extraction rules.
`CrossDeskMobile/Resources/ThirdPartyLicenses.json` is the generated resource
bundled by Xcode. The catalog covers CrossDesk and 33 dependency entries for the default iOS build,
including header-only dependencies, GLib's proxy-libintl, spdlog's bundled fmt,
WebRTC-derived code, inih, and the applicable patent notices. Host build tools,
OpenH264, desktop-only libraries, and the disabled libpsl built-in suffix list
are excluded.

Regenerate from the pinned source distributions with Python 3.9 or newer:

```sh
python3 apps/ios/scripts/generate_third_party_licenses.py --generate
```

The collector uses original checksum-verified archives, including GLib's
`COPYING` file even when Xmake excludes it from an extracted build directory.
It reuses matching Xmake downloads or caches downloads outside the repository
in `~/.cache/crossdesk/licenses/`. License text is preserved, with explicit
character decoding for the legacy OpenFEC files. Copyright comment blocks are
collected from the reviewed runtime source directories; the catalog is an
attribution superset, not a claim that every source file survives linker
dead stripping.

Normal Xcode builds verify the checked-in resource, local license files and
package recipes offline before compiling native code. After compilation they
also compare all resolved dependency versions (including header-only packages)
with the catalog and check the merged archive's input hashes and pinned
toolchain. A missing, stale or altered catalog fails the build. Ordinary MiniRTC
code changes do not require changing the notice-review revision or regenerating
the catalog. Changes to dependencies, license texts or embedded third-party
notices still require reviewing and updating the affected catalog entries;
regeneration alone does not select a new dependency version.
`MINIRTC_ENABLE_AOM=true` also requires a reviewed AOM entry.

The source screen links to the exact application tag/commit, MiniRTC commit,
and custom dependency recipes/patches. `SourceMetadata.json` is generated in
the app bundle, not in the checkout. Development builds with local edits are
explicitly identified as modified. Version-tag CI builds reject an unclean
checkout or a mismatched tag. The release archive also carries
`ThirdPartySources.json`, `SourceMetadata.json` and the open-source rights
notice. `ThirdPartySources.json` preserves the notice-review catalog;
`SourceMetadata.json` records the actual build commit and per-component version,
source and recipe links used by the license screen, including MiniRTC, inih and
WebRTC-derived code. These links update automatically on every build without
rewriting the checked-in license resource. Release notes provide the
corresponding source and build links.
Keep the source tags, submodule commits and referenced dependency source
archives available for recipients of that release.

To change a library, edit MiniRTC in its submodule, or modify the relevant
package recipe/patch under `deps/submodules/minirtc/thirdparty`. If a package
recipe changes, change its package configuration revision (or rebuild that
package) so Xmake does not reuse a previous binary. Review affected notices,
update recipe hashes, and commit MiniRTC. When the notice catalog changes,
record the reviewed revision in `sources.json` and regenerate the catalog.
Build the application again to relink it with
the modified library. Use your own team and bundle identifier to install the
modified build; the maintainer's signing credentials are not required.

These materials implement source identification and attribution. They do not
by themselves resolve GPL/LGPL distribution-term compatibility or grant
additional permissions from other copyright holders. Applicable modification,
reverse-engineering and installation-information rights must also be respected.
See [LGPLv3 section 4](https://www.gnu.org/licenses/lgpl-3.0.html) and
[GPLv3 section 6](https://www.gnu.org/licenses/gpl-3.0.html).

## Optional codecs

The unused libaom backends are excluded by default. To include them for development,
set the `MINIRTC_ENABLE_AOM` environment variable (or Xcode user-defined build setting)
to `true`. This includes libaom in the merged native archive without changing the
AV1 factories' choice of SVT-AV1 and dav1d.

## Privacy choices

A launch without stored consent shows the privacy notice before signaling
registration, including after a previous refusal or withdrawal. Returning from
the background also shows it until consent is granted. Declining dismisses it
for the current visit and leaves settings, local history and the bundled
privacy policy available. Settings → Privacy → Privacy and Authorization shows consent status and can
withdraw consent to stop both the persistent identity peer and remote session. Consent is required
again before reconnecting; foregrounding or relaunching does not grant it.

The notice opens as a native large sheet with a scrolling document above a fixed
consent area. The agreement checkbox starts unchecked; **同意并继续** stays
disabled until it is selected and the bundled policy is available.
**暂不同意** dismisses the sheet for this visit. Swipe-to-dismiss is disabled
so either action is explicit.

Remote desktop previews default to off, including on upgrade. Enabling and
confirming saving in Settings → Privacy applies from the next connection. Previews stay local
and are excluded from device backups. Disabling saving or withdrawing consent
clears them; clearing is serialized after pending writes to prevent resurrection.
Old previews saved without this choice are removed on upgrade.

## Physical-device test checklist

Run the app from Xcode on a physical device and connect to a current desktop
build. Test the features in this order so a media problem is not confused with
a data-channel problem:

1. **Video:** after the session connects, the waiting panel should be replaced
   by the remote desktop. Tap the floating icon, then **网络状态** (Network
   status), to see video/audio/data/total receive and send bitrates, receive
   loss rates, submitted FPS, decoded resolution, video delay, connection RTT,
   P2P/TURN mode, and the actual SRTP encryption state. Scroll the panel in
   either orientation to see all metrics.
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

Network status samples and caches displayed measurements once per second while
the panel is open; video-frame redraws reuse that snapshot. Traffic and connection RTT
expire after three seconds without a valid report; RTT uses the desktop's
0.25-weight smoothing. FPS counts distinct frames submitted to the native
display layer during the last second. Video delay averages calibrated capture
time through display submission over that same window; it is an estimate, not
a measurement of physical screen presentation. Missing calibration or stale
samples show **—**, and idle video reaches zero FPS. Switching displays or
backgrounding clears video samples; disconnecting clears all statistics.
Receive loss rates are fractions converted to percentages. As on desktop,
MiniRTC's total loss is the sum of the media loss rates, not a packet-weighted
overall loss rate.

For black-screen diagnosis, keep Xcode's debug console open and look for
`CrossDesk` and `VideoToolbox` messages. With VideoToolbox H.264 decoding selected, look for
`VideoToolbox decoded frame` from MiniRTC and `CrossDesk delivered latest frame`
from the iOS bridge. Other codec paths do not produce the VideoToolbox message.
If decoded frames do not reach the bridge, check that the selected stream is
named `Display1`, `Display2`, and so on. If no frames decode, check the selected
codec and the desktop capture/encoding logs, then reconnect or switch displays
to request a new key frame.

## App Store archive and bundle validation

OpenSSL is on Apple's
[required SDK privacy-manifest list](https://developer.apple.com/support/third-party-SDK-requirements/).
The iOS build compiles OpenSSL from source, packages it separately as
`OpenSSL.framework`, then links and embeds it using Xcode's
[static framework support](https://developer.apple.com/documentation/xcode/creating-a-static-framework).
Xcode removes the static archive from the embedded framework (it may insert a
small resource-framework stub) and preserves `PrivacyInfo.xcprivacy`, source
metadata and license texts. The application does not load a dynamic OpenSSL copy.

The iOS-specific OpenSSL recipe disables POSIX metadata calls, automatic
`openssl.cnf` loading, and the unused file-URI store provider. It selects the
upstream `getrandom` entropy backend, which uses Darwin's `getentropy` on the
supported iOS versions; the random-device fallback is not compiled. Existing
in-memory certificate/key handling remains available, while `OSSL_STORE` file
URLs are intentionally unsupported. Signaling trust and hostname validation
use Apple Security rather than OpenSSL's build-host certificate paths.
Desktop OpenSSL builds are unchanged.

`package_openssl_ios.py` checks the actual OpenSSL archives for file metadata,
disk-space and boot-time API imports before producing the framework. Its
`sysctlbyname` calls are limited to the reviewed `armcap.c` CPU implementation
selection, not fingerprinting. The SDK privacy manifest declares no tracking,
SDK data collection or required-reason API use for this specific configuration.
This is not a declaration about generic OpenSSL builds or the application's
signaling/relay data processing. Source or configuration changes require another
review; the packager rejects known forbidden imports even when reusing a cache.

`PrivacyInfo.xcprivacy` declares app preferences (`CA92.1`), sandbox and
user-selected file metadata (`C617.1`, `3B52.1`), and elapsed/event timing
(`35F9.1`, `8FFB.1`). MiniRTC uses timing for pacing, timeouts, RTT and media
packet timestamps. The iOS GLib recipe disables unused filesystem-capacity
and system-volume discovery. These APIs must not be repurposed for fingerprinting.
The app-level manifest does not assert that the service collects no data: App Store
Connect privacy answers also depend on actual signaling/relay-server practices.
The application links to the [privacy policy](../../PRIVACY.md), including iOS
clipboard, file, local thumbnail and Keychain behavior.

Validate a built bundle using:

```sh
python3 apps/ios/scripts/verify_app_bundle.py /path/to/CrossDeskMobile.app
```

For a release, first commit all application and submodule changes, regenerate
notices for the committed dependency revision, and check out the intended
version tag. Publish the tag and its referenced MiniRTC commit to the public repositories
before distributing the app. Local tag verification cannot prove that recipients
can fetch an unpublished commit. With an App Store-capable development team
configured in Xcode:

```sh
xcodebuild -project apps/ios/CrossDeskMobile.xcodeproj \
  -scheme CrossDeskMobile -configuration Release \
  -destination 'generic/platform=iOS' \
  -archivePath /tmp/CrossDeskMobile.xcarchive \
  CROSSDESK_RELEASE_BUILD=YES archive

python3 apps/ios/scripts/verify_app_bundle.py \
  /tmp/CrossDeskMobile.xcarchive/Products/Applications/CrossDeskMobile.app \
  --require-release

xcodebuild -exportArchive -archivePath /tmp/CrossDeskMobile.xcarchive \
  -exportOptionsPlist apps/ios/ExportOptions-AppStore.plist \
  -exportPath /tmp/CrossDeskMobile-AppStore
```

Set a new `CURRENT_PROJECT_VERSION` for each upload. The export configuration
creates a local App Store export; it does not submit the app. Distribution
signing requires the appropriate Apple Developer Program membership and
provisioning. The unsigned GitHub artifact is for development, not an App Store
submission. Organizer/App Store Connect validation remains necessary.

The app embeds OpenSSL and libsrtp. Do not declare that encryption is limited
to Apple system APIs or set `ITSAppUsesNonExemptEncryption=NO` without completing
the applicable [export-compliance determination](https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance).
After that determination, supply the appropriate declaration and any required
approval code in the distribution build. The repository deliberately does not
preselect an exemption.
