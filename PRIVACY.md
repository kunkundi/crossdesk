# CrossDesk Privacy Policy

Last updated: September 28, 2026

[中文](#中文) | [English](#english)

## 中文

更新日期：2026 年 9 月 28 日

CrossDesk 重视您的个人信息与隐私保护。本政策说明我们在提供远程桌面服务时如何处理相关信息，以及您可以如何管理授权、保存的内容和数据请求。请在使用前阅读并了解本政策。

### 一、适用范围

本政策适用于 CrossDesk 桌面客户端、iOS 客户端，以及由 CrossDesk 项目维护者（以下简称“我们”）提供的默认网络服务。

如果您选择自行部署或使用其他运营者提供的服务器，该服务器所处理的信息、保存期限及安全管理由相应运营者负责。请在使用前了解其隐私政策。本政策中有关客户端本地数据处理的说明仍适用。

### 二、我们处理的信息及用途

**设备连接与身份验证。** 为登记设备、验证连接权限、查询在线状态及建立远程连接，客户端与服务端会处理设备 ID、认证信息及在线状态。服务端还会保存设备身份及认证校验信息、在线与离线时间、客户端版本与平台、设备关联记录，并生成必要的运行和安全日志，用于维持服务、排查故障及防止滥用。

**必要的网络信息。** 建立连接、网络地址转换穿透及中继转发需要处理 IP 地址、端口等网络信息。这些信息用于通信和网络诊断，不等同于读取设备的定位权限。连接服务、中继服务及网络请求可能产生相应日志。

**远程会话内容。** 您发起或接受远程连接后，软件根据使用的功能在参与会话的设备之间传输画面、音频、键盘和鼠标操作、剪贴板文本及文件。会话内容可能包含个人或工作信息，请仅连接您拥有或已获授权使用的设备，并按需管理共享内容。

**版本检查。** 桌面客户端启动后会获取版本信息；iOS 客户端在连接成功并收到远端版本信息后，会获取最新桌面版本信息，用于提示升级。该请求不携带设备 ID 或远端版本号，但网络服务会处理完成请求所必需的网络信息。

CrossDesk 未集成广告投放、广告追踪或用户行为分析功能。客户端不会主动上传诊断日志、崩溃报告或用户行为分析数据。您主动向我们提交问题或日志时，请先移除与问题无关的个人信息及连接凭据。

### 三、数据传输与相关服务

我们提供设备登记、连接协商、中继转发及版本检查服务。即使您使用自托管连接服务，客户端仍会按前述条件访问我们提供的版本服务。

远程会话内容优先在参与会话的设备间直接传输，必要时通过配置的中继服务器转发。中继服务用于实时转发，不提供会话录像或文件归档。客户端本地保存的画面预览和接收文件，按本政策相关说明处理。

自托管服务器由您选择的运营者管理。更换连接服务器后，设备登记、连接协商及中继数据将由相应运营者处理。

### 四、信息保存与安全措施

**客户端本地数据。** 软件会在本机保存连接设置、用户偏好、最近连接记录及必要的运行日志，以支持后续使用和故障排查。iOS 中的本机登录凭据及您选择记住的远程连接密码保存在系统钥匙串中，不通过 iCloud 钥匙串同步；旧版偏好中的本机登录凭据在成功迁入钥匙串后删除。

**远程画面预览。** iOS 默认不保存远程画面预览。只有您在“设置 → 隐私”单独开启并确认后，才会从下一次连接起，每次连接保存一张远程画面，用于最近连接预览；会话中会显示保存已开启的提示。预览仅保存在本机，不上传，并设置为不纳入设备备份。您可以单独清除预览；关闭保存或撤回隐私授权也会清除已有预览。旧版本未经此选择保存的预览会在升级后清理。

**接收的文件。** iOS 接收文件保存在本应用的文件目录中，您可通过系统“文件”应用管理或删除。清除画面预览不会删除接收文件或最近连接记录。

**服务端记录。** 为支持再次连接、服务运维和安全排查，设备身份、状态、关联记录及运行日志可能在连接结束后继续保存。保存范围和期限受服务运行需要、部署规则及适用的保留要求约束。您可以通过下述联系方式查询与您设备相关的信息及保存期限，或提出删除请求。撤回隐私授权、删除本地记录或卸载客户端，不会自动删除已产生的服务端记录；日志、备份和快照需分别核查和处理。

iOS 通过系统钥匙串和文件保护机制保护相应本地数据，默认信令连接进行 TLS 证书校验。请妥善保管设备和连接凭据，避免向无关人员共享访问密码。

### 五、iOS 权限与授权选择

**隐私授权。** 首次使用，或从未提供此选择的旧版升级时，您需阅读本政策、主动勾选并点击同意后，客户端才会登记本机身份、连接信令服务和查询设备在线状态。拒绝后仍可查看设置、离线隐私政策和本地连接记录，远程连接功能暂不可用。

**撤回同意。** 您可以在“设置 → 隐私 → 隐私与授权”中查看隐私政策、授权状态或撤回授权。客户端将停止远程会话、信令连接和版本检查，关闭并清除本地画面预览。再次使用连接功能前需重新同意；重新同意不会自动开启画面预览保存。撤回不影响撤回前已经发生的信息处理，也不等同于删除服务端数据。

**系统权限与会话功能。** iOS 客户端作为控制端接收远程电脑的画面与音频，不采集本机屏幕、摄像头或麦克风。局域网访问权限用于连接本地网络中的设备，您可在系统设置中管理。发送文件通过系统文件选择器选择，不要求开放整个文件库。当前 iOS 界面不读取或发送本机剪贴板文本；远端发送的剪贴板文本可在会话中写入本机剪贴板。

应用及通信组件使用本地文件元数据、应用偏好和计时接口实现文件传输、设置保存及网络计时，不使用这些信息制作设备指纹或进行广告追踪。

### 六、查询与删除请求

您可以管理本地最近连接记录、保存的画面预览及接收文件。删除某条最近连接记录时，其预览及已保存的远程连接密码也会删除。

如需查询、获取数据摘要或删除默认服务中与您设备相关的记录，请通过下述邮箱联系我们，说明设备 ID 和请求范围。为避免未经授权的披露或删除，我们会核实请求权限；请勿通过邮件发送访问密码、私钥或其他敏感凭据。

删除服务端设备身份后，原身份及对应凭据将失效，再次连接可能登记为新设备。数据库记录的删除不表示日志、备份或其他设备上已接收的内容同时删除。使用自托管服务时，请向相应服务器运营者提出服务端数据请求。

### 七、政策更新与联系方式

本政策可能随功能及服务方式调整而更新，更新后的内容和日期将在隐私政策中公布。您可随时在应用设置中查阅当前随应用提供的政策文本。

如对本政策或个人信息处理有疑问，请联系：

邮箱：[junkun.di@hotmail.com](mailto:junkun.di@hotmail.com)

---

## English

Updated: September 28, 2026

CrossDesk respects your privacy. This policy explains how information is processed when we provide remote desktop services and how you can manage consent, locally saved content and data requests. Please read this policy before using the software.

### 1. Scope

This policy applies to the CrossDesk desktop and iOS clients and the default network services provided by the CrossDesk project maintainers ("we" or "us").

If you deploy your own server or choose a server provided by another operator, that operator is responsible for its data processing, retention and security practices. Please review its privacy policy before use. The descriptions of local client data processing in this policy still apply.

### 2. Information processed and its purposes

**Device connections and authentication.** To register devices, verify connection permissions, query availability and establish remote connections, the clients and server process device IDs, authentication information and presence. The server also retains device identity and authentication verification records, online and offline times, client versions and platforms, and device associations. Necessary operational and security logs support service operation, troubleshooting and abuse prevention.

**Necessary network information.** Connections, network address translation traversal and relay forwarding require IP addresses, ports and other network information. This supports communication and network diagnostics and does not involve reading the device's location permission. Connection services, relay services and network requests may generate related logs.

**Remote-session content.** After you initiate or accept a remote connection, the software transfers content between participating devices according to the features used. This includes screens, audio, keyboard and mouse input, clipboard text and files. Such content may contain personal or work information. Connect only to devices you own or are authorized to access, and manage shared content accordingly.

**Version checks.** The desktop client retrieves release information after startup. After connecting and receiving the remote host's version, the iOS client retrieves the latest desktop release information to suggest updates. These requests do not contain device IDs or the remote host's version, but the service processes network information necessary to complete the request.

CrossDesk does not integrate advertising, advertising tracking or behavioral analytics. The clients do not proactively upload diagnostic logs, crash reports or behavioral analytics data. If you choose to submit an issue or logs, remove unrelated personal information and connection credentials first.

### 3. Data transmission and related services

We provide device registration, connection negotiation, relay forwarding and version checks. Even when you use a self-hosted connection service, the client still accesses our version service under the conditions described above.

Remote-session content is transmitted directly between participating devices where possible, or through the configured relay server when needed. The relay service forwards content in real time and does not provide session recording or file archiving. Locally saved previews and received files are handled as described in this policy.

Self-hosted servers are managed by the operator you choose. If you change the connection server, the corresponding operator will process device registration, connection negotiation and relay data.

### 4. Storage and security measures

**Local client data.** The software stores connection settings, preferences, recent connections and necessary operational logs on the device for subsequent use and troubleshooting. On iOS, local login credentials and remote connection passwords you choose to remember are stored in the system Keychain without iCloud Keychain synchronization. Legacy local login credentials in preferences are removed after successful migration to the Keychain.

**Remote desktop previews.** Saving previews is off by default on iOS. Only after you separately enable and confirm it in Settings → Privacy will the app save one remote frame per connection, starting with the next connection, for recent-connection previews. An indicator appears during sessions while saving is enabled. Previews remain local, are not uploaded and are marked as excluded from device backups. You can clear them separately. Disabling saving or withdrawing network consent also clears them. Previews saved by older versions without this choice are cleared on upgrade.

**Received files.** Received files on iOS are stored in the app's file directory and can be managed or deleted through Files. Clearing previews does not delete received files or recent-connection records.

**Server records.** Device identities, presence, associations and operational logs may remain after a connection ends to support reconnection, service operation and security troubleshooting. Their scope and retention depend on operational needs, deployment rules and applicable retention requirements. You can contact us below to ask about records and retention periods related to your device or request deletion. Withdrawing network consent, deleting local records or uninstalling the client does not automatically delete existing server records. Logs, backups and snapshots require separate review and handling.

On iOS, the system Keychain and file protection mechanisms protect the relevant local data, and default signaling connections validate TLS certificates. Keep your devices and connection credentials secure and avoid sharing access passwords with unrelated people.

### 5. iOS permissions and consent choices

**Privacy authorization.** On first use, or when upgrading from a version without this choice, you must read this policy, actively select the checkbox and agree before the client registers a local identity, connects to signaling or queries device presence. If you decline, settings, the offline policy and local connection history remain available, while remote connections are unavailable.

**Withdrawal.** You can review the privacy policy and your authorization status or withdraw consent in Settings → Privacy → Privacy and Authorization. The client stops remote sessions, signaling and version checks, disables preview saving and clears local previews. Consent is required again before reconnecting. Agreeing again does not automatically enable preview saving. Withdrawal does not undo processing that already occurred or delete server data.

**System permissions and session features.** The iOS client receives remote computer screens and audio as a controller; it does not capture the local screen, camera or microphone. Local network access supports connections to devices on your network and can be managed in system settings. File sending uses the system document picker without requiring access to the entire file library. The current iOS interface does not read or send local clipboard text. Text sent by the remote device may be written to the local clipboard during a session.

The app and its communication components use local file metadata, preferences and timing APIs for file transfer, settings and network timing, not for device fingerprinting or advertising tracking.

### 6. Access and deletion requests

You can manage local recent connections, saved previews and received files. Removing a recent connection also removes its preview and remembered remote connection password.

To request access to information, a data summary or deletion of records associated with your device in the default service, contact the email address below with the device ID and scope of your request. We verify the requester's authority to avoid unauthorized disclosure or deletion. Do not send access passwords, private keys or other sensitive credentials by email.

Deleting a server-side device identity invalidates that identity and its credentials; reconnecting may register a new device. Deleting database records does not mean that logs, backups or content already received by other devices are also deleted. For a self-hosted service, direct server-side data requests to its operator.

### 7. Policy changes and contact

This policy may be updated as features and services change. Updated text and its date will be published in the privacy policy. You can review the policy bundled with your app in its settings at any time.

For questions about this policy or the processing of personal information, contact:

Email: [junkun.di@hotmail.com](mailto:junkun.di@hotmail.com)
