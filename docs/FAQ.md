# 常见问题 / FAQ

[返回 README](../README.md) · [English](#english)

## 未连接服务器，或提示 TLS 证书错误

先确认网络可用、两端使用预期的服务器配置。自托管环境检查信令端口、证书有效期、主机名匹配和系统信任；详见 [自托管指南](SELF_HOSTING.md)。这一步解决后，再排查远程设备和 P2P 连接。

## 已连接服务器，但远程设备离线

确认被控端 CrossDesk 仍在运行，双方连接同一个信令服务，并重新复制被控端当前的本机 ID。切换服务器后，原来的 ID 和连接记录可能不再适用。

## 对等连接失败

默认连接策略已允许 TURN 中继回退。可在 **☰ → 设置** 中开启 **强制中继连接**，保存后重新连接以排查直连问题。自托管用户还需检查 Coturn 是否正常运行，以及中继端口 TCP/UDP、媒体端口范围 UDP 是否放通。

公共服务的兼容性变更也会影响旧客户端；请核对 [Release 说明](https://github.com/kunkundi/crossdesk/releases)，尽量让两端使用兼容的近期版本。

## macOS 有连接但黑屏，或无法操作键鼠

在“系统设置 → 隐私与安全性”中检查 CrossDesk 的屏幕录制与辅助功能权限。更换应用位置或签名、更新开发构建后，可能需要重新授权并重启应用。

同一团队正式签名且应用标识与签名要求兼容的版本，正常升级通常可以沿用权限。从未签名或 ad-hoc 旧版升级后，退出旧版并启动新版，新版会在后台处理当前用户的辅助功能和录屏权限迁移，再引导重新授权。每项权限的完成状态会单独保存。

迁移失败或中断时，打开“设置 → 系统权限”，点击“重试权限迁移”。也可选择“稍后”继续使用客户端，受影响的被控功能在授权后启用。若系统设置仍显示已授权但功能异常，彻底退出 CrossDesk，在对应权限列表中移除旧条目后重新添加 `/Applications/CrossDesk.app`，再启动应用。

问题仍存在时，再次退出 CrossDesk，在当前登录用户的终端中仅执行与故障对应的一条命令，重置 CrossDesk 的该项授权。

```bash
# 录屏权限异常
tccutil reset ScreenCapture cn.crossdesk.app
# 辅助功能权限异常
tccutil reset Accessibility cn.crossdesk.app
```

执行后重新启动 CrossDesk，按提示在系统设置中授权。仍有旧路径或旧版本条目时，可在对应权限列表中移除旧条目后重新添加当前应用。

## Linux Wayland 无法捕获画面

Wayland 画面捕获依赖启用了 Wayland 支持的构建、宿主系统 PipeWire 0.3 和桌面门户授权；无法捕获时也可在 X11 会话中对比验证。构建选项见 [Linux 构建说明](BUILD.md#linux)。

## Linux 电脑未连接显示器或没有图形会话

正常启动 CrossDesk 即可自动发现并选择当前用户的已有 X11 桌面，保留原来的 Dock 和应用，无需知道显示编号；多个会话会根据登录状态和活动状态自动选择。只看到 CrossDesk、背景一片黑，通常是创建了未运行桌面环境的裸 Xvfb；请改为复用原桌面。需要独立虚拟桌面时安装 `xvfb xfce4 dbus`，使用 `--headless-size 1920x1080 --headless-session startxfce4`。部署与限制见 [Linux 无头运行](HEADLESS.md)。

## Windows 锁屏后无法操作

在被控电脑检查 CrossDesk Service 是否安装、是否运行，并保持 CrossDesk 客户端运行。便携版可在“设置 → 锁屏控制服务”安装。服务状态检查命令见 [Windows 服务说明](../README.md#windows-service)。

“断开连接后自动锁屏”可用不代表锁屏控制服务可用：锁屏由客户端调用系统功能，输入登录密码则需要服务。若 Android 手机能看到登录界面但输入法无法输入，可在远程键盘中切换到 **电脑键盘**，对比数字、字母与回车是否有效；反馈时注明两端版本、安装版或便携版，以及两种键盘的表现。

若电脑键盘可用，而搜狗等系统输入法不可用，请确认被控端已安装包含文本转发修复的新 Windows 构建，并一并更新客户端、服务与会话辅助程序。旧版的按键转发与输入法文本转发是不同通道，仅更新手机端不会修复旧 Windows 的文本通道。本次修复转发完整文本，保留大小写、符号、空格及中文等字符，不限于数字密码；仍需在更新后的 Windows 锁屏、登录界面验证。

若日志出现 `Secure desktop text injection failed: error=unknown_command`，说明文本已到达 Windows 客户端，但服务或会话辅助程序不支持该指令。直接运行重新编译的 `CrossDesk.exe` 不会将已注册的服务切换到新构建目录；旧服务也可能导致解锁后的记事本无法接收文本。请按[开发构建的服务更新步骤](USER_GUIDE.md#windows-service-development-update)，从完整的新构建目录重新注册并启动服务后再测试。

## 移动端连接后没有进入隐私屏

Android、iOS 和鸿蒙原生移动端可直接点击悬浮菜单的隐私屏按钮切换，按钮按电脑确认的状态显示“隐私屏已开启”或“隐私屏已关闭”，开启与关闭使用相同配色，无需进入详情页；开启时与桌面端一致屏蔽电脑本地键鼠，不提供独立屏蔽选项。自动开启还需要电脑勾选“被连接时开启隐私屏”，且电脑当前桌面支持遮罩；锁屏、登录界面或无人值守被控端不提供隐私遮罩。

v1.7.3-1 的 Android 端尚未实现上述控制能力，需要使用包含该功能的新构建。若一直等待状态，请检查电脑端版本；若显示不可用或失败，请查看控制栏内的电脑返回原因。当前 PC 策略要求所有已连接控制端均支持隐私屏，旧版或 Web 控制端同时在线时仍会阻止开启。“断开连接后自动锁屏”是独立功能。Windows 被控端仍可通过 Ctrl+Alt+Shift+F12 应急恢复本地显示与输入。

## 被控 Windows 电脑没有接显示器

没有显示器时，Windows 会把桌面放在一个分辨率固定的虚拟目标上，GPU 采集路径不可用。CrossDesk 会先尝试插入内置的 usbmmidd 虚拟显示器（见下一节）；无法插入时自动进入**无屏兼容模式**：用 GDI 采集，分辨率由系统决定（常见为 1024×768），帧率和画质低于正常模式，被控端日志会记录进入兼容模式的原因。键鼠控制、隐私屏、文件传输不受影响。

需要完整体验时，给显卡接一个 HDMI/DP 假负载（EDID 模拟器）或真实显示器，然后重新连接；CrossDesk 会在下一次连接时重新检测并切回 GPU 采集。兼容模式只在“采集方式”为“自动”时启用；手动指定采集方式时按所选方式工作。

### 自动插入 usbmmidd 虚拟显示器（免假负载）

Windows 安装包和便携包内置了 Amyuni 的 `usbmmidd_v2` 虚拟显示器驱动（位于安装目录的 `usbmmidd_v2` 文件夹，附带 Amyuni 的 License.txt）。无屏时 CrossDesk 会自动使用它，每次连接时：

1. CrossDesk 检测到没有任何显示器且采集方式为“自动”；
2. 先以兼容模式开始传输，同时在后台通过 CrossDesk Service 的会话助手（管理员权限、交互会话内）安装驱动（仅首次，驱动带微软签名，无需测试模式）、插入一块虚拟显示器并设置为 1920×1080；
3. 虚拟显示器就绪后采集自动切到 DXGI/WGC 正常模式；
4. 会话结束时自动拔出虚拟显示器，接入真实显示器后不会残留。

未安装 CrossDesk Service 时，只有以管理员身份运行的 CrossDesk 才能自行插拔；否则日志会记录 `usbmmidd_installer_launch_failed:740` 并保持兼容模式。

卸载 CrossDesk 时，安装包会移除由 CrossDesk 安装的这块驱动；如果驱动是你为其他软件事先安装的，则保持不动。便携版不带卸载程序，需要移除时在便携目录的 `usbmmidd_v2` 下以管理员身份运行 `deviceinstaller64 enableidd 0`、`deviceinstaller64 stop usbmmidd`、`deviceinstaller64 remove usbmmidd`，或在设备管理器中卸载“USB Mobile Monitor Virtual Display”。

调试时不方便拔线，可以设置环境变量 `CROSSDESK_FORCE_HEADLESS=1` 再启动 CrossDesk：在没有 usbmmidd 显示器时按无屏处理，走完插屏流程后恢复真实检测。仅用于验证，不要在生产环境设置。

## 修改密码后连接失败

修改密码时使用 6 位数字或英文字母，并保持连接服务器。等待修改成功、客户端重新连接后，重新复制当前密码。控制端保存的旧密码会失效，按弹窗输入新密码再连接。

## 收到的文件在哪里

桌面端见 **☰ → 设置 → 文件保存路径**。iOS 原生端收到的文件保存在应用的 `Documents/Received`，可以从分享入口导出；更多说明见 [iOS 开发文档](../apps/ios/README.md)。

## 关闭主窗口后为什么还能连接

关闭主窗口会隐藏窗口，客户端仍在后台运行。需要结束程序时，从系统托盘或 macOS 菜单栏中选择退出。

## Windows 没有安装 CUDA，如何编译

可以先使用 `--USE_CUDA=false` 构建。需要 CUDA 编解码支持时，按 [构建指南](BUILD.md) 安装对应依赖；PowerShell 中设置环境变量应使用 `$env:CUDA_PATH = "实际安装目录"`。

## iOS 构建产物为什么不能直接安装

当前 CI 导出的是未签名应用。需要使用自己的开发签名 / 分发方式安装；也可用 Xcode 打开工程并选择真机运行。见 [iOS 构建要求](../apps/ios/README.md)。

---

<a id="english"></a>

## English

[Back to README](../README_EN.md)

| Symptom | What to check |
| --- | --- |
| Server disconnected / TLS error | Network access, signaling endpoint, certificate dates, hostname, and system trust. See [self-hosting](SELF_HOSTING_EN.md). |
| Remote device offline | Keep the host running, connect both sides to the same signaling server, and copy its current ID again. |
| P2P connection failed | The default connection policy already allows TURN relay fallback. Enable Force Relay in Settings, save, and reconnect to troubleshoot direct connections. For self-hosting, check Coturn and relay/media firewall ports. Review [release compatibility notes](https://github.com/kunkundi/crossdesk/releases). |
| Black screen / no input on macOS | Grant Screen Recording and Accessibility, then reopen the app. Release updates with compatible signing identities normally retain permissions. For migration or stale grants, see [macOS permission repair](#repair-macos-permissions). |
| No capture on Linux Wayland | Check the Wayland build option, host PipeWire 0.3, and desktop portal permission. Compare with an X11 session. See [Linux build instructions](BUILD_EN.md#linux). |
| Linux host has no monitor or graphical session | Start CrossDesk normally to automatically discover and select your existing desktop, preserving its Dock and apps; no display number is needed. A bare Xvfb shows only CrossDesk on a black background; use an existing session or install Xfce and specify `--headless-session startxfce4`. See [Linux headless operation](HEADLESS_EN.md). |
| Cannot control a Windows lock screen | Install/start CrossDesk Service and keep the host client running. See [service commands](../README_EN.md#windows-service). |
| Android can connect but cannot type a Windows login password | Locking on disconnect does not verify service availability. Check CrossDesk Service, then compare the phone IME with **Computer keyboard** in the remote keyboard panel. Include both client versions, installer/portable edition, and both keyboard results in the report. |
| Privacy screen does not activate for a mobile connection | Toggle **Privacy screen** directly in the native Android, iOS or HarmonyOS floating menu. The button labels the host-confirmed on/off state without changing colors; normal toggling shows no extra text; errors and unavailable status still appear inline. Android v1.7.3-1 lacks this feature; use a build containing it. Automatic activation also needs the host setting and a supported unlocked desktop. The current host policy requires every connected controller to support privacy; older or Web controllers can prevent activation. Locking on disconnect is independent. |
| Windows host has no monitor attached | The Windows installer and portable archive bundle Amyuni's `usbmmidd_v2` virtual display driver (in the `usbmmidd_v2` folder with its License.txt). On each connection to a host without a monitor (capture method Auto), CrossDesk starts streaming in compatibility mode and, in the background, asks the CrossDesk Service session helper to install the Microsoft-signed driver (first time only) and plug one virtual monitor at 1920×1080; capture then switches to DXGI/WGC on that monitor and it is unplugged when the session ends. Without the service only an elevated CrossDesk can plug it itself; otherwise the host stays in **headless compatibility mode**: GDI capture at an OS-fixed resolution (often 1024×768) with reduced frame rate; the host log records why. Input, privacy screen and file transfer still work. A real monitor or an HDMI/DP dummy plug also restores GPU capture. |
| Removing the bundled virtual display driver | The uninstaller removes the driver when CrossDesk installed it and leaves a driver you installed yourself. For the portable build run `deviceinstaller64 enableidd 0`, `deviceinstaller64 stop usbmmidd` and `deviceinstaller64 remove usbmmidd` as administrator from the `usbmmidd_v2` folder, or uninstall "USB Mobile Monitor Virtual Display" in Device Manager. |
| Saved password no longer works | Use 6 ASCII letters or digits when changing the password, with the host connected to the server. Wait for the change and reconnection to complete, then copy the current password and enter it on the controller. |
| Cannot find received files | Check Settings → File Save Path on desktop. Native iOS stores them in `Documents/Received` and provides a share action. |
| Main window closed but app still running | Closing hides the window. Use the tray/menu-bar exit command to quit. |
| Windows build without CUDA | Build with `--USE_CUDA=false`, or follow the [CUDA build instructions](BUILD_EN.md). Use `$env:CUDA_PATH` in PowerShell. |
| iOS artifact cannot be installed directly | CI exports an unsigned app. Sign it yourself or build and run from Xcode on a physical device. See [iOS requirements](../apps/ios/README.md). |

### Repair macOS permissions

Release upgrades with compatible signing identities normally retain permissions. After upgrading an unsigned or ad-hoc build, quit the old app and launch the new version. It migrates Accessibility and Screen Recording in the background for the current user, then guides you through authorization. Completion is saved separately for each permission.

For failed or interrupted migrations, open **Settings → System permissions** and choose **Retry migration**. Choose **Later** to continue using the client. Affected host features become available after authorization. If access appears enabled while capture or input still fails, quit CrossDesk, remove the old entry from the affected privacy list, add `/Applications/CrossDesk.app` again, and reopen the app.

If the issue persists, quit CrossDesk and run only the command for the affected permission in the logged-in user's terminal.

```bash
# Screen Recording permission issue
tccutil reset ScreenCapture cn.crossdesk.app
# Accessibility permission issue
tccutil reset Accessibility cn.crossdesk.app
```

Reopen CrossDesk and grant the requested permission in System Settings. If entries for older paths or builds remain, remove the old entry from the affected permission list and add the current app again.

If the issue persists, [open an issue](https://github.com/kunkundi/crossdesk/issues) with both OS versions, client versions, connection mode, and reproduction steps. Remove passwords and private device information from logs or screenshots before sharing them.
