# 更新 CrossDesk

[返回首页](../README_ZH.md) · [设置与更新](USER_GUIDE.md#settings) · [构建与发布](#升级器构建与发布)

桌面端可以在 **主菜单 → 关于 → 检查更新** 查看新版本。发现新版本后，先阅读更新说明，再按界面提供的“安装更新”或下载入口继续。

## 安装更新

1. 保存远端正在编辑的内容，等待文件传输完成。安装过程会中断连接。
2. 点击 **安装更新** 并确认，等待下载及校验完成。
3. 按操作系统提示提供安装授权。
4. 安装完成后等待 CrossDesk 重新打开，在“关于”中确认版本，再重新连接远端。

macOS / Linux 在准备阶段取消授权会保留原程序和连接。下载或校验失败时不会关闭主程序，可检查网络后重试。

## 为什么只有下载按钮

以下情况需要手动安装：

- 使用便携版或没有配置更新验证公钥的自行构建版本。
- 安装位置不受应用内更新支持。
- 当前发布包没有可用的签名更新信息，或校验未通过。

从 [官网](https://www.crossdesk.cn/) 或 [GitHub Releases](https://github.com/kunkundi/crossdesk/releases) 下载与你的系统和架构匹配的完整安装包，关闭 CrossDesk 后安装。保留原有安装类型；Linux 普通版和虚拟桌面版按实际用途选择。

## 各平台注意事项

| 平台 | 更新方式与要求 |
| --- | --- |
| Windows 安装版 | 使用完整安装包更新主程序、服务及辅助程序；需要管理员授权 |
| Windows 便携版 | 下载并解压完整便携包；若注册过服务，还需更新服务指向的目录，见 [服务管理](USER_GUIDE.md#windows-service-development-update) |
| macOS | 应用内安装支持 `/Applications/CrossDesk.app`；其他位置使用 PKG 手动安装 |
| Linux | DEB 安装版需要系统授权组件和可用的包管理器；更新会按已安装类型选择普通包或虚拟桌面包 |
| iOS / iPadOS | 从 App Store 更新；自行签名的应用按原签名方式重新安装 |
| Android / HarmonyOS | 按应用或发布页面提供的入口下载，沿用兼容的签名方式安装 |

Linux 自行配置了 systemd 或其他进程守护时，先停止相应服务，再安装更新，完成后重新启动。否则旧进程可能阻止安装。

## 更新失败怎么办

| 现象 | 处理方法 |
| --- | --- |
| 无法下载或校验失败 | 检查网络、系统时间及下载来源，重试或手动下载官方完整包 |
| 请求授权但未继续 | 确认系统授权窗口；Linux 检查桌面授权代理是否可用 |
| 提示程序仍在运行 | 退出其他用户会话中的 CrossDesk，并停止自行配置的守护服务后重试 |
| 安装后未自动打开 | 手动启动 CrossDesk；仍失败时用完整安装包修复安装 |
| 更新后锁屏输入不可用 | 确认 Windows 主程序、服务和会话辅助程序来自同一版本 |
| 仅构建日期不同却不提示更新 | 更新按数字版本及补丁号比较，仅日期变化不会触发升级 |

安装中断不会保证自动回滚。不要只替换一个可执行文件来修复安装，应使用对应平台的完整安装包。仍无法恢复时，通过 **反馈与建议 → 导出诊断日志** 提交问题，附上更新前后版本与错误内容。

## 自行构建版本

不设置更新公钥也能正常运行和检查版本，但需手动下载安装。启用应用内更新时，需配置 [构建参数](BUILD.md#update-public-key) 和下文的 [发布签名](#发布密钥)。

## 升级器构建与发布

Windows、macOS 和 Linux 的升级核心位于 `apps/desktop/src/updater`，平台实现位于 `apps/desktop/src/platform/<平台>/updater`。安装版随主程序构建升级器，便携版只提供手动下载。

### 安装协调流程

1. 用户点击“安装更新”并确认连接和文件传输将中断。
2. 主程序异步下载，显示进度，并验证签名、大小和 SHA-256。
3. 升级器从私有暂存目录启动。Windows 协调进程取得安装目录对应的维护锁。
4. macOS/Linux 先请求系统授权，再由提权后的安装进程再次验证签名、
   将包复制到只有管理员可写的私有目录并验证副本，再取得安装路径对应的系统维护锁。完成这些步骤后才
   通知主程序退出；等待授权期间远程连接继续运行，取消授权不会关闭
   主程序。准备阶段最多等待五分钟。
5. 主程序收到就绪通知后提交安装并正常退出。Windows 保持原有流程：
   先等待同一用户的 GUI 和守护进程退出，再启动提权安装进程验证副本。
6. 安装进程确认相关程序退出后调用平台安装器。macOS/Linux 未收到
   主程序的安装提交、收到取消通知或等待提交超时时，不执行安装。
7. 升级器以原调用者身份重启程序。新程序进入 GUI 事件循环后确认启动；
   90 秒内未收到确认会记录失败。启动确认不依赖远程服务器连通性。

系统代码签名不是这里的更新来源验证机制。Windows/macOS 安全检查、
管理员授权，以及现有 macOS 安装包的权限处理仍然适用。

## 发布密钥

客户端内置一个 Ed25519 公钥，发布端保存对应的私钥。不要把私钥提交到
仓库，也不要在普通构建或不可信 PR 的工作流中提供私钥。

用 OpenSSL 在可信环境生成密钥：

```sh
openssl genpkey -algorithm ED25519 -out crossdesk-update-private.pem
openssl pkey -in crossdesk-update-private.pem -pubout -outform DER -out crossdesk-update-public.der
python3 -c "from pathlib import Path; p=Path('crossdesk-update-public.der').read_bytes(); assert len(p)==44 and p[:12].hex()=='302a300506032b6570032100'; print(p[12:].hex())"
```

在 GitHub 仓库配置：

- Actions variable `CROSSDESK_UPDATE_PUBLIC_KEY`：上述 64 位小写十六进制公钥。
- Actions secret `CROSSDESK_UPDATE_PRIVATE_KEY`：PEM 私钥文件的完整内容。

桌面构建工作流将公钥传给 Xmake。只有发布清单生成步骤读取私钥。
首次需要手动安装包含升级器、公钥及新安装元数据的版本。

GitHub Actions 从分支手动打包时，在 `Build and Release` 的
`Run workflow` 中填写 `desktop_version`（例如 `1.7.2`），`patch` 保持
`0`。三个桌面平台会使用同一个数字版本并追加构建日期。空着不填的分支
构建使用 `0.0.0-日期`；版本标签构建继续使用标签中的版本号。
`desktop_version` 只影响桌面安装包，不改变移动端版本。

`prepare-version` 前置任务统一生成完整桌面版本，通过 job outputs 传给
Linux、macOS、Windows 和发布任务。无日期标签使用前置任务运行当天的
北京时间日期，显式日期标签使用标签中的日期；后续任务不再重新计算。
构建跨午夜或单独重跑发布任务时，继续使用前置任务确定的版本。

不要使用 `vauto-update-20261020` 这类分支名作为版本号验证升级。
新版本必须增加数字版本或 patch；只改变日期不视为升级。例如先手动安装
`v1.7.2-20261020`，再验证到 `v1.7.3-20261020` 的升级。
分支手动构建只上传 Actions 构建产物，不会自动发布版本清单。

发布脚本按数字版本和 patch 比较版本，忽略构建日期。编辑或重新发布旧的
桌面 Release 时，保留服务器现有清单并在 Actions 日志中说明跳过原因；
同版本仍可更新发布说明，新版本正常发布。各平台下载记录也不会被较低
版本覆盖，包括独立移动端发布和桌面 Release 附带的移动端安装包。

桌面 Release 中所有桌面安装包及便携包的完整版本必须与标签去掉 `v`
前缀后的版本一致，包括日期和 patch 的位置；不能只按数字版本比较。
任一包不匹配时，脚本报告标签、平台、文件名及双方版本，终止清单生成，
不写入输出文件。手动发布时应使用与安装包一致的完整版本标签，例如
`v1.7.4-20261021`。iOS、Android 安装包保持独立版本。

本地构建可以设置同名环境变量，或使用：

```sh
xmake f --CROSSDESK_UPDATE_PUBLIC_KEY=<64位小写十六进制公钥>
xmake b crossdesk
```

没有公钥的构建可以正常运行和检查版本，但不执行自动安装。
私钥与公钥不匹配时，清单生成失败。当前只支持单个公钥；更换公钥前需要
设计过渡版本，不能直接替换发布私钥而让旧客户端继续自动升级。

## 清单格式

`.github/scripts/update_version_manifest.py` 为安装包写入 `size`、`sha256`。
配置私钥后，桌面安装包还包含：

```json
{
  "update": {
    "payload": "{\"filename\":\"...\",\"platform\":\"windows-x64\",\"schema\":1,\"sha256\":\"...\",\"size\":123,\"url\":\"...\",\"version\":\"...\"}",
    "signature": "128位小写十六进制Ed25519签名"
  }
}
```

签名覆盖 `payload` 字符串的原始 UTF-8 字节；客户端先验签，再解析。
签名包含版本、平台、包名、下载地址、大小和哈希，避免下载记录被互换。
仅允许 `https://downloads.crossdesk.cn/` 下与版本和平台匹配的包名，
禁止下载重定向，包大小上限为 2 GiB，禁止同版本安装及降级。
便携包和移动端包不会获得此自动安装入口。

## 平台边界

- Windows：安装目录必须匹配 HKLM 卸载记录中的 `InstallLocation`。
  停止 CrossDesk 服务并等待其退出；确认同一路径的主程序全部结束后，
  以 `/S /CROSSDESK_UPDATE /D=...` 调用 NSIS。升级模式不按进程名强杀
  程序，且由升级器负责重启。原先运行的服务会尝试恢复。
- macOS：支持 `/Applications/CrossDesk.app`，通过系统授权调用
  `/usr/sbin/installer` 安装 PKG。其他安装位置使用手动下载。
  升级器只能依赖系统动态库，打包时验证这一点。
- Linux：支持 `/usr/lib/crossdesk/crossdesk-bin`，根据 `package-name`
  选择普通包或虚拟桌面包。使用 `pkexec` 授权和 `apt-get install`
  安装本地 DEB；禁止包管理器自动删除依赖。需要可用的授权代理。
  重启时恢复原来的 `--headless`、`--no-headless`、`--headless-display`、
  `--headless-size` 和 `--headless-session` 参数，包括安装失败后的重启；
  内部运行模式参数不会重放。
  自定义 systemd/headless supervisor 必须由管理员先协调停止；
  检测到仍在运行的程序会中止升级，不猜测或停止其他服务。

退出等待有时间限制，跨用户仍有程序运行会报告失败。维护锁在进程退出时
由系统释放。macOS/Linux 的锁位于系统运行目录，只有提权后的安装进程能持有。各用户通过状态 FIFO 查询维护状态，安装进程退出后状态自动清除。

## 失败处理与验证

下载、验签或准备失败时不关闭主程序。主程序退出后的安装失败会尝试
重启；macOS/Linux 在退出前取消授权时保留原程序和连接。
`last-update` 位于应用缓存目录，指向私有暂存目录；`request.json` 和
`result.json` 保留升级版本及结果，未完成的任务不会自动重放。新版本确认
启动后先通知升级器，再清除 `last-update`，避免临时目录被系统清理后
误报升级中断；旧客户端已经确认成功的记录也会在下次启动时清除。
升级器随后删除大型安装包，保留少量诊断文件。失败任务保留记录和下载包
便于排查。

NSIS、PKG 和 DEB 的安装操作不是跨平台事务。当前没有自动回滚承诺：
安装中断或新版本启动失败时，保留错误并提供手动修复安装路径。不能通过
只恢复 EXE 来回滚服务、依赖库或配置迁移。

```sh
xmake b update_checker_test version_checker_test
xmake r update_checker_test
xmake r version_checker_test
python3 -m unittest discover -s .github/scripts -p test_update_version_manifest.py
xmake lua apps/desktop/tests/build_define_scope_test.lua windows true
```

发布前仍须在各平台的隔离安装环境验证：授权取消、其他会话占用、服务
退出、安装失败、升级器自身更新和新程序启动确认。不要在开发机上通过
自动运行生产安装包来代替这些安装测试。
