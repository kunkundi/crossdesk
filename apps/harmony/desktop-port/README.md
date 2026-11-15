# CrossDesk 鸿蒙 PC 使用说明

[鸿蒙安装说明](../README.md) · [桌面使用指南](../../../docs/USER_GUIDE.md)

鸿蒙 PC 客户端需要 API 26、arm64 的 PC / 2in1 设备。它提供与其他桌面平台相近的连接窗口、远程会话、桌面墙和文件传输，目前仍为开发版。

## 控制另一台电脑

在主窗口输入对方设备 ID，完成密码验证后打开远程画面。工具栏可切换显示器、传文件、调节声音和画面；多设备使用桌面墙。具体操作见 [桌面使用指南](../../../docs/USER_GUIDE.md)。

系统可能保留部分组合键。未获得全局键盘权限时，普通窗口内的按键仍可使用，但不能保证所有系统快捷键都转发到远端。后台剪贴板同步也取决于系统授权。

## 让其他设备控制这台鸿蒙 PC

保持 CrossDesk 运行并连接服务器，将本机 ID 和密码交给控制端。首次连接时按系统提示授权录屏及键鼠控制；拒绝授权时无法提供相应功能。

带有持久权限的签名包可以引导你在系统设置中开启 **屏幕共享** 和 **辅助操作**。打开设置页面本身不等于授权成功，请确认两项开关的实际状态。普通未配置这些权限的包不会提供相同入口。

持久权限用于减少运行中重复连接时的确认，不代表已经支持所有登录前、锁屏、休眠恢复或重启后的无人值守场景。这些功能仍需在目标真机确认。

## 自启动与关闭窗口

在 **系统设置 → 应用和元服务 → 应用启动管理 → 添加应用** 中选择 CrossDesk；关闭时从该列表移除。应用内的自启动入口会显示操作引导与系统状态，可点击“刷新状态”。

关闭主窗口后，支持托盘的系统会保留后台程序，可从托盘恢复或退出。需要完全结束远程访问时，断开连接并退出程序。

<a id="permissions"></a>

## 自行构建时的权限与签名

构建步骤见 [鸿蒙安装说明](../README.md#自行构建)。在真机使用受限功能，需要签名 Profile 中的 ACL（受限权限许可）和用户实际授权；仅声明权限或编译成功不代表获准使用。

| 功能 | 所需权限或构建选项 |
| --- | --- |
| 持久屏幕共享 | `CUSTOM_SCREEN_RECORDING`，`--host-permissions` |
| 持久键鼠控制 | `CONTROL_DEVICE`，`--host-permissions` |
| 控制端全局快捷键 | `HOOK_KEY_EVENT`，`--desktop-permissions` |
| 后台读取剪贴板 | `READ_PASTEBOARD`，`--desktop-permissions` |
| 被控端后台鼠标位置反馈 | `INPUT_MONITORING`，`--cursor-position-permission` |

请根据自己的使用场景申请权限。控制端快捷键权限不能替代被控端的键鼠控制权限。

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

## 使用限制与排查

- 录屏或输入不可用时，先检查签名包是否包含对应权限，再检查系统授权状态。
- 不能保证所有固件都支持系统快捷键拦截、后台鼠标位置反馈和剪贴板同步。
- 多屏和混合缩放、真机声音、批量会话及长时间连接仍有待确认的兼容范围。
- 旧验证包 `cn.crossdesk.slintdesktop` 与当前 `com.chunkdash.crossdesk` 是不同应用，已有数据不会自动迁移。

反馈时请注明硬件型号、系统版本、安装包版本、签名权限和已完成的授权步骤，并描述实际出现的问题。
