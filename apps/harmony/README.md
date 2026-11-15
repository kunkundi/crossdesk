# CrossDesk HarmonyOS 使用与安装

[返回首页](../../README.md) · [PC 使用说明](desktop-port/README.md) · [手机和平板使用说明](MOBILE_PARITY.md)

鸿蒙客户端目前为开发版，需要自行构建并签名后安装。

| 设备 | 系统要求 | 可以做什么 |
| --- | --- | --- |
| PC（2in1） | API 26、arm64 | 控制其他电脑，也可在获得系统权限后作为被控电脑；提供桌面墙 |
| 手机 / 平板 | API 20+、arm64 | 通过设备 ID 控制电脑，不提供手机或平板被控功能 |

真机的录屏与输入授权、多屏、后台连接及长时间会话仍有兼容性限制。先在自己的设备上确认所需功能可用，再用于依赖无人值守的场景。

## 连接电脑

1. 安装对应设备类型的已签名 HAP。
2. 在被控电脑运行 CrossDesk，取得设备 ID 和密码。
3. 打开鸿蒙客户端，完成联网授权，输入电脑 ID 并连接，再按提示填写密码。
4. 自建服务器用户需在两端配置同一服务器。

手机和平板的触控、键盘、文件和画中画操作见 [移动端指南](MOBILE_PARITY.md)。PC 的桌面墙与日常操作见 [桌面使用指南](../../docs/USER_GUIDE.md)。

## 自行构建

先下载源码及子模块：

```sh
git clone --recurse-submodules https://github.com/kunkundi/crossdesk.git
cd crossdesk
```

已有仓库时运行 `git submodule update --init --recursive`。

在仓库根目录执行，当前构建脚本面向 macOS：

```bash
# 构建 PC 安装包
python3 apps/harmony/scripts/build.py

# 再次构建 PC，复用已有依赖
python3 apps/harmony/scripts/build.py --reuse-runtime

# 构建手机 / 平板安装包
python3 apps/harmony/scripts/build.py --mobile

# 仅准备 PC 原生依赖
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


## 签名与安装

| 客户端 | 用 DevEco 打开的生成工程 |
| --- | --- |
| PC | `apps/harmony/.native/slint-poc/desktop-project` |
| 手机 / 平板 | `apps/harmony/.native/mobile-project` |

未签名 HAP 位于相应工程下的 `entry/build/default/outputs/default/entry-default-unsigned.hap`。使用 DevEco 打开生成工程，选择自己的签名配置并安装到设备；未签名包不能直接视为可安装包。

PC 和移动端的应用名均为 CrossDesk，包名均为 `com.chunkdash.crossdesk`。签名、版本和设备类型需匹配；旧 PC 验证包 `cn.crossdesk.slintdesktop` 的数据不会自动迁移。

再次执行构建脚本会重新生成工程，先在工程外保存签名配置。PC 可通过 `--signing-config` 指定配置，所需权限见 [PC 权限与签名](desktop-port/README.md#permissions)。

## 更新与反馈

使用相同签名生成新版本后安装；不要通过卸载来绕过签名不匹配，卸载可能丢失本机记录和配对身份。没有对应平台更新信息时，应用不会提供可用的鸿蒙更新包。

反馈时请提供设备型号、系统版本、PC 或移动端类型、客户端版本、授权状态及复现步骤。
