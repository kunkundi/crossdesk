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

CERTS_SOURCE="certs"
CERT_NAME="crossdesk.cn_root.crt"

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
USER_HOME=$( /usr/bin/stat -f "%Su" /dev/console )
HOME_DIR=$( /usr/bin/dscl . -read /Users/$USER_HOME NFSHomeDirectory | awk '{print $2}' )

DEST="$HOME_DIR/Library/Application Support/CrossDesk/certs"

mkdir -p "$DEST"
cp -R "/Library/Application Support/CrossDesk/certs/"* "$DEST/"

exit 0
EOF

chmod +x build_pkg_scripts/postinstall

pkgbuild \
  --root "${CERTS_SOURCE}" \
  --identifier "${IDENTIFIER}.certs" \
  --version "${APP_VERSION}" \
  --install-location "/Library/Application Support/CrossDesk/certs" \
  --scripts build_pkg_scripts \
  build_pkg_temp/${APP_NAME}-certs.pkg

productbuild \
  --package build_pkg_temp/${APP_NAME}-component.pkg \
  --package build_pkg_temp/${APP_NAME}-certs.pkg \
  "${PKG_NAME}"

echo "PKG package created: ${PKG_NAME}"

rm -rf build_pkg_temp build_pkg_scripts ${APP_BUNDLE}

echo "PKG package created successfully."
echo "package ${APP_BUNDLE}"
echo "installer ${PKG_NAME}"