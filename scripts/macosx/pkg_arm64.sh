#!/bin/bash
set -e  # 遇错退出

# === 配置变量 ===
APP_NAME="crossdesk"
APP_NAME_UPPER="CrossDesk"  # 这个变量用来指定大写的应用名
EXECUTABLE_PATH="./build/macosx/arm64/release/crossdesk"             # 可执行文件路径
APP_VERSION="$1"
PLATFORM="macos"
ARCH="arm64"
IDENTIFIER="cn.crossdesk.app"
ICON_PATH="./icons/crossdesk.icns"         # .icns 图标路径
MACOS_MIN_VERSION="10.12"

CERTS_SOURCE="./certs"                     # 你的证书文件目录，里面放所有需要安装的文件
CERT_NAME="crossdesk.cn_root.crt"

APP_BUNDLE="${APP_NAME_UPPER}.app"          # 使用大写的应用名称
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

PKG_NAME="${APP_NAME}-${PLATFORM}-${ARCH}-${APP_VERSION}.pkg"  # 保持安装包名称小写
DMG_NAME="${APP_NAME}-${PLATFORM}-${ARCH}-${APP_VERSION}.dmg"
VOL_NAME="Install ${APP_NAME_UPPER}"

# === 清理旧文件 ===
echo "🧹 清理旧文件..."
rm -rf "${APP_BUNDLE}" "${PKG_NAME}" "${DMG_NAME}" build_pkg_temp CrossDesk_dmg_temp

mkdir -p build_pkg_temp

# === 创建 .app 结构 ===
echo "📦 创建 ${APP_BUNDLE}..."
mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}"

echo "🚚 拷贝可执行文件..."
cp "${EXECUTABLE_PATH}" "${MACOS_DIR}/${APP_NAME_UPPER}"  # 拷贝时使用大写的应用名称
chmod +x "${MACOS_DIR}/${APP_NAME_UPPER}"

# === 图标 ===
if [ -f "${ICON_PATH}" ]; then
    cp "${ICON_PATH}" "${RESOURCES_DIR}/crossedesk.icns"
    ICON_KEY="<key>CFBundleIconFile</key><string>crossedesk.icns</string>"
    echo "🎨 图标添加完成"
else
    ICON_KEY=""
    echo "⚠️ 未找到图标文件，跳过图标设置"
fi

# === 生成 Info.plist ===
echo "📝 生成 Info.plist..."
cat > "${CONTENTS_DIR}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME_UPPER}</string>  <!-- 使用大写名称 -->
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME_UPPER}</string>  <!-- 使用大写名称 -->
    <key>CFBundleIdentifier</key>
    <string>${IDENTIFIER}</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME_UPPER}</string>  <!-- 使用大写名称 -->
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

echo "✅ .app 创建完成"

# === 构建应用组件包 ===
echo "📦 构建应用组件包..."
pkgbuild \
  --identifier "${IDENTIFIER}" \
  --version "${APP_VERSION}" \
  --install-location "/Applications" \
  --component "${APP_BUNDLE}" \
  build_pkg_temp/${APP_NAME}-component.pkg

# === 构建 certs 组件包 ===
# 先创建脚本目录和脚本文件
mkdir -p scripts

cat > scripts/postinstall <<'EOF'
#!/bin/bash
USER_HOME=$( /usr/bin/stat -f "%Su" /dev/console )
HOME_DIR=$( /usr/bin/dscl . -read /Users/$USER_HOME NFSHomeDirectory | awk '{print $2}' )

DEST="$HOME_DIR/Library/Application Support/CrossDesk/certs"

mkdir -p "$DEST"
cp -R "/Library/Application Support/CrossDesk/certs/"* "$DEST/"

exit 0
EOF

chmod +x scripts/postinstall

# 构建 certs 组件包，增加 --scripts 参数指定 postinstall
pkgbuild \
  --root "${CERTS_SOURCE}" \
  --identifier "${IDENTIFIER}.certs" \
  --version "${APP_VERSION}" \
  --install-location "/Library/Application Support/CrossDesk/certs" \
  --scripts scripts \
  build_pkg_temp/${APP_NAME}-certs.pkg

# === 组合产品包 ===
echo "🏗️ 组合最终安装包..."
productbuild \
  --package build_pkg_temp/${APP_NAME}-component.pkg \
  --package build_pkg_temp/${APP_NAME}-certs.pkg \
  "${PKG_NAME}"

echo "✅ 生成安装包完成：${PKG_NAME}"

# === 可选：打包成 DMG ===
echo "📦 可选打包成 DMG..."
mkdir -p CrossDesk_dmg_temp
cp "${PKG_NAME}" CrossDesk_dmg_temp/
ln -s /Applications CrossDesk_dmg_temp/Applications

hdiutil create -volname "${VOL_NAME}" \
  -srcfolder CrossDesk_dmg_temp \
  -ov -format UDZO "${DMG_NAME}"

rm -rf CrossDesk_dmg_temp build_pkg_temp scripts ${APP_BUNDLE} ${DMG_NAME}

echo "🎉 所有打包完成："
echo "   ✔️ 应用：${APP_BUNDLE}"
echo "   ✔️ 安装包：${PKG_NAME}"
echo "   ✔️ 镜像包（可选）：${DMG_NAME}"