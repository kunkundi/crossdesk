# CrossDesk HarmonyOS

鸿蒙 PC 只保留共享 Slint 桌面客户端；手机和平板使用独立 ArkUI 控制端。旧 ArkUI PC 界面、独立 PC 会话状态机和早期 Slint 验证程序已移除。

| 设备 | 实现 | 要求与说明 |
| --- | --- | --- |
| PC（`2in1`） | 公共桌面 UI、业务逻辑和鸿蒙平台适配 | API 26、arm64；[桌面构建、权限与验收](desktop-port/README.md) |
| 手机 / 平板 | ArkUI 移动控制端 | 兼容 API 20、目标 API 26、arm64；[功能对照与验收](MOBILE_PARITY.md) |

两者仍为开发版。已完成的模拟器验证及未完成的真机验收见各端说明。

手机控制端的“设置 → 屏幕方向 → 连接建立后锁定横屏”默认关闭并保存在本机。开启后，手机连接时进入并保持横屏；关闭时跟随设备方向。平板和多窗口不受此选项影响。退出会话恢复首页方向，返回后台会话沿用该会话的方向设置。已移除检测手机姿态后解除初始横屏的逻辑。

## 移动端可信设备鉴权

手机和平板的“设置 → 本机配对信息”支持查看指纹和复制配对信息。在被控电脑的“管理可信设备 → 添加可信设备”中粘贴并核对指纹后，可以连接开启“仅允许可信设备控制”的电脑；连接密码仍需验证。

移动模式使用共享 Ed25519 挑战响应协议，私钥通过 AssetStore 持久保存并禁止同步，不放入缓存、日志或配对信息。重启、清理缓存、更换服务器和本机登录 ID 不改变公钥；已初始化的密钥损坏、不可用或丢失时报告错误，不自动换钥。清除应用数据或重新安装后需要重新配对。

## 构建

依赖仓库沿用 Windows/macOS/Linux 的 Xmake 策略：使用默认仓库或调用环境指定的 `XMAKE_GLOBALDIR` / `XMAKE_MAIN_REPO`，不固定或校验仓库提交。鸿蒙 PC 与移动端共用此规则，SDK、编译产物和依赖安装缓存仍按平台隔离。

在仓库根目录执行，当前构建脚本面向 macOS：

```bash
# 默认构建 PC：MiniRTC、Slint runtime、公共桌面应用和 HAP
python3 apps/harmony/scripts/build.py

# PC 增量构建：复用已生成的 runtime；补丁变化仍会触发重建
python3 apps/harmony/scripts/build.py --reuse-runtime

# 手机 / 平板：独立控制端，不包含 PC 采集和输入注入代码
python3 apps/harmony/scripts/build.py --mobile

# 仅准备 PC 的 MiniRTC 依赖；不再构建旧 ArkUI PC 核心
python3 apps/harmony/scripts/build.py --native-only

# 使用已有原生依赖重新打包
python3 apps/harmony/scripts/build.py --hap-only --reuse-runtime
python3 apps/harmony/scripts/build.py --mobile --hap-only
```

`--release` 选择 release HAP；原生依赖默认使用 release。PC 的安装、设备选择、版本号、权限和签名选项见 `python3 apps/harmony/scripts/build_desktop.py --help`，也可直接传给默认的 `build.py`。移动端仅构建控制端核心时使用 `--mobile --native-only`。

需要 DevEco Studio（SDK、Native SDK、Node、ohpm、hvigor、Java）、Xmake 3.1.1+ 和完整的 MiniRTC 子模块。PC 另需 Python 3.12+、Rust 1.92+ 和主机版 Slint compiler 1.17.1；移动端需 Python 3.10+。如果系统 Python 版本过旧，可使用 `/opt/homebrew/bin/python3`。

| 环境变量 | 用途 / 默认值 |
| --- | --- |
| `DEVECO_STUDIO_HOME` | `/Applications/DevEco-Studio.app/Contents` |
| `DEVECO_SDK_HOME` | `$DEVECO_STUDIO_HOME/sdk`，包含 `default/openharmony/native` |
| `XMAKE_BIN` | PATH 中的 `xmake`；本机也可指定 `$HOME/.local/bin/xmake` |
| `SLINT_COMPILER` | 主机 Slint compiler；默认查找 Xmake 的 1.17.1 缓存 |

脚本使用独立的 Xmake 配置与包缓存，不切换仓库根目录的桌面配置。Slint、httplib、Xmake 包仓库和 Python 构建工具版本固定。PC 与移动端构建共用锁，避免同时改写原生依赖。

## 工程与产物

| 客户端 | 生成的 DevEco 工程 |
| --- | --- |
| PC | `apps/harmony/.native/slint-poc/desktop-project` |
| 手机 / 平板 | `apps/harmony/.native/mobile-project` |

HAP 均位于对应工程的 `entry/build/default/outputs/default/entry-default-unsigned.hap`。PC 的 `slint-poc` 缓存名保留以复用现有 Rust 构建缓存，源码中已没有验证程序。PC 与手机 / 平板共用 `AppScope/app.json5` 和应用名称资源，包名统一为 `com.chunkdash.crossdesk`，名称统一为 **CrossDesk**。旧 PC 验证包 `cn.crossdesk.slintdesktop` 与新包属于不同应用，已有数据不会自动迁移。

使用 DevEco 打开生成工程进行调试和签名。请修改本目录源码后重新执行脚本；生成工程不是源码入口。移动端构建会重新生成源码与库目录；PC 首次从旧构建迁移时会清除旧桥接目标的产物。两端打包后均检查实际 HAP 的设备类型、权限、ArkTS、ARM64 库、SONAME 和依赖，拒绝混入已移除入口的库。

签名材料与本机配置放在仓库外或已忽略的 `.native` 中；PC 使用显式 `--signing-config`，不会继承移动工程的签名。MiniRTC 是独立 Git 子模块，交付时需要保留其中的鸿蒙适配改动。

## 代码与验证

- `desktop-port/`：PC 平台服务、窗口、输入法、Slint 适配及 runtime 补丁；业务逻辑和 UI 直接编译 `apps/desktop`。
- `entry/src/main/`：移动 ArkUI 页面与移动 N-API 桥接。
- `native/mobile_session.*`、`media.*`：移动控制会话与 NV12 视频显示。
- `native/audio.*`：两端共用的 OHAudio 播放服务。
- `native/capture*`、`input.*`：PC 录屏与被控输入；`key_mapping.h` 也由移动端使用。
- `libs/wire`：仓库公共协议。
- `libs/controller/include/file_transfer_core.h`：鸿蒙 PC、其他桌面平台和移动端共用的文件传输核心；`mobile_file_transfer_session.h` 仅保留移动端文件生命周期适配。

文件传输使用同一份分块、确认窗口、乱序重组、超时和取消逻辑，保持现有 wire 格式。PC 继续使用公共桌面的多文件队列、进度列表、目录设置及同名文件追加时间戳规则，不删除发送原文件；移动端继续使用文件选择器生成的临时副本，并在完成或失败后清理副本，接收文件仍保存在独立子目录。桌面接收状态按连接和对端隔离，反馈只接受对应对端的 ACK，断开时清理未完成的接收文件。

```bash
python3 apps/harmony/tests/run.py --desktop --mobile
python3 apps/harmony/tests/verify_hap.py
python3 apps/harmony/tests/verify_hap.py --mobile \
  apps/harmony/.native/mobile-project/entry/build/default/outputs/default/entry-default-unsigned.hap
```

主机侧测试覆盖协议、输入、采集、显示器、键盘钩子、窗口、输入法、自启动状态及移动生命周期。它们使用 SDK 声明与模拟系统接口；真机录屏、输入授权、多屏、音频、后台与长期会话仍需设备验收。
