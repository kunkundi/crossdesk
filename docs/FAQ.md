# 常见问题 / FAQ

[返回 README](../README_ZH.md) · [English](#english)

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

在被控电脑检查 CrossDesk Service 是否安装、是否运行，并保持 CrossDesk 客户端运行。便携版可在“设置 → 锁屏控制服务”安装。服务状态检查命令见 [Windows 服务说明](../README_ZH.md#windows-service)。

“断开连接后自动锁屏”可用不代表锁屏控制服务可用：锁屏由客户端调用系统功能，输入登录密码则需要服务。若 Android 手机能看到登录界面但输入法无法输入，可在远程键盘中切换到 **电脑键盘**，对比数字、字母与回车是否有效；反馈时注明两端版本、安装版或便携版，以及两种键盘的表现。

若电脑键盘可用而手机系统输入法不可用，请用完整 Windows 安装包更新被控电脑，使客户端、服务和会话辅助程序来自同一版本。只更新手机端或替换 `CrossDesk.exe` 可能仍使用旧服务。

若日志出现 `Secure desktop text injection failed: error=unknown_command`，通常表示服务与主程序版本不匹配。安装版请用完整安装包修复；自行部署构建的用户按 [Windows 服务更新步骤](USER_GUIDE.md#windows-service-development-update) 更新服务后重连。

## 移动端连接后没有进入隐私屏

Android、iOS 和鸿蒙原生移动端可在悬浮菜单直接开关“隐私屏”，按钮显示电脑确认的状态。开启时会遮住电脑画面并屏蔽本地键鼠。自动开启还需电脑勾选“被连接时开启隐私屏”，并处于支持遮罩的桌面；锁屏、登录界面及尚未附加用户界面的无人值守模式不提供遮罩。

旧客户端可能不提供隐私屏控制，请确认两端已更新且移动端有该入口。若提示失败，查看电脑返回的原因；旧版或 Web 控制端同时在线时可能阻止开启。“断开连接后自动锁屏”是独立功能。Windows 被控端可通过 Ctrl+Alt+Shift+F12 应急恢复本地显示与输入。

## 被控 Windows 电脑没有接显示器

CrossDesk 会自动选择可用的采集方式，并尝试使用随 Windows 安装包或便携包提供的虚拟显示器。请确认 CrossDesk Service 已安装且可运行；未安装服务时，普通权限下可能无法准备虚拟显示器。

无人值守模式会优先恢复已记录的显示分辨率，没有记录时使用 1920×1080。驱动不可用时，仍可能进入系统提供的兼容画面，分辨率和帧率会降低。接入真实显示器后重新连接，也可以检查是否恢复正常画面。

安装包附带的驱动位于 `usbmmidd_v2`，许可文件随包提供。卸载安装版会移除由 CrossDesk 安装的驱动，原先为其他软件安装的驱动保持不动。便携版需要手动移除时，可在设备管理器中卸载“USB Mobile Monitor Virtual Display”。

## 修改密码后连接失败

密码使用 6 位数字或英文字母，区分大小写。ID 模式修改时需连接服务器，等待成功并重新连接后使用新密码；IP 直连使用独立密码，修改成功后立即生效。控制端保存的旧密码需要重新输入。

## 密码正确，仍提示不是可信设备

被控电脑可能开启了“仅允许可信设备控制”。先在控制端复制配对信息，再到被控端的可信名单添加并核对指纹；手机入口为 **设置 → 本机配对信息**。旧客户端和当前终端 CLI 不支持此认证。换设备或清除应用数据后可能需要重新配对，详见 [可信设备说明](CONNECTIONS.md#trusted-devices)。

## 收到的文件在哪里

桌面端见 **☰ → 设置 → 文件保存路径**。iOS 收到的文件位于应用的 `Documents/Received`，可从分享入口导出。Android 和鸿蒙通过会话中的保存 / 导出入口保留文件；Android 会在结束会话后清除临时接收文件，请在断开前导出。

## 关闭主窗口后为什么还能连接

关闭主窗口会隐藏窗口，客户端仍在后台运行。需要结束程序时，从系统托盘或 macOS 菜单栏中选择退出。

## Windows 没有安装 CUDA，如何编译

可以先使用 `--USE_CUDA=false` 构建。需要 CUDA 编解码支持时，按 [构建指南](BUILD.md) 安装对应依赖；PowerShell 中设置环境变量应使用 `$env:CUDA_PATH = "实际安装目录"`。

## iOS 构建产物为什么不能直接安装

从 [App Store](https://apps.apple.com/app/id6828859211) 可直接安装。GitHub 构建产物若标为未签名，需要自己的签名才能装到设备上，也可通过 Xcode 签名运行。见 [iOS 构建要求](../apps/ios/README.md)。

---

<a id="english"></a>

## English

[Back to README](../README.md)

| Symptom | What to check |
| --- | --- |
| Server disconnected / TLS error | Network access, signaling endpoint, certificate dates, hostname, and system trust. See [self-hosting](SELF_HOSTING_EN.md). |
| Remote device offline | Keep the host running, connect both sides to the same signaling server, and copy its current ID again. |
| P2P connection failed | The default connection policy already allows TURN relay fallback. Enable Force Relay in Settings, save, and reconnect to troubleshoot direct connections. For self-hosting, check Coturn and relay/media firewall ports. Review [release compatibility notes](https://github.com/kunkundi/crossdesk/releases). |
| Black screen / no input on macOS | Grant Screen Recording and Accessibility, then reopen the app. Release updates with compatible signing identities normally retain permissions. For migration or stale grants, see [macOS permission repair](#repair-macos-permissions). |
| No capture on Linux Wayland | Check the Wayland build option, host PipeWire 0.3, and desktop portal permission. Compare with an X11 session. See [Linux build instructions](BUILD_EN.md#linux). |
| Linux host has no monitor or graphical session | Start CrossDesk normally to automatically discover and select your existing desktop, preserving its Dock and apps; no display number is needed. A bare Xvfb shows only CrossDesk on a black background; use an existing session or install Xfce and specify `--headless-session startxfce4`. See [Linux headless operation](HEADLESS_EN.md). |
| Cannot control a Windows lock screen | Install/start CrossDesk Service and keep the host client running. See [service commands](../README.md#windows-service). |
| Android can connect but cannot type a Windows login password | Locking on disconnect does not verify service availability. Check CrossDesk Service, then compare the phone IME with **Computer keyboard** in the remote keyboard panel. Include both client versions, installer/portable edition, and both keyboard results in the report. |
| Privacy screen does not activate for a mobile connection | Use **Privacy screen** in the native mobile floating menu. Update both clients if the entry is missing. Automatic activation also requires the host setting and a supported user desktop; older or Web controllers connected at the same time may prevent it. Locking on disconnect is separate. |
| Windows host has no monitor attached | CrossDesk automatically selects a capture method and tries the bundled virtual display. Check that CrossDesk Service is installed and running. Unattended mode prefers a saved resolution, otherwise 1920×1080. If the driver is unavailable, resolution and frame rate may be limited. Reconnect after attaching a physical monitor to check recovery. |
| Removing the bundled virtual display driver | The installer removes a driver installed by CrossDesk. For a portable build, uninstall “USB Mobile Monitor Virtual Display” in Device Manager when it is no longer needed. |
| Saved password no longer works | Use six case-sensitive ASCII letters or digits. ID password changes require a server connection; wait for confirmation. Direct IP has a separate password that applies immediately after a successful change. Enter the new password on the controller. |
| Correct password but device is not trusted | Copy the controller’s pairing information, add it to the host’s trusted list, and compare fingerprints. Mobile: Settings → This device’s pairing info. Older clients and the current terminal CLI do not support this restriction. |
| Cannot find received files | Desktop: Settings → File Save Path. iOS: share from `Documents/Received`. Android / HarmonyOS: use the save/export action. Export Android files before disconnecting, as its temporary received files are cleared when the session ends. |
| Main window closed but app still running | Closing hides the window. Use the tray/menu-bar exit command to quit. |
| Windows build without CUDA | Build with `--USE_CUDA=false`, or follow the [CUDA build instructions](BUILD_EN.md). Use `$env:CUDA_PATH` in PowerShell. |
| iOS artifact cannot be installed directly | Install from the [App Store](https://apps.apple.com/app/id6828859211), or sign the unsigned build yourself. See [iOS installation](../apps/ios/README.md). |

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
