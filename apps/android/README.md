# CrossDesk Android

原生 Android 控制端，使用 Java 界面、JNI 和 MiniRTC，与桌面端使用相同的信令、ICE、SRTP 和输入协议。没有 WebView。当前版本为 0.1.0，支持 Android 8.0（API 26）及以上的 arm64 手机和平板。

## 当前功能

- 官方或自托管 WSS 信令服务器；授权后进入前台即自动注册或登录本机身份，首页实时显示连接、重连及失败状态。发起桌面连接时复用该身份；切换服务器会重新登录，撤回授权或进入后台会关闭连接。
- 优先使用 MediaCodec 硬件解码 H.264 / AV1，硬件缓冲区直接提交 Surface；设备无对应硬件、分辨率超限或运行失败时，按原编码格式回退到 OpenH264 / dav1d。软件输出由独立显示线程将 NV12 转 RGBA；只保留最新待显示帧。MiniRTC 原有的通用压缩帧解码队列保持不变。
- 48 kHz 单声道 PCM 播放，支持静音，音频队列有容量限制。
- 相对鼠标（触控板）和绝对位置模式；单指点击、绝对模式按住拖动、长按右键、双指滚轮，以及外接键盘、鼠标。
- 横竖屏等比例显示、远端多显示器切换。与 iOS 一致的画面质量（低/中/高）、采集帧率（30/60 fps）、画面偏好；支持能力检测、电脑端确认、失败/5 秒超时回退及连续操作的应答排序。
- 根据常见移动设备解码能力和屏幕尺寸设置起始参数：显示长边不超过 1280 像素时默认低画质（最高 720p）/30 fps，其余默认中画质（最高 1080p）/30 fps。用户保存的画面偏好不变，仍可手动选择高画质或 60 fps。
- 网络状态按视频、音频、数据、合计展示收发速率和丢包率，并显示实际提交帧率、分辨率、画面延时、平滑 RTT、P2P/TURN 和实际 SRTP 状态。每秒刷新，过期或无效测量显示“—”。
- 可拖动的悬浮控制球、三列控制菜单和浮动 ASCII / 电脑键盘、常用快捷键；通过剪贴板发送中文等文本，再手动执行远端粘贴。
- 设备身份与选择保存的连接密码使用 Android Keystore / AES-GCM 加密；首次联网需同意，后台切换会结束会话。
- 与 iOS 一致的首页卡片、顶部状态/公告/设置入口、密码底部弹层、两列最近连接及分组设置页；最近连接按服务器隔离，长按可删除记录及密码。
- 页面转场沿用 iOS 的交互方式：设置、隐私、关于及公告页面进入时从右滑入，返回时反向滑出，背景页面轻微跟随；进入和退出远程会话使用 0.2 秒淡入淡出。遵循 Android 系统动画开关及速度设置。
- 设置页的鼠标控制、画面偏好，以及悬浮控制栏的画质、帧率和画面偏好使用 0.2 秒的选中底块滑动动画；连续点击从当前位置转向，电脑端拒绝或超时后平滑回退，系统关闭动画时直接切换。
- 通知公告与 iOS 共用信令协议，支持未读角标、分页、下拉刷新、更新推送、详情和网页链接；已读及删除状态按服务器、本机身份和公告版本保存在本机。
- 连接建立并收到对端信息后，按 iOS 相同的版本号及补丁规则检查桌面端版本；旧版本弹出“升级提示”，点击“知道了”继续控制。每次连接最多检查、提醒一次；缺失或无效版本按旧版提醒，检查失败不打断连接。仅获取公共 HTTPS 版本元数据，不上传对端身份或版本；未授权不请求，断开及进入后台取消检查并忽略过期回复。
- 可选保存远程画面预览：默认关闭，经确认开启后，从下一次连接起保存一张 640×360 预览，用于最近连接卡片。支持清除预览，关闭选项或撤回授权会清除已有图片。
- “关于 CrossDesk”对齐 iOS 的版本、软件许可、开源组件及源码与构建说明页面，包含 OpenFEC 告知、组件详情和可离线选择复制的完整许可；版本号读取当前 APK，构建时记录应用与 MiniRTC 的基础版本。

当前不包含安卓被控端、文件传输、隐私屏、Ctrl+Alt+Del 服务命令、捏合缩放或 iOS 全部附加功能。相关入口保留 iOS 的布局并明确标注不可用。硬件能力取决于设备和编码格式，软件回退时高分辨率或高帧率可能增加功耗；可通过悬浮菜单的画面设置降低画质或采集帧率。暂只构建 `arm64-v8a`，不支持 x86 模拟器或 32 位设备。

## 构建

在 macOS 或 Linux 安装：

- JDK 17 或更新版本（可使用 Android Studio 自带 JBR）。
- Android SDK Platform 36、Build Tools 36.0.0、NDK `28.2.13676358`。
- Xmake **3.1.1**、Python 3.9+、Git、CMake 和主机编译工具。
- Python 的 setuptools、wheel，用于构建依赖的宿主工具。

先在仓库根目录初始化子模块：

```sh
git submodule update --init --recursive
```

macOS 命令行示例：

```sh
export ANDROID_HOME="$HOME/Library/Android/sdk"
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
# 若 PATH 中的 Xmake 不是 3.1.1，通过 XMAKE_BIN 指定对应可执行文件。
# 构建器也识别 ~/.cache/crossdesk/toolchains/xmake-3.1.1/xmake。
cd apps/android
./gradlew :app:assembleDebug
```

也可用 Android Studio 打开 `apps/android`。在 SDK Manager 安装上述版本；SDK 位置可由 IDE 写入被忽略的 `local.properties`。Xmake 不在 IDE 的 PATH 中时，启动 IDE 前设置 `XMAKE_BIN`。

输出为 `app/build/outputs/apk/debug/app-debug.apk`，已由本机 Android 调试密钥签名：

```sh
"$ANDROID_HOME/platform-tools/adb" install -r app/build/outputs/apk/debug/app-debug.apk
```

Release 构建使用 `./gradlew :app:assembleRelease`，默认输出未签名 APK；发行前需配置自己的签名。Debug APK 也使用优化后的 Release 原生库，以保证软件解码性能。

首次构建会下载并从源码编译依赖，耗时取决于网络。后续编译复用缓存。Android 直接编译 `deps/submodules/minirtc` 中的源码；构建产物和依赖缓存位于 `.native/`，Xmake 工程配置位于 `native/.xmake/`，与桌面和 iOS 分开保存：

- Gradle 9.3.1、AGP 9.1.0、NDK r28c 固定在工程中。
- Xmake 包仓库固定到 `eda39de3fb99b420c168f1ab9ab2d1791e11b662`。
- `scripts/build_native.py` 调用独立的 `native/xmake.lua`，直接包含 MiniRTC 子模块的构建定义。构建过程不复制 MiniRTC 源码，也不应用补丁。
- MiniRTC 源码内的 Android 分支启用 OpenH264、SVT-AV1 和 dav1d 软件编解码及依赖配方，兼容 OpenFEC 的 Android 头文件，并关闭 UPnP。SVT-AV1 使用内置 ARM CPU 能力检测；当前应用作为控制端只接收视频，库同时提供 AV1 编码能力。TLS 校验位于 MiniRTC 内部。STUN、TURN UDP/TCP 与直接连接仍由 MiniRTC 提供。
- MiniRTC 的 `thirdparty/openssl/xmake.lua` 统一维护 iOS 和 Android 配方；Android 显式使用 NDK 的归档索引工具，避免 macOS `ranlib` 损坏 ELF 静态库。
- SVT-AV1 编码及 dav1d 解码已在 Android 15 arm64 / 16 KB 模拟器上完成验证，覆盖软件编码、请求硬件加速时的软件回退、码率更新、强制关键帧及 320×192 → 256×144 → 320×192 分辨率切换；两组测试各成功编码并解码 24 帧。这是编解码正确性验证，不代表真机实时编码性能。
- 依赖静态链接到 `libcrossdesk_android.so`，保留 16 KB ELF / APK 对齐。

修改原生代码后重新运行 Gradle 即可；修改依赖版本或配方时同步检查许可证内容，必要时移除 `.native/packages` 重新构建。MiniRTC 的源码和依赖配方变更直接保存在子模块中；提交时先提交 MiniRTC，再更新主仓库的子模块引用。

## GitHub Actions

[`Build Android`](../../.github/workflows/build-android.yml) 可在 Actions 页面单独手动运行；修改 Android、MiniRTC、共享协议或许可证资料的 Pull Request 也会触发。仓库原有的 [`Build and Release`](../../.github/workflows/build.yml) 在分支推送、标签推送及手动运行时调用同一工作流，`v` 标签发布会等待安卓构建成功，并将未签名 Release APK 加入 GitHub Release 和现有下载服务器的产物集合。推送主仓库前，先确保它引用的 MiniRTC 提交已推送到子模块远程仓库，否则 CI 无法完成检出。

单独发布使用 [`Release Mobile`](../../.github/workflows/release-mobile.yml)。先提交 `app/build.gradle` 中需要发布的 `versionName`，再为该提交创建 `android-v<versionName>-YYYYMMDD` 标签，例如：

```sh
git tag android-v0.1.0-20261002
git push origin android-v0.1.0-20261002
```

该标签只触发安卓构建，将未签名 Release APK 发布到独立的 GitHub Release 和现有下载服务器。标签版本必须与应用版本一致，日期必须有效。重试发布时，在 Actions 页面手动运行 `Release Mobile`，将已有标签填入 `source_tag`；即使从其他分支触发，也会检出该标签的源码。APK 的源码资料记录安卓标签及准确的应用、MiniRTC 提交。

移动端独立发布不会更新桌面 `latest` 标签、GitHub 的最新 Release 选择或桌面 `version.json`。下载服务器上传保留其他平台文件，复用仓库已有的 `SERVER_HOST`、`SERVER_USER`、`SERVER_KEY` Secrets。普通桌面版本标签仍通过 `Build and Release` 发布全部平台；单独运行 `Build Android` 仍只上传 Actions 构建产物。

工作流使用 Ubuntu 24.04、JDK 17、Python 3.13、SDK 36、Build Tools 36.0.0、NDK r28c 和 Xmake 3.1.1。Gradle 使用工程内的 Wrapper，原生构建器固定 Xmake 包仓库版本。Gradle 缓存与 Android 原生依赖缓存分别管理；原生缓存按宿主架构、构建脚本及 MiniRTC 配方区分，不复用桌面或 iOS 的配置和目标文件。

CI 构建 Debug、Release 和设备测试 APK，执行 Debug / Release Lint、本机 C++ 协议与画面队列测试，并校验 APK 的 arm64 ABI、16 KB ELF / ZIP 对齐、完整许可证及 Debug 签名。Ubuntu x64 构建机上不运行 arm64 设备测试；使用下面的设备验证流程在 arm64 手机或模拟器上执行。Lint 报告在构建失败时也会保留。

产物使用 Android 工程自己的 `versionName`，例如 `v0.1.0-20261001`，不跟随桌面版本号。构建日期采用上海时区；带日期的版本标签沿用标签中的日期，与 iOS 一致。Actions 提供：

- `crossdesk-android-arm64-unsigned-<版本>.apk`：未签名 Release APK，标签发布只收集此 APK，安装前需自行签名。
- `crossdesk-android-arm64-debug-<版本>.apk` 和对应的 `-test.apk`：可安装的调试应用和设备测试包，保留 14 天。
- `crossdesk-android-reports`：Lint 报告，保留 14 天。

当前 CI 不需要签名 Secrets。Debug APK 使用每次运行的临时调试证书，可能无法覆盖手机上已有的其他签名版本；卸载原应用会清除本机连接记录和密码。需要持续覆盖升级时，应使用同一私钥签名 Release APK，并在发布新版本时递增 `app/build.gradle` 中的 `versionCode`。未签名 APK 已完成对齐，可通过 SDK 的 `apksigner sign --ks <自己的密钥库> --out <已签名.apk> <未签名.apk>` 签名，再用 `apksigner verify` 检查。

## 使用与连接安全

授权后首页会自动连接服务器，绿色“已连接服务器”表示信令登录完成；失败时可点击状态重试。设置页显示本机 ID。电脑端保持在线，在 Android 首页填写 9 位设备 ID，首次点击“连接”后在底部弹层输入密码，可选择加密保存密码。成功保存后，点击最近连接或再次输入相同 ID 会直接使用该服务器下保存的密码，重启应用后仍然有效；密码无法读取或被远端拒绝时会重新显示输入弹层，新密码仅在连接成功后替换，取消或连接失败不会覆盖原密码。自托管服务器从“设置 → 服务器”展开修改主机名与端口，不能填写 `https://`、路径或完整 URL。默认是 `api.crossdesk.cn:9099`。

最近连接卡片与 iOS 一样，在设备 ID 旁显示已保存密码的钥匙图标和在线／离线状态，在线设备优先，同一状态内保留原来的最近连接顺序。授权并登录服务器后，通过身份信令订阅 `recent_connections_presence`，接收完整快照与 `presence_update`；每 30 秒刷新，状态超过 60 秒未更新则显示离线。断线、切换服务器或进入后台会清除在线状态，重连后等待新的快照。自托管服务需要支持相同的设备在线状态协议。

证书信任来自 Android 默认 TrustManager 导出的根证书。JNI 设置证书文件路径后，MiniRTC 的 Android 分支通过 `SSL_CTX_load_verify_locations` 显式加载，并校验证书链及 DNS / IP 主机名；不会跳过 TLS 验证。Android 应用进程带有 `AT_SECURE` 标记，不能依赖 OpenSSL 的默认路径加载器读取 `SSL_CERT_FILE`，否则会出现所有服务器均无法建立信任链的问题。自托管服务器应使用受系统信任的证书。当前没有在应用内导入自签名证书的入口。

身份按主机名和端口分开保存，备份与设备迁移不包含应用数据。密码不写入连接记录，仅在明确开启“保存密码”后写入独立加密存储；未开启时仅用于当前会话。连接成功通知只处理一次，重复的传输就绪回调不会再次保存密码或重建会话页面；密码存储拒绝空值覆盖。旧版本已被空值覆盖的密码无法恢复，遇到此记录时会提示重新输入一次，并保留“保存密码”开启状态。由于 MiniRTC 的诊断日志包含登录身份，Android 桥接层关闭其会话日志。身份存储损坏会显示错误，不会静默重新注册。

会话中的“剪贴板”发送用户输入的文本；收到的远端文本先留在会话内，点击“复制远端文本”才写入手机剪贴板。退出会话后清除会话内容。每次发送最多 128 KB。浮动键盘切换“电脑”后的“复制 / 粘贴 / 全选”根据远端平台选择 Ctrl 或 macOS Command。

首页设置里的“画面偏好”保存在本机，作为下一次连接的默认偏好；会话内画面设置与 iOS 一样只影响当前连接。初始质量按屏幕长边选择低（不超过 1280 像素）或中，采集帧率为 30 fps，电脑端报告支持后自动下发；旧版电脑未报告支持时禁用控件。网络页的 FPS 统计实际提交给 Android Surface 的帧，画面延时使用 MiniRTC 校准后的采集时间至提交时刻的估计值，未完成校准时显示“—”；RTT 使用 0.25 权重平滑，3 秒未更新的数据会过期。切换屏幕清除旧画面的帧率、分辨率和画面延时，保留网络统计。

采集时间的有效性在接收时检查，本机解码和显示等待计入画面延时；真实延时超过 5 秒也会继续显示，不会当成无效时间戳过滤。显示线程复用解码器的 CPU NV12 缓冲区，只保留一帧待显示画面，切换显示器或 Surface 时清除旧帧，会话结束时先停止显示线程再销毁原生连接。

## 远程画面预览

设置 → 隐私中的“保存远程画面预览”默认关闭，完成联网授权后可开启。与 iOS 一致，开启前说明预览可能包含个人或工作信息；每次连接只在实际显示画面后保存一张预览，居中裁剪为 640×360、JPEG 质量 78。截图来自远程视频的 Surface，不包含悬浮菜单、键盘、本地光标或画面外的黑边；压缩、读写和解码在串行后台线程执行，不复制完整 4K 位图，也不连续截图。

图片保存在应用私有、不参与备份的 `RecentConnectionThumbnails` 目录，不上传、不写入系统相册。文件按服务器与远端 ID 隔离，最近连接卡片异步加载；未开启、没有预览或图片损坏时继续显示平台封面。删除或淘汰连接记录会删除对应预览。

“清除预览图”保留连接记录、密码和开关状态，下一次连接仍可保存新的预览。关闭保存选项或撤回联网授权会关闭保存并清除全部预览。清除时立即使未完成的截图和读取请求失效，删除与保存共用串行队列，防止已清除的图片被旧截图重新写回；清除失败会显示提示，可再次重试。

## 通知公告

首页铃铛展示全部未读公告的数量（超过 99 条显示 99+）。授权并连接服务器后，通过已有身份信令请求 `announcements_list`，摘要每页 200 条、正文每页 20 条；不依赖远程桌面连接。列表支持刷新、滚动加载更多、长按或左滑删除，删除前需确认，且仅隐藏本机的当前版本。打开详情即标记已读；服务器更新公告版本后会重新显示并计为未读。详情正文保留纯文本和换行，支持 HTTP(S) 地址及 `[文字](https://...)` 链接，不执行 HTML 或其他协议。

阅读和删除标记以原子文件保存在不参与备份的 `AnnouncementReads` 目录，按主机名、端口和本机设备 ID 隔离；写入失败会提示并保留原状态。内容只缓存于内存。收到 `announcements_changed` 时刷新，分页期间目录版本改变时重新获取；过期请求、断线前的回复不会覆盖新状态。请求失败或 12 秒超时可重试；未授权不联网，进入后台停止请求。自托管服务需要支持与桌面及 iOS 相同的公告协议。

## 验证

连接 arm64 设备或启动 arm64 模拟器后，在 `apps/android` 执行：

```sh
./gradlew :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug
"$ANDROID_HOME/platform-tools/adb" install -r app/build/outputs/apk/debug/app-debug.apk
"$ANDROID_HOME/platform-tools/adb" install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
"$ANDROID_HOME/platform-tools/adb" shell am instrument -w -r cn.crossdesk.mobile.test/androidx.test.runner.AndroidJUnitRunner
"$ANDROID_HOME/platform-tools/adb" uninstall cn.crossdesk.mobile.test
python3 test/verify_apk.py app/build/outputs/apk/debug/app-debug.apk
python3 test/run_native.py
```

以上流程覆盖安装应用并只卸载测试包，保留已有应用数据；多台设备同时连接时，在每个 `adb` 命令中加上 `-s <设备序列号>`。`connectedDebugAndroidTest` 可能卸载被测应用及其数据，不用于已有连接记录或密码的设备。

测试源码、资源和检查入口统一位于 `test/`：

- `test/native/`：本机 C++ 协议、帧队列与画面延时回归，由 `test/run_native.py` 编译运行。
- `test/android/java/`：非 UI 的 Android 功能回归，覆盖需要 Android API 或 JNI 的逻辑，包括设备在线状态刷新、过期与信令重连。Gradle 的 `androidTest` 源集显式引用此目录。
- `test/android/assets/tls/`：本机环回 TLS 测试证书，仅进入测试 APK。
- `test/verify_apk.py`：发布 APK 的 arm64 ABI、16 KB ELF / ZIP 对齐、许可证完整性和源码资料检查。

设备回归仅保留 TLS 证书校验、凭据加密及服务器隔离、原生连接生命周期、键盘映射、画面设置状态、网络采样、公告与预览数据持久化、版本兼容判断。预览存储测试使用生成的纯色位图，不采集屏幕或视频画面。

UI 验证统一由人工完成，不维护页面导航、布局、动画、悬浮窗和截图采集的自动化用例。许可证和构建资料的完整性由 `test/verify_apk.py` 检查。

## MediaCodec 编解码

实现参照 MiniRTC 的 NVCodec 分层：`src/media/mediacodec/` 负责硬件后端、能力查询和 JNI/Surface 资源；`src/media/video/encode/mediacodec/` 与 `src/media/video/decode/mediacodec/` 分别接入编码器、解码器接口，并负责同编码格式的软件回退。Android 工程直接编译这些源码。通用 API 仅需启用 `hardware_acceleration` 和 `native_video_output`。JavaVM 由 Android JNI 适配层在 `JNI_OnLoad` 中设置到后端私有实现；该桥接不安装为 SDK 头文件，也不导出为动态库符号。当前能力查询和 SurfaceTexture 创建依赖 JNI，缺少 JavaVM 时仍可使用软件编解码。独立输出线程在画面停止更新后仍会取出最后一帧，空闲时休眠。

- H.264 / AV1 由系统能力查询选择硬件组件，支持编码码率调整、请求关键帧和分辨率变化。编码输入为 8 位 NV12，硬件需支持 NV12 / I420 ByteBuffer 输入；不满足时回退 OpenH264 / SVT-AV1。当前应用仍是控制端，库的编码能力不代表已实现安卓被控端。
- 解码原生输出为可保留/释放的 MediaCodec 缓冲区，显示线程直接提交 Surface。切换显示器、销毁 Surface 和切回软件输出时解除旧 Surface 绑定。缓冲区在解码器重建或销毁后不可再次显示；未显示的缓冲区自动归还。
- 关闭原生输出时，支持将硬件 NV12 / I420 输出按行距、填充高度和裁剪范围转换为紧凑 NV12；未知布局回退软件。
- Retroid Pocket 2S 的 `OMX.sprd.av1.decoder` 实际加载软件 dav1d，虽被系统标成硬件组件，仍明确排除；AV1 使用软件回退。H.264 可使用 `OMX.sprd.h264.encoder` / `OMX.sprd.h264.decoder`。

AV1 硬件路径尚无对应实机验证；Retroid 上使用 dav1d 软件解码。

## 开源资料

应用为 GPL-3.0-only，MiniRTC 为 LGPL-3.0-only，其余组件保留各自许可证。`scripts/generate_notices.py` 复用仓库已有的依赖通知，选取 Android 实际使用的组件；`licenses/additional-notices.json` 补充 OpenH264、GNU libiconv 和 NDK / LLVM 运行时通知。APK 的开源信息页提供完整内容。可以修改库源码、重新链接，并使用自己的 Android 密钥签名安装。

设置 → 关于使用与 iOS 相同的分组及入口：版本、软件许可、开源组件、源码与构建说明。软件许可中可分别阅读 GNU GPL 第 3 版及开源软件权利说明；开源组件页单独展示 OpenFEC 告知，其余组件列出名称和许可证，详情包含版本、源码、适用的构建配方和每一份许可文档。内容按 Android 构建生成，不显示未使用的 iOS 依赖，Android 签名说明替换 Apple 签名说明。

APK 保留完整 `THIRD_PARTY_NOTICES.txt`，并生成轻量的 `ThirdPartyLicenses.json` 索引、72 份独立许可文档及 `SourceMetadata.json`。打开文档时才读取正文，长文按有限长度分块显示，支持文本选择、复制与网页链接；打包验证核对每份正文的 SHA-256。源码页面显示标签或提交版本及未发布修改提示，源码链接指向基础提交。当前 Android 构建说明、构建脚本、MiniRTC、OpenSSL 及 SVT-AV1 配方随 APK 离线提供，避免开发版链接指向基础提交中尚不存在的 Android 文件。
