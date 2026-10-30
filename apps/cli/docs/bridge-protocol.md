# Bridge v1（CrossDesk 私有协议，不是 MCP）

启动：`crossdesk-cli bridge --stdio [--password-fd 3] [--server host] [--port 9099] [--quality low|balanced|high|ultra]`。
通过参数数组启动，不经 shell。stdin/stdout 为 UTF-8 NDJSON，每行一个完整 JSON。stdout 使用保留的描述符，底层 stdout 重定向到 stderr。宿主应消费/隔离 stderr，不能展示为视频行或模型消息。bridge 不要求 TTY。

每条请求含整数 `id` 和字符串 `method`。响应含同一 `id`、布尔 `ok`，成功时还含 `version:1`、完整 `status`；失败含固定 `error`（无效请求、版本或操作不可用等），不回显原输入/凭据。无有效 id 的错误为 null。事件没有 id。先 hello，再发送其他操作。

| method | 字段 | 行为 |
|---|---|---|
| hello | version:1 | 协议版本握手 |
| viewport | columns:1..240, rows:1..100, colors:8/24/256, utf8:boolean, fps:1..30；可选 detail:quadrant/half, quality:low/balanced/high/ultra, zoom:1..32, native:boolean | 设置画面行数（不含状态栏）、本地目标与预览档位；默认 quadrant/high/1、native=false，保留未显式指定的 detail/quality/zoom/native；不探测宿主 TTY |
| connect | device:9位数字字符串, display:0..63（默认0） | 从专用 password fd 读取至换行/EOF（最多256字节、等待5秒），开始异步注册/连接；ok不是远端已连接 |
| status | 无 | 返回当前状态 |
| control | enabled:boolean | 显式启停控制；关闭时释放输入 |
| input | data:string，最多16KiB | 终端原始按键/UTF-8/bracketed paste/SGR鼠标；只读时 c 开启控制、q/Ctrl+C 退出，d/鼠标点击进入细节，+/-/滚轮缩放，方向键或 i 上、j 左、k 下、l 右平移，f 全景；Ctrl+] 打开本地命令，随后 c 选择返回后的控制状态、q 退出或视图键连续调整画面，Esc/Ctrl+C 手动返回；控制模式中 Ctrl+T/Ctrl+C 传给远端，本地模式中再按 Ctrl+] 手动返回，并在返回控制模式时传递远端 Ctrl+] |
| display | index:0..63 | 选择已广告的零起始显示器，提升 generation |
| ack | frame_id、generation：非负整数 | 确认宿主消费帧；匹配当前等待帧才开放下一帧 |
| disconnect | 无 | 释放输入、结束当前媒体会话 |
| exit | 无 | 断开并结束进程 |

凭据不能放在 connect JSON 中。专用描述符可以由 Node `spawn` 的第四条 pipe 提供。成功读取后会话不会保留用户密码供上层访问；MiniRTC 内部为现有 Join/重连流程持有必要凭据直到销毁 Peer。下一次 connect 必须有可读的凭据来源；Pi 关闭视图后直接结束进程。

帧事件：

```json
{"event":"frame","frame_id":42,"decoded_frame_id":38,"generation":4,"source_width":1280,"source_height":720,"zoom":2,"native":false,"source_rect":[320,180,640,360],"detail":"quadrant","columns":80,"rows":23,"lines":["..."],"queue_ms":3.2}
```

实际 lines 数为 rows，仅包含本地生成的 SGR 颜色序列、空格和 U+2580～U+259F 块字符，不含远端名称或任意终端指令。`frame_id` 是进程递增的输出序号，静止画面重绘仍产生新序号；`decoded_frame_id` 标识源解码帧，同源重绘可以相同。`source_width/height` 为实际解码尺寸，source_rect 为实际源裁剪矩形 [x,y,width,height]。native=true 表示原始细节模式，zoom 为当前模式下的倍率；generation 在关闭、重新连接和显示器切换时变化。viewport 变化可能与已写出的旧尺寸帧交错，宿主应丢弃不匹配尺寸/旧 generation 的帧并释放对应 ACK。宿主返回的 ACK 表示消费/布局，不表示屏幕物理显示完成。

状态事件每秒最多一次：`{"event":"state","status":{...},"output_bytes":123,"submitted_frames":3,"elapsed_seconds":1.5}`。status 含 registering/connecting/connected/failed/closed/idle、client_id（当前或刚结束会话的 `cli-xxxxxxxxx`，注册前为空）、error、control、generation、显示器、文本/预览能力、预览确认状态、请求的 preview_width/height/quality/bitrate_bps、帧计数与网络统计。接收/提交 FPS 由相应计数的增量除以时间增量计算。

状态和成功响应中的 `status.command_mode` 表示处于持续本地命令模式，此时 `control=false`，远端输入暂停；`status.resume_control` 表示手动返回后是否启用控制。执行命令、未知按键或粘贴不会退出此模式，失焦或输入溢出只会将返回模式设为只读。`Esc` / `Ctrl+C` 或再次 `Ctrl+]` 手动返回；`status.exit_requested` 表示用户按键请求退出，宿主可据此关闭组件。宿主应从 `input` 响应立即更新命令提示，不必等待下一次状态事件；不要自行截获 `Ctrl+]` 退出。显式 `control` 请求只更新所选控制状态，在本地模式中仍暂停远端输入。新建连接或断开会重置本地模式。具体快捷键见 [终端客户端](terminal-client.md)。

边界：输入每条及当前解析缓冲上限64KiB、嵌套32层；响应队列64条；输出一条在途消息上限2MiB；1MiB/s；最多一条待 ACK 帧；一个最新待渲染 NV12 加一个最近画面的重绘缓存。协议错误明确响应；超限缓冲后结束。EOF 清理会话，完整的已收请求按序处理，不完整末尾报错；最多两秒排空输出后退出。宿主应持续消费响应，不能无限发送而不读取。失去连接后的释放只能尽力发送。

没有 `screenshot`、`audio`、文件、剪贴板、任意远端动作或 shell 命令方法；未知方法报错。模型接入者必须另行取得截图/自动操作授权，不能把持续帧事件附加到聊天记录。
