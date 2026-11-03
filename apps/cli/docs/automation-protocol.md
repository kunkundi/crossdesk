# Native automation protocol

`crossdesk-cli agent --stdio --password-fd 3 --video-ws` is the private native adapter used by CrossDesk MCP. It is separate from the existing terminal-oriented `bridge` protocol. This is **not MCP on stdin**: the TypeScript service owns the public MCP transport and tool schemas.

Optional native arguments are `--server HOST`, `--port PORT`, `--quality PRESET` and `--log-dir DIRECTORY`. The caller creates the log directory. Diagnostic stdout is redirected to stderr; only protocol responses use the preserved output descriptor. Passwords travel over descriptor 3, terminated by newline or EOF, and never appear in JSON requests. `--video-ws` starts an authenticated, native loopback WebSocket listener on an ephemeral port. There is no video file descriptor. Agent mode selects the high-frame-rate preview tier at up to 1920 × 1080 by default; negotiated remote capabilities and network conditions determine the delivered rate. Each process accepts one connection attempt; retry authentication with a new process and credential pipe.

## Envelope

One UTF-8 JSON object per line, maximum request 64 KiB and nesting depth 32. Use nonnegative integer request IDs. Responses may complete out of order; correlate by ID.

```json
{"id":1,"method":"hello","protocol":"crossdesk-automation/1"}
{"id":1,"ok":true,"result":{"protocol":"crossdesk-automation/1","image_formats":["image/png"],"action_completion":"submitted","human_pause":true,"max_request_bytes":65536,"max_response_bytes":4194304,"operation_cache_entries":128,"operation_cache_ms":300000}}
```

Unknown fields and malformed arguments are rejected. Error responses contain a stable code, retryability and whether input may already have been submitted:

```json
{"id":2,"ok":false,"error":{"code":"CONTROL_DENIED","retryable":false,"side_effect_possible":false}}
```

The output queue is bounded to 64 messages and 8 MiB; a blocked output pipe terminates after 10 seconds without progress. EOF, signals and `exit` disconnect and release held input. A caller that needs responses must keep stdin open until they arrive.

## Methods

| Method | Parameters in addition to `id`/`method` |
| --- | --- |
| `hello` | `protocol: "crossdesk-automation/1"` |
| `connect` | `device` (9 digits), `mode: "observe" | "control"`, `display` (0–63), optional `decode_path: "browser" / "minirtc"`, `decoder: "auto" / "software"` |
| `status` | none |
| `screenshot` | optional `region: {x,y,width,height}`, `max_edge` (64–2048), `after_frame_id`, `generation`, `timeout_ms` (0–5000) |
| `video_open` | `ticket`, `view`, `view_generation`, `session`, `origin`, `apps`; requires `--video-ws` and a connected session |
| `video_revoke` | optional `view`; omit to revoke all video grants and sockets |
| `preview` | optional `max_edge` (64–960), `after_frame_id`, `generation`, `timeout_ms` (0–5000); no region |
| `action` | `operation_id`, `observation_id`, `action` |
| `control` | `enabled` boolean; cannot override observe-only permission or human pause |
| `viewer_control` | `operation: "pause" | "resume" | "disconnect"`; restricted to trusted UI by the MCP adapter |
| `terminal_frame` | `columns` (1–160), `rows` (1–60), `colors` (8/24/256), `cell_aspect` (0.2–2); private terminal viewer only |
| `display` | `index` (0–63) |
| `cancel` | `request_id`; cancels queued/in-flight captures or input sequences |
| `disconnect`, `exit` | none; `exit` also ends the process |

`connect` returns immediately after initiating transport. Poll `status` until `state == "ready"`; this requires a frame from the current display generation. Status includes `transport_state`, `geometry_epoch`, `frame_age_ms`, `source_size`, `control_allowed`, `human_paused`, host platform, displays and transport diagnostics.

## Live video

`doctor --json` advertises `automation_video_transport: "loopback-websocket/1"`, `automation_video_protocols: ["h264-annexb/1", "nv12/1"]`, and `automation_video_decoders: ["auto", "software"]`. `hello` repeats these without the `automation_` prefix. The MCP adapter requires the transport capability, so an old pipe-only CLI is rejected. Node spawns only stdin, stdout, stderr, and the credential pipe; live video never enters Node.

The terminal viewer additionally requires `automation_terminal_protocol: "ansi/1"` in `doctor` and `terminal_protocol: "ansi/1"` in `hello`. `terminal_frame` asynchronously converts the current decoded frame using the existing ANSI quadrant renderer and returns `columns`, `rows`, `lines`, `generation`, `geometry_epoch`, and a nullable `cursor` with zero-based terminal `column`/`row`. Only one render may be in flight; obsolete geometry or frames older than two seconds are rejected. These rendered text rows travel through the private authenticated terminal IPC, never through public MCP tools or model content. The viewer requests at most 8 FPS and waits for terminal writes before requesting another frame. It shares the automation connection; no second media session is opened.

Status includes `cursor` with normalized full-frame `x`/`y`, `visible`, `generation`, `geometry_epoch`, `width`, and `height`. The native layer updates it after each successfully sent pointer step, including drag steps, using the same ROI-to-source mapping as remote input. It hides on pause/control disable and clears on geometry changes. It is an Agent position indicator, not remote cursor telemetry or execution confirmation; observation PNGs and source video remain unchanged.

Authenticated video sockets also receive small JSON text messages with `type: "cursor"` and these fields, independently of binary video frames. Pointer updates are coalesced to at most 30 Hz, retained for newly authorized viewers, and deferred under socket backpressure; they require no video ACK. Browser/Apps render a separate transparent overlay with the same aspect-ratio fitting as the video, including when no new video frame arrives.

`connect.decode_path` defaults to `browser` (compressed H.264), or selects `minirtc` (decoded NV12). `connect.decoder` defaults to `auto` (hardware preferred, software when unavailable); `software` selects the native software decoder. Both paths maintain native screenshot frames with the selected decoder. These choices are fixed for the connection. Status exposes `video` and the reported `native_video_decoder` backend name. PNG encoding runs only when requested. The legacy `preview` image method remains available to private protocol callers but is not used by MCP panels.

### Authorization and transport

The trusted adapter generates 24 random bytes as a 32-character base64url `ticket` and sends `video_open` over private JSON-RPC. `view` and `session` are also 32-character base64url IDs; `view_generation` is positive. `origin` is the Node panel's exact `http://127.0.0.1:PORT` origin. `apps` permits an opaque (`null`) or HTTPS MCP Apps origin, but still requires the same one-time ticket. No password is included.

The response contains `{url, transport, protocol, decoder, expires_in_ms:15000}`. Node supplies the ticket to the authorized UI. The browser opens the returned `ws://127.0.0.1:PORT/api/video` directly and sends `{"ticket":"..."}` as its first text message within 3 seconds. Tickets expire after 15 seconds, are consumed once, and are bound to a view/session plus the native display generation. The native service checks the exact Host and path, RFC 6455 version, and authorized Origin. It does not serve HTTP content or accept remote input commands. Logs are disabled for this listener; tickets never go in URLs or model-visible results.

The listener uses its own I/O thread, at most 16 WebSocket candidates and 8 authorized viewers. Client messages are limited to 1024 bytes. Revocation, disconnect, native generation changes, or shutdown cancel sockets and invalidate tickets, including partially sent frames. Pause only disables Agent control; viewing continues. The adapter explicitly revokes a removed or replaced view, and terminates the native session if revocation cannot be confirmed. Native processes own ephemeral ports, so panel CSP allows `ws://127.0.0.1:*`, restricted to the literal loopback address; the control HTTP origin remains exact.

### Binary messages and flow control

Each binary WebSocket message contains one complete frame, **without the old pipe length prefix**. Integers are big-endian. For `h264-annexb/1`:

| Offset | Type | Value |
| --- | --- | --- |
| 0 | u32 | `0x43445631` (`CDV1`) |
| 4 | u32 | Display/connection generation |
| 8 | u32 | Receive sequence; gaps require a new keyframe |
| 12 | u64 | Local monotonic receive timestamp in microseconds |
| 20 | u32 | Flags: 1 = IDR keyframe, 0 = delta |
| 24 | bytes | Annex B H.264; keyframes include SPS and PPS |

Access units are capped at 4 MiB. Both the receive tap and the I/O handoff are bounded to 60 frames / 8 MiB. Overflow discards dependent frames until an IDR. After receiving each message the browser sends `{"type":"ack","generation":N,"sequence":N}` directly to the native socket, in order. This acknowledges receipt, not decode, presentation, or remote execution. Per-viewer unacknowledged output is capped at 8 frames; at 1 MiB outstanding, further frames are skipped until space is available (a single keyframe may exceed that threshold). Dropping a dependent picture requires a new keyframe; `{"type":"keyframe"}` requests recovery, rate-limited to once per second.

For `nv12/1`, magic is `0x43444e31` (`CDN1`). Offsets 4, 8, 12 and 20 remain generation, decoded sequence, local monotonic decoded-frame queue time in microseconds, and flags (always 1). Offsets 24 and 28 are u32 width and height; offset 32 starts tightly packed Y followed by interleaved UV. Dimensions must be even, at most 8192 per axis / 16777216 pixels. Message size must equal `32 + width * height * 3 / 2` (at most 25165856 bytes). Color is limited-range BT.601, matching the existing PNG conversion. Sequence gaps are allowed.

NV12 retains one latest picture in the I/O handoff and at most one unacknowledged picture per viewer. New pictures are skipped for a slow viewer without blocking others. Each prepared WebSocket message is immutable and shared between recipients, avoiding per-viewer payload copies. There is still a native copy into the WebSocket payload and raw pixel transfer to the browser; this is not GPU zero-copy. A viewer whose oldest outstanding frame remains unacknowledged for 10 seconds is disconnected. Hidden/unloaded pages close their native socket. The browser replaces pending raw pictures and does not decode H.264 in this mode.

`status.video_output` includes `transport`, `viewers`, I/O handoff `queued` / `queued_bytes`, aggregate `unacknowledged_bytes`, and `dropped` (handoff replacements plus per-viewer congestion skips). These diagnostics contain no video payload. The original receive-tap SPS/PPS and keyframe state remain available for H.264.

The MiniRTC `Params` structure appends `on_receive_encoded_video`, changing its ABI size. Rebuild consumers and MiniRTC together; zero-initialize `Params`. The callback borrows frame bytes on the receive/decode queue and must not retain the pointer. Returning nonzero requests a remote keyframe. No remote wire-protocol change is required.

## Images and observations

Images return `{mime_type:"image/png",data:"<base64>",metadata:{...}}`. Metadata includes `generation`, `geometry_epoch`, `frame_id`, `display_index`, `source_size`, `source_rect`, `image_size` and `frame_age_ms`; only screenshots create `observation_id`. Preview images cannot authorize model input.

NV12 frames must be tightly packed, even dimensions, at most 8192 per axis and 16 million pixels. Conversion/PNG encoding runs on one worker with no `ControllerSession` calls. The owner loop continues processing cancellation and input release. Screenshots have priority over queued previews. An encoding from an obsolete generation or geometry epoch is discarded. Frames older than two seconds are not used for a new image. PNG limits are 2 MiB for observations and 256 KiB for previews; the encoder reduces dimensions when necessary.

`after_frame_id` requires `generation`. A screenshot waits up to `timeout_ms` for a newer frame; a preview can return `{unchanged:true, frame_age_ms, stale, generation, frame_id}`. Neither path treats an image as an input acknowledgement.

`timeout_ms` bounds frame/worker availability, not the subsequent PNG encoding. With `timeout_ms: 0`, a fresh eligible frame is encoded immediately if the worker and queue are idle; otherwise return `FRAME_TIMEOUT` or `BUSY`. Captures of the same priority retain arrival order. Encoding still rejects obsolete geometry and frames that become older than two seconds. Terminal rendering also honors cancellation without sending a second result when the worker finishes.

## Actions

All coordinates refer to the immutable returned observation image. The native layer maps them through its source rectangle to normalized remote coordinates, without terminal layout padding. Coordinates must satisfy `0 <= x < image_width` and `0 <= y < image_height`. Observations expire after 30 seconds, retain at most 32 entries, and are invalidated by geometry/display generation changes.

```json
{"id":10,"method":"action","operation_id":"click-1","observation_id":"obs_2_1","action":{"type":"click","x":120,"y":80,"button":"left"}}
```

Supported payloads:

- `move`: `x`, `y`.
- `click`, `double_click`: `x`, `y`, optional `button` (`left`, `right`, `middle`).
- `scroll`: `x`, `y`, `direction` (`up`, `down`, `left`, `right`), optional `ticks` (1–10).
- `drag`: `path` of 2–128 `{x,y}` points, optional `duration_ms` (100–2000), optional `button` (`left`, `right`, `middle`; default `left`).
- `key_press`: `key`; `hotkey`: 2–6 unique `keys`.
- `type_text`: `text`, at most 16 KiB UTF-8; requires negotiated remote text-input capability.

All pointer actions and `key_press` accept optional `modifiers`, an array of at most four unique names from `CTRL`, `ALT`, `SHIFT`, `META`. They are pressed before the action and released afterwards in reverse order; a `key_press` cannot also list its own key as a modifier. `hotkey` already describes the complete chord and does not accept `modifiers`. Punctuation VK names are `SEMICOLON`, `EQUAL`, `COMMA`, `MINUS`, `PERIOD`, `SLASH`, `BACKQUOTE`, `BRACKET_LEFT`, `BACKSLASH`, `BRACKET_RIGHT`, `QUOTE`; these name keys, not layout-independent text.

Only one input sequence runs at once. Timed steps are advanced by the owner loop; cancellation, disconnect, display changes and pause release held buttons/keys. A failed send is conservatively considered potentially submitted.

New actions require a current frame no older than two seconds as well as a valid observation. Cached operation lookups remain available with a stalled feed or paused control. Failed key/button releases remain tracked for subsequent cleanup, including after control is disabled; a successful local send still does not prove remote execution. Invalid display indices leave existing observations and input sequences intact.

Results use `status: submitted | in_progress | failed_before_submit | outcome_unknown`, with `operation_id`, `retryable`, `side_effect_possible` and, for completed actions, `generation`/`after_frame_id`. The same operation ID and arguments retrieve the cached result; different arguments conflict. Retention is bounded to 128 entries, with entries older than five minutes eligible for removal on a new action. Never retry an uncertain effect with a new ID. `submitted` does not claim remote execution.

The native process trusts its parent. The parent must authenticate panel-only `viewer_control` calls and protect descriptor 3; this private pipe does not isolate arbitrary programs running as the same OS user.
