#!/bin/bash
set -e

APP_NAME="crossdesk"
APP_NAME_UPPER="CrossDesk"
EXECUTABLE_PATH="./build/macosx/arm64/release/crossdesk"
APP_VERSION="$1"
PLATFORM="macos"
ARCH="arm64"
IDENTIFIER="cn.crossdesk.app"
ICON_PATH="icons/macos/crossdesk.icns"
MACOS_MIN_VERSION="10.12"

APP_BUNDLE="${APP_NAME_UPPER}.app"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

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

echo ".app created successfully."

echo "building pkg..."
pkgbuild \
  --identifier "${IDENTIFIER}" \
  --version "${APP_VERSION}" \
  --install-location "/Applications" \
  --component "${APP_BUNDLE}" \
  build_pkg_temp/${APP_NAME}-component.pkg

mkdir -p build_pkg_scripts

cat > build_pkg_scripts/postinstall <<'EOF'
#!/bin/bash
set -e

IDENTIFIER="cn.crossdesk.app"

# 获取当前登录用户
USER_HOME=$( /usr/bin/stat -f "%Su" /dev/console )
HOME_DIR=$( /usr/bin/dscl . -read /Users/$USER_HOME NFSHomeDirectory | awk '{print $2}' )

# 清除应用的权限授权，以便重新授权
# 使用 tccutil 重置录屏权限和辅助功能权限
if command -v tccutil >/dev/null 2>&1; then
    # 重置录屏权限
    tccutil reset ScreenCapture "$IDENTIFIER" 2>/dev/null || true
    # 重置辅助功能权限
    tccutil reset Accessibility "$IDENTIFIER" 2>/dev/null || true
    # 重置摄像头权限（如果需要）
    tccutil reset Camera "$IDENTIFIER" 2>/dev/null || true
    # 重置麦克风权限（如果需要）
    tccutil reset Microphone "$IDENTIFIER" 2>/dev/null || true
fi

# 为所有用户清除权限（可选，如果需要）
# 遍历所有用户目录并清除权限
for USER_DIR in /Users/*; do
    if [ -d "$USER_DIR" ] && [ "$USER_DIR" != "/Users/Shared" ]; then
        USER_NAME=$(basename "$USER_DIR")
        # 跳过系统用户
        if [ "$USER_NAME" != "Shared" ] && [ -d "$USER_DIR/Library" ]; then
            # 删除 TCC 数据库中的相关条目（需要管理员权限）
            TCC_DB="$USER_DIR/Library/Application Support/com.apple.TCC/TCC.db"
            if [ -f "$TCC_DB" ]; then
                # 使用 sqlite3 删除相关权限记录（如果可用）
                if command -v sqlite3 >/dev/null 2>&1; then
                    sqlite3 "$TCC_DB" "DELETE FROM access WHERE client='$IDENTIFIER' AND service IN ('kTCCServiceScreenCapture', 'kTCCServiceAccessibility');" 2>/dev/null || true
                fi
            fi
        fi
    fi
done

exit 0
EOF

chmod +x build_pkg_scripts/postinstall

productbuild \
  --package build_pkg_temp/${APP_NAME}-component.pkg \
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

rm -rf build_pkg_temp build_pkg_scripts ${APP_BUNDLE}

echo "PKG package created successfully."
echo "package ${APP_BUNDLE}"
echo "installer ${PKG_NAME}"