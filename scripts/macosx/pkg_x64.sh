#!/usr/bin/env bash
set -euo pipefail

APP_NAME="crossdesk"
APP_NAME_UPPER="CrossDesk"
EXECUTABLE_PATH="build/macosx/x86_64/release/crossdesk"
PLATFORM="macos"
ARCH="x64"
BINARY_ARCH="x86_64"
IDENTIFIER="cn.crossdesk.app"
ICON_PATH="icons/macos/crossdesk.icns"
MACOS_MIN_VERSION="14.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

normalize_app_version() {
    local input="$1"
    local prefix=""
    local body="$input"

    if [[ "$body" == v* ]]; then
        prefix="v"
        body="${body#v}"
    fi

    if [[ "$body" =~ ^([0-9]+(\.[0-9]+){1,3})-([0-9]{8})-([0-9]+)$ ]]; then
        echo "${prefix}${BASH_REMATCH[1]}-${BASH_REMATCH[4]}-${BASH_REMATCH[3]}"
    else
        echo "$input"
    fi
}

APP_VERSION="$(normalize_app_version "$1")"

APP_BUNDLE="${APP_NAME_UPPER}.app"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
INSTALLER_TITLE="${APP_NAME_UPPER} ${APP_VERSION}"

PKG_NAME="${APP_NAME}-${PLATFORM}-${ARCH}-${APP_VERSION}.pkg"
DMG_NAME="${APP_NAME}-${PLATFORM}-${ARCH}-${APP_VERSION}.dmg"
VOL_NAME="Install ${APP_NAME_UPPER}"

echo "delete old files"
rm -rf "${APP_BUNDLE}" "${PKG_NAME}" "${DMG_NAME}" build_pkg_temp CrossDesk_dmg_temp

mkdir -p build_pkg_temp
mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}"

cp "${EXECUTABLE_PATH}" "${MACOS_DIR}/${APP_NAME_UPPER}"
chmod +x "${MACOS_DIR}/${APP_NAME_UPPER}"

if [ -f "${ICON_PATH}" ]; then
    cp "${ICON_PATH}" "${RESOURCES_DIR}/crossedesk.icns"
    ICON_KEY="<key>CFBundleIconFile</key><string>crossedesk.icns</string>"
else
    ICON_KEY=""
fi

echo "generate Info.plist"
cat > "${CONTENTS_DIR}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME_UPPER}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME_UPPER}</string>
    <key>CFBundleIdentifier</key>
    <string>${IDENTIFIER}</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME_UPPER}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    ${ICON_KEY}
    <key>LSMinimumSystemVersion</key>
    <string>${MACOS_MIN_VERSION}</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSCameraUsageDescription</key>
    <string>应用需要访问摄像头</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>应用需要访问麦克风</string>
    <key>NSAppleEventsUsageDescription</key>
    <string>应用需要发送 Apple 事件</string>
    <key>NSScreenCaptureUsageDescription</key>
    <string>应用需要录屏权限以捕获屏幕内容</string>
</dict>
</plist>
EOF

xattr -cr "${APP_BUNDLE}" 2>/dev/null || true
find "${APP_BUNDLE}" -name '._*' -delete

"${SCRIPT_DIR}/bundle_app.sh" "${APP_BUNDLE}" "${BINARY_ARCH}"
"${SCRIPT_DIR}/verify_app.sh" "${APP_BUNDLE}" "${BINARY_ARCH}"

echo ".app created successfully."

mkdir -p build_pkg_scripts
cp scripts/macosx/tcc_postinstall.sh build_pkg_scripts/postinstall
chmod +x build_pkg_scripts/postinstall

mkdir -p build_pkg_resources
cat > build_pkg_resources/welcome.html <<EOF
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Helvetica Neue", sans-serif;
            font-size: 13px;
            line-height: 1.5;
            color: #222;
            margin: 0;
            padding: 0;
        }
        h1 {
            font-size: 20px;
            font-weight: 600;
            margin: 0 0 12px;
        }
        p {
            margin: 0 0 10px;
        }
    </style>
</head>
<body>
    <h1>欢迎安装 ${INSTALLER_TITLE}</h1>
    <p>CrossDesk 将安装到“应用程序”文件夹。</p>
    <p>首次启动时，CrossDesk 会引导你在系统设置中授予必要权限，包括辅助功能、录屏与系统录音等。</p>
    <p>为避免旧版本授权残留造成状态误判，安装后可能需要重新授权。</p>
    <p>安装完成后，请从“应用程序”文件夹启动 CrossDesk。</p>
</body>
</html>
EOF

echo "building pkg..."
pkgbuild \
  --identifier "${IDENTIFIER}" \
  --version "${APP_VERSION}" \
  --install-location "/Applications" \
  --component "${APP_BUNDLE}" \
  --scripts build_pkg_scripts \
  build_pkg_temp/${APP_NAME}-component.pkg

cat > build_pkg_temp/Distribution <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>${INSTALLER_TITLE}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="never" require-scripts="false" hostArchitectures="x86_64"/>
    <choices-outline>
        <line choice="default">
            <line choice="${IDENTIFIER}"/>
        </line>
    </choices-outline>
    <choice id="default" title="${INSTALLER_TITLE}"/>
    <choice id="${IDENTIFIER}" title="${INSTALLER_TITLE}" visible="false">
        <pkg-ref id="${IDENTIFIER}"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}" version="${APP_VERSION}" onConclusion="none">crossdesk-component.pkg</pkg-ref>
</installer-gui-script>
EOF

productbuild \
  --distribution build_pkg_temp/Distribution \
  --package-path build_pkg_temp \
  --resources build_pkg_resources \
  "${PKG_NAME}"

echo "PKG package created: ${PKG_NAME}"

# Set custom icon for PKG file
if [ -f "${ICON_PATH}" ]; then
    echo "Setting custom icon for PKG file..."
    # Create a temporary iconset from icns
    TEMP_ICON_DIR=$(mktemp -d)
    cp "${ICON_PATH}" "${TEMP_ICON_DIR}/icon.icns"
    
    # Use sips to create a png from icns for the icon
    sips -s format png "${TEMP_ICON_DIR}/icon.icns" --out "${TEMP_ICON_DIR}/icon.png" 2>/dev/null || true
    
    # Method: Use osascript to set file icon (works on macOS)
    osascript <<APPLESCRIPT
use framework "Foundation"
use framework "AppKit"

set iconPath to POSIX file "${TEMP_ICON_DIR}/icon.icns"
set targetPath to POSIX file "$(pwd)/${PKG_NAME}"

set iconImage to current application's NSImage's alloc()'s initWithContentsOfFile:(POSIX path of iconPath)
set workspace to current application's NSWorkspace's sharedWorkspace()
workspace's setIcon:iconImage forFile:(POSIX path of targetPath) options:0
APPLESCRIPT

    if [ $? -eq 0 ]; then
        echo "Custom icon set successfully for ${PKG_NAME}"
    else
        echo "Warning: Failed to set custom icon (this is optional)"
    fi
    
    rm -rf "${TEMP_ICON_DIR}"
fi
echo "Set icon finished"

rm -rf build_pkg_temp build_pkg_scripts build_pkg_resources ${APP_BUNDLE}

echo "PKG package created successfully."
echo "package ${APP_BUNDLE}"
echo "installer ${PKG_NAME}"
