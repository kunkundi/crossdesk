# HarmonyOS Slint 桌面移植

[桌面使用指南](../../../docs/USER_GUIDE.md) · [鸿蒙构建入口](../README.md) · [权限与签名](#被控端持久授权api-26)

目标是与 Windows、macOS、Linux 使用同一套界面和产品行为。此目录直接编译 `apps/desktop/src/gui/application/gui_application.cpp`、`gui/runtime`、`gui/features` 和原始 `crossdesk_ui.slint`。鸿蒙 PC 只维护这一套入口，旧 ArkUI PC 会话和 Slint 验证程序已移除。

**当前是可运行的移植开发版，尚未达到全部 PC 行为一致的验收标准。** PC 与移动端共用应用信息，包名统一为 `com.chunkdash.crossdesk`，桌面名称统一为 **CrossDesk**。移动端使用独立 ArkUI 工程；旧 PC 验证包 `cn.crossdesk.slintdesktop` 的数据不会自动迁移到统一包名的新应用。

## 一致性约束

- 界面、布局、菜单、密码确认、设置保存/取消、历史记录、分组、桌面墙、会话标签、文件传输状态机、剪贴板同步和网络连接全部来自桌面公共代码。业务改动应在公共代码修复，不能在 ArkTS 再写一套行为。
- ArkTS 只负责系统窗口、XComponent、键鼠事件、输入法、剪贴板、文件选择器、目录授权、系统托盘和打开链接。
- C++ 平台层实现窗口/渲染、音频、屏幕采集及输入注入。私有 `include/SDL3` 只适配公共代码实际使用的 SDL 服务，不是完整的 SDL 移植，不能用于其他目标。
- 屏幕、窗口和输入法坐标使用系统的实际显示器、工作区和各窗口缩放比例。Slint 的无边框属性直接映射到原生窗口，置顶属性目前只保证应用内部子窗口层级。
- 所有 Slint 对象、事件、定时器和渲染在同一 GUI 线程运行。系统服务通过 ArkTS 线程处理；退出会取消等待，切换窗口保持已有会话。
- Harmony 也定义 `__linux__`。共享代码仅在使用 X11/Wayland、Linux 原生窗口等位置排除 `__OHOS__`；POSIX 和 TLS 代码继续复用。

## 本机构建和运行

当前桌面适配使用 API 26 的[独立子窗口](https://developer.huawei.com/consumer/cn/doc/HarmonyOS-Guides/subwindow-guide)行为，要求 HarmonyOS API 26；移动端仍兼容 API 20。当前产物为 arm64，已在本机 2in1 模拟器验证。

```bash
# 构建 MiniRTC、Slint runtime、公共桌面程序和 HAP，安装到正在运行的模拟器
python3 apps/harmony/scripts/build_slint_desktop.py --desktop --install

# 后续增量构建；runtime 补丁变更仍会自动重建
python3 apps/harmony/scripts/build_slint_desktop.py --desktop --reuse-runtime --install

# 原生采集、输入 / wire 协议 / Slint 平台适配回归
python3 apps/harmony/tests/run.py --desktop
```

`--device` 可指定 HDC 设备。`--version` 设置与其他桌面构建相同的版本号，默认 `0.0.0`，因此开发包会出现现有的新版提示。未提供鸿蒙发布文件时，更新入口打开官网，不能下载 Linux 安装包。

需要本机 Slint compiler 1.17.1、Rust OHOS 标准库和 DevEco SDK。工具链定位及首次下载要求见 [鸿蒙构建说明](../README.md)，签名选项见下文。Slint 和 httplib 源码均固定版本及 SHA-256。生成的工程位于 `.native/slint-poc/desktop-project`，可用 DevEco Studio 打开。HAP 位于其 `entry/build/default/outputs/default/entry-default-unsigned.hap`；真机安装仍需自己的调试签名。

## 验证记录（2026-10-15）

工程精简后重新通过 `tests/run.py --desktop --mobile`、默认 PC HAP、全部可选权限声明的 PC HAP 以及移动端完整构建。包检查确认 PC 仅支持 `2in1`、移动端仅支持 `phone/tablet`，两端均只包含各自所需的三个原生库；PC 不含旧验证库或移动视频渲染器。以下设备交互记录来自此前验证；该次工程精简没有重复真机或模拟器端到端会话。

| 范围 | 证据 / 状态 |
| --- | --- |
| 共享桌面程序 | 完整 C++ 源码交叉编译、无未解析符号；HAP 的 arm64 ELF、SONAME 和动态依赖检查通过 |
| 启动和登录 | 本机 2in1 模拟器启动；系统 CA 校验下连接信令服务器；重启恢复设备身份和近期连接 |
| 真正远程会话 | 与测试 MiniRTC 被控端建立连接；共享密码对话框、会话窗口、工具栏和动态 NV12 测试画面正常；远端实际收到 control / keyboard / mouse 数据流，关闭会话后收到断开事件 |
| 中文输入 | 连接别名文本框全选替换；鸿蒙系统拼音候选 `nihao` 选择“你好”并提交到原 Slint 文本框 |
| 键盘 | 补齐扫描码、扩展位、左右修饰键及 F13–F24；复用公共按键状态对账，按下时先修饰键、释放时先普通键。左右 Ctrl、双 Shift、乱序快照、重复按键、释放失败重试和失焦清理的主机测试通过 |
| 被控输入实测 | Mac MiniRTC 测试主控经网络发送真实输入：拖动鸿蒙窗口、Ctrl+A 替换文本、右 Shift+方向键选择后删除、右 Ctrl、双 Ctrl 先放开左侧仍保持组合键均通过；按住右 Ctrl 时断开，之后本地输入正常，无修饰键滞留 |
| 剪贴板快捷键 | 最终 HAP 中经远程 Ctrl+A/C、End、Ctrl+V、Ctrl+A/X、Ctrl+V，`987` 完整复制为 `987 987`，剪切后再次粘贴恢复；补充后台同步不能抢占用户粘贴授权的回归测试 |
| 窗口 | 桌面墙独立窗口、全屏进入/退出恢复；主窗口关闭后从应用入口重新打开，保持原进程与登录状态 |
| 窗口重建 | 模拟器桌面墙移动后关闭、重开保留位置和尺寸；修复重建 XComponent 后因局部重绘缓冲被清空而出现黑块的问题 |
| 显示器适配 | 接入显示器增删、分辨率及工作区变化；负坐标、非连续屏幕 ID、主屏 ID 为 0、可用工作区和最近屏幕选择的主机测试通过。外接多屏与混合缩放仍需真机验收 |
| 被控端适配测试 | 直接编译生产 `devices.cpp`，以模拟系统接口验证 Display1/Display2 切换、失败回退、拔屏后编号稳定及恢复、采集帧率、私密场景暂停和授权失效时拒绝输入 |
| 原生采集回归 | 直接编译生产 `capture.cpp`，模拟异步授权；验证启动前不发送数据、GPU 路径优先及失败回退、CPU Surface 合并通知后排空队列、同步 fence 等待/超时/释放、私密场景和暂停/恢复、音频范围检查、重复停止及初始化失败清理；麦克风静音不影响视频和输入 |
| 模拟器被控实测 | 改用系统 Surface 输出及 GLES 纹理读取后，Mac 测试主控实际解码出 3120×2080 桌面；检查了壁纸、Slint 窗口、文字、颜色和方向。移动窗口时画面持续更新。同一鸿蒙进程断开、重新授权、重连再次收到正常画面；断开后被控栏消失，桌面墙仍能创建和绘制；授权弹窗选择“返回”收到取消状态且解码帧数为 0，授权等待中断开也能正常清理 |
| 鼠标状态 | 多键同时按下、拖出边界、取消和重复失焦的释放测试通过；切屏释放旧屏按键/鼠标，之后映射到目标屏像素范围 |
| 桌面墙 | 共享空状态、从近期连接导入设备、密码确认、单设备连接和动态视频预览通过；批量多设备会话尚未完整验收 |
| 系统托盘 | 接入 PC 原生托盘；关闭主窗口后保留原进程，左键恢复、菜单打开共享设置及退出通过；退出执行公共连接清理并移除图标 |
| 音频 | 合成 48 kHz 单声道测试音经 MiniRTC 解码后进入 OH_AudioRenderer；连接后自动播放、静音停止、取消静音恢复；尚未验收真机扬声器听感 |
| 文件 | 系统选择器发送 1 MiB 二进制文件，远端逐字节校验一致；接收 UTF-8 文件到默认应用目录和用户选择的公共目录；进程重启后无需重新选择目录即可接收 |
| 对话框及焦点 | 修复空初始目录导致的闪退；文件选择器绑定发起操作的窗口，取消后返回原会话；选择器尚未关闭时从托盘退出，也能取消等待并完成会话清理 |
| macOS 回归 | 公共 Slint UI 冒烟程序完整编译、链接和运行通过；新增间隔输入后 Ctrl+A 替换的回归，修复前失败、修复后通过。未完成整个 macOS 客户端链接回归 |

密码框沿用共享桌面的明文文本控件。输入英文密码时，请切换系统输入法到英文或先提交组合文本；自动化回归也必须控制输入法状态，不能把尚未提交的拼音候选当成已输入的密码。

文件测试使用独立的合成 MiniRTC 测试端及测试文件，不采集本机桌面。默认目录接收和公共目录接收均检查了 UTF-8 内容，发送测试文件大小为 1,048,576 字节，SHA-256 为 `fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83`。测试记录和临时文件已清理，原有连接记录保留。

被控端联调仅共享鸿蒙模拟器桌面，临时保存主控解码帧以检查实际像素，测试结束清理；没有采集工作站桌面。原始数据回调在本机模拟器只产生音频，CPU 映射的 Surface 视频缓冲也几乎全黑；`StartScreenCaptureWithSurface` 配合 NativeImage 外部纹理、V2 变换矩阵、GLES 读取和 NV12 转换后，主控可显示实际桌面。保留 CPU Surface 和原始回调作为初始化失败时的后备路径。采集帧率设置遵循 [AVScreenCapture 接口约束](https://github.com/openharmony/docs/blob/master/en/application-dev/reference/apis-media-kit/capi-native-avscreen-capture-h.md)，在异步启动成功通知之后执行。

采集纹理及 EGL context 由专用线程创建、使用和释放，生产者窗口由 NativeImage 持有，避免重复释放。停止采集只销毁自身资源，不终止与 Slint/ArkUI 共用的 EGL display。界面、会话、编码发送及连接清理继续使用公共桌面逻辑。远程拖动原先因 MOVE 事件携带 `BUTTON_NONE` 被窗口管理器拒绝，改为携带实际按下的按钮后，模拟器窗口实际移动并恢复原位。系统全局指针位置查询仍返回能力错误，与已验证的输入注入分别处理。

鸿蒙输入法可能消费硬件 Ctrl 组合键。`KeyboardHost` 在 `onKeyPreIme` 中把 Ctrl/Meta 及组合键交给原 Slint 按键分发；普通文本与 Ctrl+Space 保留系统输入法路径。输入法自身的全选、剪切、复制、粘贴、移动和选区回调也交给 Slint TextInput。公共 ID 格式化定时器改为通过 `running` 属性启停，避免原生重复定时器持续清除选区，修复同时适用于其他桌面平台。

系统 Ctrl+V 提供的剪贴板读取授权只能使用一次。显式粘贴使用独立的 `clipboardPaste` 请求，后台同步先检查 `READ_PASTEBOARD` 权限，避免公共剪贴板轮询抢先消费粘贴授权。剪贴板服务按顺序执行，读取失败不会伪装成成功的空字符串。已接入 `SystemPasteboard.update` 变更通知，隐藏/失焦窗口也向公共桌面投递 `SDL_EVENT_CLIPBOARD_UPDATE`，同步去重、大小限制及会话过滤继续复用 `ClipboardController`。后台读取使用按变更版本失效的缓存，读取期间再次变更会重读，撤权会清空缓存并拒绝结果。显式粘贴不进入后台缓存。当前普通调试包没有 `READ_PASTEBOARD` 的签名 ACL，因此跨应用后台同步的授权实机验收尚未完成。权限限制参见[系统剪贴板说明](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-v5/use_pasteboard_to_copy_and_paste-V5)。

## 全局输入和后台剪贴板权限

`KeyboardHook` 使用 API 21 的 `OH_Input_AddKeyEventHook`，而不是局部窗口监听或热键订阅。共享 `SessionDeviceManager` 决定捕获生命周期，原生回调仅进入公共按键队列；窗口失焦、撤权和取消时释放捕获状态，未知按键/未捕获的释放事件交回系统。卸载失败后回调只放行，不再访问已销毁的会话。未授权时继续使用原 Slint 窗口事件；收到授权变化后自动重试，不因周期性失败重试打断用户正按住的回退组合键。

本机 API 26 SDK 的 `PermissionDefinitions.json` 将 `HOOK_KEY_EVENT` 定义为 `system_basic / manual_settings`（tablet、2in1），将 `READ_PASTEBOARD` 定义为 `system_basic / user_grant`，二者均需签名 profile 中获批的 ACL。代码声明不能替代签名授权。`DesktopAccess` 串行处理系统授权界面，每次启动只引导一次；用户取消后不循环弹窗，并监听自身权限变化。键盘使用 `openPermissionOnSetting`，剪贴板使用 `requestPermissionsFromUser`。普通开发包不声明这两项权限，也不打开无法使用的授权页。

获得获批 profile 后，导出一项 DevEco `signingConfigs` 配置到仓库外的私有 JSON 文件，`material.certpath`、`material.storeFile`、`material.profile` 使用绝对路径：

```sh
python3 apps/harmony/scripts/build_slint_desktop.py --desktop --reuse-runtime \
  --desktop-permissions --signing-config /absolute/private/signing.json --install
```

`--desktop-permissions` 生成权限声明、用途说明和授权流程开关；`--signing-config` 生成签名配置并选择 `entry-default-signed.hap` 安装。生成的配置仅位于忽略的 `.native` 缓存，文件权限为 `0600`。无签名参数时仍输出普通 `entry-default-unsigned.hap`。可单独用 `--desktop-permissions` 做带权限声明的编译检查，不能据此认为已获得权限或可安装到真机。

远程 `lock_workstation` 命令已接到经过采集/输入授权检查的 Meta+L 路径，先释放残留键鼠，发送后配对释放，失败保留待清理状态。Windows `send_sas` 没有等价的鸿蒙公开接口，明确返回不支持；PC 和移动端仅在被控端为 Windows 时显示系统组合键按钮，目标为鸿蒙或其他系统时隐藏。普通 Ctrl/Alt/Meta 组合键由原生钩子按原顺序转发，系统最终允许拦截哪些保留键仍需授权后的真机验证。

验证记录：普通 HAP 与带权限声明的 HAP 构建通过；普通 HAP 安装本机 2in1 模拟器后，全局钩子实际返回 `201`，无授权弹窗循环，仍收到共享窗口回退发送的 `keyboard` 数据流。真实 Ctrl+A/C、End、Ctrl+V 把 `246` 复制为 `246 246`。生产 C++/ArkTS 的模拟 SDK 回归覆盖 Alt+Tab、Meta+L、重复键、小键盘 Enter、取消、失焦、撤权、重复注册、卸载失败、后台通知、缓存失效、读取中变更/撤权，以及权限拒绝后不重复引导。macOS 公共 Slint 冒烟测试新增快捷键菜单的真实点击回归，验证移除 SAS 后首项为锁屏、弹层命中范围随菜单缩小、切回 Windows 菜单仍正常。成功授权的钩子和后台跨应用读取没有在无 ACL 包中冒充验证通过。

<a id="permissions"></a>

## 被控端持久授权（API 26）

已接入一次系统设置授权、后续连接复用权限的路径。它解决的是应用运行中重复连接的录屏和键鼠确认；重启后自启动、系统登录前控制、锁屏及休眠恢复仍需分别验收。已使用包含这两项 ACL 的调试 Profile 完成签名和模拟器安装，尚未完成本机首次设置授权及真机免确认重连验证。

| 权限 | 用途 | SDK 要求 |
| --- | --- | --- |
| `ohos.permission.CUSTOM_SCREEN_RECORDING` | 录屏持久授权，指定显示器采集时无需重复确认 | API 22 起，`system_basic / manual_settings` |
| `ohos.permission.CONTROL_DEVICE` | 直接注入键鼠，无需每次请求临时输入授权 | API 26 起，`system_basic / manual_settings`，PC/2in1 |

两项权限都需要签名 Profile 的 ACL 和用户在系统设置中的授权。`CUSTOM_SCREEN_CAPTURE` 是截图权限，不能替代上述录屏权限。`HOOK_KEY_EVENT` 是控制端捕获快捷键的权限，也不能替代被控端的 `CONTROL_DEVICE`。

使用独立的 `--host-permissions` 选项生成声明、用途说明及设置引导，不要求同时申请全局键盘钩子或剪贴板权限：

```sh
# 生成 DevEco 工程并编译未签名包，供申请权限和检查构建使用。
python3 apps/harmony/scripts/build_slint_desktop.py --desktop --reuse-runtime --host-permissions
```

### 申请与签名

1. 在 DevEco Studio 打开 `apps/harmony/.native/slint-poc/desktop-project`。PC 与移动端的 bundle name 均为 `com.chunkdash.crossdesk`；AGC 应用、签名 Profile 和包名必须一致。构建从共享的 `AppScope/app.json5` 读取包名，不支持单独为 PC 覆盖包名。
2. 在 **File > Project Structure > Project > Signing Configs** 登录自己的华为开发者账号，关联 AGC 注册应用。在支持该流程的 DevEco Studio 中，通过 **Enable ACL Permissions** 为 `entry / EntryAbility` 申请上述两项权限。调用时机选择 `always`，因为最小化到后台后仍需接受经过认证的连接。若权限列表中没有 `CONTROL_DEVICE`，核对 IDE/SDK 的 API 26 支持，并通过 AGC 项目的“ACL权限”页签确认该权限的开放范围；网页端需先取得 ACL，再将其加入新生成的 Profile。
3. 分别填写用途和申请理由，并提交实际功能演示材料。可使用以下说明作为草稿，按真实发布场景修改：

   - `CUSTOM_SCREEN_RECORDING`：CrossDesk 为鸿蒙 PC 提供远程桌面被控功能。设备所有者在本机设置中主动授权后，应用仅在远程连接通过密码验证且会话有效时采集指定显示器并传输画面，断开后停止采集。申请持久权限以支持所有者离开设备后的远程访问，避免每次连接都要求现场确认。
   - `CONTROL_DEVICE`：在已认证且屏幕采集有效的远程会话中，将远端操作转换为本机键鼠输入。应用停止共享、系统进入私密场景或授权失效时不接受远程输入；用户可在系统设置中撤销授权。

4. 检查生成 Profile 的 ACL 确实包含两项权限。调试阶段官方自动签名流程可能提供临时 Profile，其有效期和权限以实际签发结果为准；发布阶段须完成正式权限及发布 Profile 申请。登录、提交申请及用户设置授权需要由账号/设备所有者完成。
5. 将工程 `build-profile.json5` 的一项 `app.signingConfigs` 对象保存为仓库外的私有 JSON 文件（不要包含外层数组）。保持原有签名字段，`material.certpath`、`material.storeFile`、`material.profile` 均使用绝对路径。构建脚本会重新生成工程配置，因此在再次运行脚本前先导出该对象。

```sh
python3 apps/harmony/scripts/build_slint_desktop.py --desktop --reuse-runtime \
  --host-permissions --signing-config /absolute/private/signing.json \
  --device YOUR_DEVICE_ID --install
```

Profile 必须对应 `com.chunkdash.crossdesk`，无需额外传入 `--bundle-name`；该选项仅为兼容已有命令保留，且只接受共享包名。目标设备已有同包名应用时将尝试更新，需满足签名和版本要求。脚本只使用显式传入的签名配置，不会自动继承源工程的签名；DevEco 写入源工程的 ACL 声明也按本次构建开关重新生成，避免重复声明或普通构建意外启用权限。

如果也需要控制端快捷键和后台剪贴板同步，可同时加 `--desktop-permissions`，但 Profile 还需包含对应的两项 ACL。未签名包的构建成功不代表有权限或能安装到真机。

申请入口和流程依据：[华为受限权限申请](https://developer.huawei.com/consumer/cn/doc/doccenter-capabilities/declare-permissions-in-acl)、[DevEco 自动签名及 ACL 配置](https://developer.huawei.com/consumer/cn/doc/doccenter-deveco-studio/ide-signing-auto)。

### 运行行为与验收

首次显示主窗口时，启用 `--host-permissions` 的构建合并提示缺少的“屏幕共享”和“辅助操作”开关；点击“前往设置”后打开本应用的系统设置详情页。两项均已授予时不弹窗，只缺一项时只解释该项。“暂不开启”或关闭引导后本次运行不重复打开，可自行进入系统设置开启或重启应用再次引导。普通构建不声明这两项权限、不打开对应设置。

PC 系统 `openPermissionOnSetting` 弹窗中的“隐私和安全 > 辅助操作”路径与实际应用设置页不一致，且该系统文案没有应用侧修改接口。因此被控权限改用窗口所属 `UIContext` 的一次引导，再按[华为官方应用设置跳转示例](https://developer.huawei.com/consumer/cn/doc/doccenter-dev-faq/faqs-access-control-16)打开 `application_info_entry`，`pushParams` 取运行中应用的实际包名，兼容默认验证包和已签名包。跳转失败时显示手动查找本应用及开关的说明，不退回错误路径的系统提示。仅打开设置不视为获得权限，仍读取系统授权状态。

启动前和设置变化后，ArkTS 都读取系统实际权限并同步给 C++。输入允许条件为“采集有效，且持有 `CONTROL_DEVICE` 或已获得临时输入授权”。不能仅查询 `OH_Input_QueryAuthorizedStatus`，因为 API 26 明确说明该接口只反映弹窗授权，不反映 `CONTROL_DEVICE`。参见 [Input Kit 接口说明](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis-input-kit/capi-oh-input-manager-h.md#oh_input_requestinjection)。

持有 `CUSTOM_SCREEN_RECORDING` 时，指定屏幕采集设置不弹出内容选择器，并由系统检查权限是否允许免确认启动；未持有权限时保留原有授权流程。此行为依据本机 SDK 和 [OpenHarmony 录屏服务的 `IsSkipPrivacyWindow`](https://github.com/openharmony/multimedia_player_framework/blob/master/services/services/screen_capture/server/screen_capture_server.cpp)，需要目标 HarmonyOS 固件验证。无论哪条路径，都等待 `STARTED` 后才发送画面。断开仍停止录屏、释放键鼠和撤销临时输入授权，不通过持续录屏维持权限。

主机回归覆盖持久权限下临时授权状态仍为 `UNAUTHORIZED` 的输入、断开重连、权限撤销、私密场景、设置串行化、取消后不重复引导、退出后不再打开待处理设置，以及录屏策略失败回退。真机验收需使用带相应 ACL 的签名包，依次验证首次授权、两次重连、退出重开、分别撤销两项权限及恢复授权；锁屏/唤醒和重启后场景单独记录，不能用模拟 SDK 测试代替。

2026-10-15 验证：`python3 apps/harmony/tests/run.py --desktop` 通过；普通构建与 `--host-permissions` 构建均通过 ArkTS、原生链接和 HAP 检查。构建脚本检查打包后的权限声明与所选开关一致。

2026-10-15 签名验证：使用本机 DevEco 生成的 `com.chunkdash.crossdesk` 调试 Profile，确认其 ACL 包含上述两项权限、已登记当前模拟器。签名 HAP 通过官方 `hap-sign-tool verify-app`（含代码和权限签名校验），在 API 26 的 2in1 模拟器安装并启动成功。系统读取到两项权限，但首次设置授权尚未开启，未据此宣称免确认重连已通过。交付包保存在 `.native/artifacts/acl-host/crossdesk-acl-signed.hap`。同时验证普通构建不会继承源工程签名或可选 ACL 声明。

2026-10-15 引导修正：回归覆盖两个开关合并、仅缺一项、已授权免提示、取消不跳转、不重复引导、实际包名路由、退出后的过期回调、跳转失败及设置打开不等于授权。完整 `--desktop` 主机回归和带 ACL 的签名构建通过；实际开关授权和远程重连仍需设备端验收。

## 无 ACL 的窗口与输入行为（2026-10-15）

继续使用同一套公共 Slint 界面、桌面墙、会话及编辑逻辑；本轮修复集中在鸿蒙窗口和输入事件适配层，不增加平台专用的产品交互。

- 原生标题栏的最大化/还原状态作为一次完整确认传回 Slint。Slint 的状态设置器会同步回调平台；`WindowMode` 阻止确认过程中产生的中间状态再次控制系统窗口，修复点击还原后立即重新最大化的循环。属性事件只携带实际变化的模式，状态版本过滤延迟命令，包括等待标题/尺寸服务期间发生的原生状态变化。
- 最小化保留原有全屏/最大化模式。主窗口恢复使用 `restore()`，子窗口使用 `showWindow()`；重复显示不重设主窗口的还原尺寸。由全屏返回普通最大化明确使用 `EXIT_IMMERSIVE`。窗口 API 行为参见[官方窗口参考](https://developer.huawei.com/consumer/cn/doc/harmonyos-references/js-apis-window)。
- 子窗口隐藏/重建时注销监听，按原生窗口实例过滤旧的关闭、失焦、尺寸和状态回调。XComponent 事件携带表面标识，旧表面的延迟销毁和输入不能作用于新表面。创建失败清理半成品，窗口命令队列完成后释放；标题/层级服务失败不再阻断尺寸和模式处理。
- 输入法增加独立会话编号。相同窗口内切换输入框时，旧回调、旧更新和正在等待的 attach 不能写入新编辑器；原生分发再次检查会话编号。窗口销毁会立即使输入法回调失效，异步完成后释放连接。
- 硬件事件传递系统 CapsLock、NumLock 快照和 SDK 提供的可打印 ASCII Unicode 值；小键盘在 NumLock 关闭时映射导航键，右 Alt 单独映射为 Slint AltGr。重复键和释放继续使用首次按下的符号。此项没有声称覆盖 SDK 未提供的非 ASCII 布局或全部输入法行为。

验证：新增生产 ArkTS 窗口适配回归，覆盖恢复/全屏、过期命令、重建、晚到回调、可选服务失败和创建失败清理；C++ 回归覆盖 Slint 同步状态回写，输入法回归覆盖同窗口换字段和 attach 期间销毁；扩展锁键、布局符号、右 Alt 和小键盘测试。`python3 apps/harmony/tests/run.py --desktop` 及普通无 ACL HAP 构建通过。2in1 模拟器实测主窗口最大化后还原到原尺寸、最小化后重新激活、桌面墙进入/退出全屏及关闭后重建；重建后的表面继续绘制，没有再出现状态往返。最终安装包验证普通输入及 Ctrl+A/C、End、Ctrl+V，将 `246` 复制为 `246 246`，随后清空测试输入并恢复原窗口位置。

## 网络状态面板展开回归（2026-10-15）

已复现点击网络状态图标后，鼠标留在按钮上时面板不展开、移开后才部分展开的问题。公共 `GuiApplication` 每 16 ms 对统计模型调用 `set_vector`，即使行数不变也会重置行数依赖，反复重启控制栏的高度动画。改为只在行数变化时重置模型，其余时间仅更新变化的数据行，保留各 PC 共用的 Slint 布局和 60 ms 动画。

新增 `slint_stream_render_test` 使用真实 Slint 定时器与保留缓冲区的软件渲染，持续刷新统计值，在左右停靠布局下连续点击展开/收起且不移动鼠标，同时检查完整面板像素和输入范围。修复前可复现失败，修复后与 `slint_ui_smoke_test` 均通过。普通无 ACL HAP 已构建、安装，在本机 2in1 模拟器连接合成视频源后确认点击展开和收起均不再等待鼠标移开。

```bash
xmake build slint_stream_render_test
xmake run slint_stream_render_test
```

## 其他按钮与弹窗回归（2026-10-15）

继续排查公共桌面的模型更新和弹窗输入路径，修复以下问题，所有 PC 仍复用同一套 Slint 组件和回调：

- 显示器和文件传输模型也改为行数变化时才整体重置，其余时间只更新变化的行，避免每帧刷新重建菜单及列表。
- 文件传输浮层的标题、边缘和空白区域原先会把点击、滚轮传到远端。浮层现在完整占用本地输入区域，拖出边界以及关闭瞬间都不会向远端漏发鼠标释放事件。
- 编码下拉框打开后，连接状态切换为会话中会禁用设置，但原菜单仍可修改编码。公共 `CompactComboBox` 现在在禁用时关闭菜单、取消展开定时器，选项回调也检查当前有效性。

新增 `slint_popup_render_test`，直接运行共享 Slint 窗口，使用保留像素缓冲区、真实鼠标/键盘事件和 16 ms 模型更新定时器。覆盖显示器、快捷键、视频设置、文件传输、分组筛选、设置下拉框和标签、消息、关于、桌面墙网络状态及添加弹窗、被控端控制者选择及断开确认。检查鼠标留在触发位置时的绘制、按住期间的数据刷新、Esc/取消关闭，以及本地输入不泄漏到远端。文件浮层与禁用下拉框的用例均在修复前失败、修复后通过。

验证：`slint_popup_render_test`、`slint_stream_render_test`、`slint_ui_smoke_test` 在本机 macOS 通过；普通无 ACL HAP 构建、安装成功。2in1 模拟器实测主菜单、设置下拉框、设置标签、桌面墙网络状态、添加菜单和添加弹窗，点击后鼠标保持原位也可立即显示，取消/关闭后正常恢复。模拟器本轮未连接真实设备；会话内持续刷新及被控面板状态转换由共享 Slint 回归覆盖，不代表所有平台的全部端到端验收已完成。

```bash
xmake build slint_popup_render_test
xmake run slint_popup_render_test
```

## 被控端鼠标同步（2026-10-15）

鸿蒙被控端原先忽略公共 `Start(show_cursor)` 参数，始终请求将鼠标录入视频，并仅发送 `embedded` 状态。现在原生 PC 会话关闭录屏中的鼠标，通过 `OH_Input_GetCursorInfo` 同步箭头、文本、链接、拖拽、忙碌和窗口缩放等语义形状及可见状态，复用 macOS/Windows 控制端已有的 `cursor_state` 协议和显示逻辑。需要视频内鼠标的会话继续使用 `show_cursor=true`。录屏只有确认移除鼠标后才报告独立绘制；移除失败会尝试视频内鼠标，均失败则报告采样不可用。切屏、暂停恢复及私密场景恢复保留该策略。

位置查询优先使用 `OH_Input_GetPointerLocation`；后台查询不可用时，使用录屏期间 `OH_Input_AddMouseEventMonitor` 收到的真实鼠标事件。两种来源均将屏内像素坐标和物理显示器 ID 转换为已协商的显示器编号和归一化位置，兼容负坐标排列及重叠屏幕。监听仅在有效的独立鼠标录屏会话期间注册，暂停、私密场景、切屏和停止时清空位置并移除监听；移除失败的回调保持停用。没有有效查询或监听事件时，不伪造位置，仍保留已获取的形状/可见状态。

后台位置同步使用 **`ohos.permission.INPUT_MONITORING`**，其授权方式是 `system_basic / system_grant`，需要签名 Profile 中对应 ACL。该权限出现在[华为自动签名支持的 ACL 清单](https://developer.huawei.com/consumer/cn/doc/doccenter-deveco-studio/ide-signing-auto)中；仍需满足应用场景及审批要求。获得该 ACL 后，构建时增加 `--cursor-position-permission` 生成并校验声明。它独立于录屏和输入注入授权，普通构建及已有两项被控权限的构建不会自动加入该 ACL。接口依据：[OpenHarmony Input Kit 的鼠标监听接口](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis-input-kit/capi-oh-input-manager-h.md#oh_input_addmouseeventmonitor)。

更正：本机 API 26 SDK 确实定义 `INPUT_DEVICE_CONFIGURATOR`，并注明它可允许后台直接查询位置，但 SDK 定义和证书授权标记不能证明该权限已在 HarmonyOS 开发者 ACL 申请页面开放。此前据此建议直接申请不充分；当前构建不再声明或依赖它。`INPUT_MONITORING` 用于事件监听，不能给 `OH_Input_GetPointerLocation` 直接查询授权。未获得监听权限时，后台位置仍不能保证反馈；鼠标形状和可见状态同步不依赖该权限。

回归测试调用生产采集适配和鼠标采样代码，覆盖形状映射、隐藏/无设备、后台查询拒绝后的监听位置、监听权限拒绝、暂停/切屏清理、迟到回调、移除失败与重试、系统已移除监听、采样失败、屏幕 ID 映射、无效坐标、协议往返、公共显示策略及录屏模式回退。`python3 apps/harmony/tests/run.py --desktop` 通过，带监听权限声明的 API 26 AArch64 桌面 HAP 构建通过，并检查打包后只声明 `INPUT_MONITORING`、未声明 `INPUT_DEVICE_CONFIGURATOR`。尚需获批签名权限的鸿蒙 PC 真机与 macOS/Windows 控制端进行端到端验收，模拟 SDK 测试不替代实际鼠标服务和签名权限验证。

## 开机自启动设置（2026-10-15）

共享设置页的「启动与后台」保留原有自启动开关样式。鸿蒙上开关按系统查询结果显示，点击开关弹出设置引导，不直接翻转状态；引导中显示详细状态，并提供「刷新状态」。打开设置、应用回到前台或原生窗口重新激活时会重新查询；查询失败、设备不支持和未开启在引导中分别显示。查询超过 5 秒可重试，旧查询、超时后的迟到结果及退出后的回调不会覆盖新状态。其他 PC 平台继续使用原有自启动开关行为。

鸿蒙 PC 用户需要在 **系统设置 → 应用和元服务 → 应用启动管理 → 添加应用** 中选择 **CrossDesk**，点击「完成」；关闭时在该列表移除。应用内提供完整操作说明，不使用未经验证的设置深链接，也不会把保存本地配置当作系统已经开启。步骤依据[华为电脑自启动说明](https://consumer.huawei.com/cn/support/content/zh-cn16048401/)；本机 API 26 SDK 的公开接口仅提供 `isAutoStartupSupported()` 和 `getAutoStartupStatusForSelf()`，不需要增加自启动 ACL。系统接口 `setApplicationAutoStartup()` 不适用于普通应用。

程序启动后继续由公共 `GuiApplication::InitializeModules()` 和 `GuiRuntime::CreateConnectionPeer()` 恢复设备身份并连接服务器，托盘也复用原有启动流程。自启动设置只决定系统是否启动应用，不等于获得录屏、输入注入权限或登录前控制能力。

验证：`python3 apps/harmony/tests/run.py --desktop` 通过，新增生产 ArkTS 回归覆盖状态变化、查询失败、超时、过期结果、进程退出，以及启动/前台/窗口激活和原生刷新命令。`slint_popup_render_test` 和 `slint_ui_smoke_test` 在 macOS 编译运行通过，覆盖开关开启/关闭时点击弹出引导且不改变状态、刷新按钮、异步状态变化、最小窗口布局、Esc/确认关闭及其他平台开关仍可直接切换。普通无 ACL HAP 构建及包检查通过，已安装到 API 26 的 2in1 模拟器，启动后自动连接服务器，自启动开关显示关闭，点击后弹出引导并显示「未开启」，手动刷新及从系统设置返回均正常。未修改模拟器的系统自启动名单；开启/关闭的状态转换由模拟 SDK 回归覆盖。真机系统自启动、重启/切换用户后自动上线仍需设备验收。

## 未完成的验收项

以下项目不能因为公共逻辑已编译就标记为支持完整：

- 真机被控端：录屏和输入注入的授权、拒绝、撤销、私密场景恢复；系统声音采集；已实现启动时连接的多显示器枚举、切换和输入映射，但模拟器只有一个屏幕。输入注入仅在采集有效且系统授权时执行。
- 真机主控音频听感、长时间连接、多会话及桌面墙批量操作；目前远程画面通过合成测试源验证。
- 文件传输中途取消、大文件与磁盘满、用户撤销目录授权、后台多任务传输；基础发送、接收及重启授权已经通过。
- 系统自启动后的真机重启/登录验收、系统服务和隐私遮罩；自启动状态、设置引导及返回刷新已接入，开启/关闭由用户在系统设置管理。无人值守的持久录屏/输入授权路径仍需带 ACL 的真机验收，详见上文。原生托盘已接入，仅在系统报告支持时启用。
- 多屏被控：MiniRTC 在初始化时协商 DisplayN；已协商屏幕拔出后保留编号、恢复到仍可用的屏幕，重新插入可恢复。运行中新增屏幕需要重新初始化连接端（当前重启应用），不能直接向已有会话发送未协商的流。切屏通过重新启动系统采集实现，系统可能再次要求授权，尚不能视为与其他 PC 无感切换完全一致。
- 全局键盘钩子的授权实机验收、系统保留键在不同 PC 固件上的路由、Meta+L 实际锁屏效果及跨应用后台剪贴板同步。无 ACL 开发包只能验证拒绝/回退路径；模拟 SDK 的成功测试不能替代系统授权后的端到端验收。
- 窗口和输入：外接多屏、混合缩放、窗口位置/尺寸的长周期恢复、所有输入法组合范围、非 US 键盘布局、系统保留快捷键及跨应用置顶浮层。模拟器不具备的鼠标服务会报告能力不足。
- GPU 视频直接导入、性能与耗电。当前视频走公共 Slint CPU 图像后备路径，再用 GLES3 呈现；手机和平板使用独立移动端。

## 代码定位

- `CMakeLists.txt`：公共桌面源码和原生依赖集合，只构建 `slint_desktop` 桥接。
- `font-discovery.patch`：关闭 OHOS 运行库不适用的 Fontconfig 自动发现；字体仍由后端显式加载。
- `../native/audio.*`：PC 与移动端共享的 OHAudio 播放；PC 不编译移动视频渲染器。
- `DisplayHost.ets` / `displays.h`：系统显示器与工作区、全局/屏内坐标转换、窗口所属屏幕及热插拔事件。
- `window-properties.patch`：把原 Slint 窗口的无边框和置顶属性交给鸿蒙窗口层；补丁只影响 OHOS。
- `pointer.h`：逐窗口跟踪鼠标按下状态，失焦/取消时完整释放。
- `desktop.cpp` / `surface.inc`：Slint GUI 线程、窗口适配、EGL/GLES3 呈现。
- `DesktopHost.ets` / `Index.ets`：鸿蒙原生窗口、表面及事件分发。
- `TextInputHost.ets` / `os-services.patch`：Slint 1.17.1 C++ 后端缺少的输入法/鼠标形状桥接，补丁只改变 OHOS 目标。
- `keyboard_hook.cpp` / `DesktopAccess.ets`：系统键盘钩子、权限变化及公共捕获回退。
- `KeyboardHost.ets`：输入法之前的硬件快捷键路由及配对释放，调用原 Slint 按键处理。
- `services.cpp` / `devices.cpp`：文件、剪贴板、音频、采集和输入等平台服务。
- `../native/capture.cpp`：原生采集生命周期、异步授权状态、音视频缓冲处理。
- `../native/capture_surface.cpp`：系统录屏 Surface 的 NativeImage/GLES 读取、方向变换及线程资源管理。
- `../native/input.cpp` / `key_mapping.h`：被控键鼠注入、扩展扫描码映射、按键快照对账及授权/断开后的释放。
- `FileHost.ets`：系统文件选择器、URI 读取快照、目录持久授权及进程启动时激活。未选公共目录时接收文件保存在应用 `files/desktop/Received Files`；可在共享设置中选择公开目录。
- `TrayHost.ets` / `tray.cpp`：PC 系统托盘事件接回公共桌面的显示、设置和退出回调。
- `AutostartHost.ets` / `platform/harmony/harmony_autostart.h`（桌面源码目录）：异步查询系统自启动状态，提供共享设置页的只读状态及刷新桥接。
- `platform/harmony/gui/window_state.h`（桌面源码目录）：原生窗口激活状态用于公共音频、剪贴板和输入释放逻辑。
- `keyboard.h`：鸿蒙按键到 Slint 按键的状态化映射。

日志：`hdc shell hilog -T CrossDeskDesktop`（共享桌面应用）和 `hdc shell hilog -T SlintDesktop`（表面/采集）。登录凭据不应复制到问题报告；公共桌面版本的本机密码默认可见，录屏/截图前请隐藏。
