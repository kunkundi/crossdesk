import type { App } from "@modelcontextprotocol/ext-apps";
import "./viewer.css";
import { VideoPlayer } from "./video-player.js";

declare const CROSSDESK_APPS: boolean;
declare const CROSSDESK_LOGO: string;
type Data = Record<string, any>;
type Invoke = (name: string, args?: Data) => Promise<Data>;
const root = document.querySelector<HTMLDivElement>("#app")!;
root.innerHTML = `
<header class="app-header">
  <div class="brand"><img class="brand-logo" src="${CROSSDESK_LOGO}" alt="" width="28" height="28"><h1>CrossDesk MCP</h1></div>
  <div class="header-right"><button id="details-toggle" aria-expanded="false" aria-controls="sidebar">连接信息</button><span class="connection" id="connection" role="status"><span id="connection-label">等待连接</span></span></div>
</header>
<div id="error" role="alert" hidden></div>
<main>
  <aside id="sidebar" aria-labelledby="sidebar-title">
    <h2 id="sidebar-title">连接设备</h2>
    <div id="connect-form" role="form" aria-label="连接远程电脑">
      <fieldset id="connection-fields">
        <label for="device">设备</label><select id="device"><option value="">连接新设备</option></select>
        <div id="new-device">
          <label for="device-id">设备 ID</label><input id="device-id" inputmode="numeric" autocomplete="off" placeholder="输入 9 位设备 ID" maxlength="11">
        </div>
        <label for="password">访问密码</label><div class="password-field"><input id="password" type="password" autocomplete="off" maxlength="256" placeholder="输入远端访问密码"><button type="button" id="password-toggle" aria-label="显示密码" aria-pressed="false">显示</button></div>
        <label for="mode">授权范围</label><select id="mode" aria-describedby="mode-description"><option value="observe">仅查看画面</option><option value="control">允许 Agent 操作</option></select>
        <p class="field-hint" id="mode-description">Agent 可以查看屏幕，无法操作键鼠。</p>
        <details class="preferences"><summary>保存与授权</summary>
          <div id="new-device-label"><label for="label">设备备注 <span class="optional">可选</span></label><input id="label" placeholder="例如：工作电脑" maxlength="80"></div>
          <div class="checks">
            <label><input id="save" type="checkbox"><span>保存设备</span></label>
            <label><input id="remember" type="checkbox" disabled><span>记住密码<small>保存在本机系统凭据库</small></span></label>
            <label><input id="authorize" type="checkbox" disabled><span>允许下次自动连接<small>沿用本次授权范围</small></span></label>
          </div>
        </details>
        <details id="advanced"><summary>高级设置</summary>
          <label for="server">服务器</label><input id="server" value="api.crossdesk.cn" autocomplete="off">
          <div class="row"><div><label for="port">端口</label><input id="port" type="number" min="1" max="65535" value="9099"></div><div><label for="display">显示器索引</label><input id="display" type="number" min="0" max="63" value="0"></div></div>
        </details>
      </fieldset>
      <button id="connect" type="button" class="primary" disabled><span id="connect-label">连接</span></button>
      <p class="privacy">密码不会发送到 Agent 对话中。</p>
    </div>
    <div id="session-card" hidden>
      <div class="device-card"><strong id="session-device">远程电脑</strong><span id="session-platform">已连接</span></div>
      <dl class="session-details"><div id="session-device-id-row" hidden><dt>设备 ID</dt><dd id="session-device-id"></dd></div><div><dt>访问权限</dt><dd id="session-access"></dd></div><div><dt>显示器</dt><dd id="session-display"></dd></div><div><dt>Agent 状态</dt><dd id="session-agent"></dd></div></dl>
      <p class="session-note">关闭页面不会断开连接。</p>
    </div>
  </aside>
  <section class="workspace" aria-label="远程画面与会话控制">
    <div class="viewer-card">
      <div class="viewer-toolbar">
        <div class="viewer-title"><h2 id="device-name">远程画面</h2><span id="video-state">待连接</span></div>
        <div class="viewer-controls"><button id="pause" disabled><span id="pause-label">暂停控制</span></button><button id="disconnect" class="danger" disabled>断开连接</button><button id="focus-view" aria-label="放大画面" aria-pressed="false" disabled>放大画面</button></div>
      </div>
      <div class="screen" id="screen">
        <canvas id="frame" role="img" aria-label="远程桌面实时画面" hidden></canvas>
        <div class="control-halo" aria-hidden="true"></div>
        <div id="empty"><h3 id="empty-title">未连接设备</h3><p id="empty-description">选择设备并输入访问密码。</p></div>
        <div class="stale" id="stale" role="status" hidden>画面暂未更新</div>
      </div>
      <div class="telemetry"><div class="stream-metrics"><span id="resolution">—</span><span id="fps">—</span></div><span id="session-info">等待设备连接</span><span id="permission">等待授权</span></div>
    </div>
    <div class="video-options"><button id="video-retry" hidden>重新加载画面</button><button id="video-browser" hidden>在浏览器中观看</button></div>
    <p id="panel-status" class="panel-status" role="status" hidden></p>
  </section>
</main>`;
const element = <T extends HTMLElement = HTMLElement>(id: string) =>
  document.getElementById(id) as T;
const input = (id: string) => element<HTMLInputElement>(id);
const select = (id: string) => element<HTMLSelectElement>(id);
let invoke: Invoke;
let app: App | undefined;
let context: Data | undefined;
let status: Data = {};
let savedDevices: Data[] = [];
let controlBusy = false;
let disconnectedByUser = false;
let frameSize = "", frameRate = "";
let player: VideoPlayer | undefined;
let videoStarting = false, videoFailed = false, videoEpoch = 0;
let videoSession: string | undefined;
let videoGeneration: number | undefined;
let imageAt = 0;
let busy = false,
  polling = false,
  started = false;
const messages: Record<string, string> = {
  VIDEO_UNSUPPORTED: "当前浏览器不支持此视频格式，请使用支持 WebCodecs 的浏览器。",
  VIDEO_UNAVAILABLE: "视频通道暂不可用。可重新加载画面；内嵌面板受限时请在浏览器中观看。",
  VIDEO_INVALID: "视频格式发生变化，请重新加载画面。",
  VIDEO_LIMIT: "观看面板数量已达上限，请关闭其他面板。",
  AUTH_FAILED: "访问密码错误，请重新输入。",
  AUTH_REQUIRED: "请输入访问密码，或在本机 setup 中配置凭据。",
  CONNECTION_FAILED: "连接失败，请检查设备是否在线及服务器设置。",
  CONNECTION_TIMEOUT: "等待远端画面超时，请检查远端状态后重试。",
  CONNECTION_LOST: "远程连接已中断。请检查远端状态并重新连接。",
  SESSION_LIMIT: "已有活动连接，请先断开当前电脑。",
  DEVICE_BUSY: "另一实例正在使用此设备。",
  CREDENTIAL_STORE_UNAVAILABLE: "系统凭据库不可用，密码未保存。",
  PAIRING_REQUIRED:
    "请在终端运行 Agent 工具结果中的 setup --open-panel 配对命令，再打开面板。",
  PANEL_UNAUTHORIZED: "面板授权已失效，请重新打开。",
  VIEW_UNAUTHORIZED: "此面板已失效，请使用最新打开的面板。",
  CONTROL_DENIED: "当前会话没有控制权限，或已被人工暂停。",
  NATIVE_PACKAGE_MISSING:
    "缺少对应平台的 CLI 运行组件，请检查包安装是否启用了 optional dependencies。",
  INVALID_ARGUMENT: "请检查设备 ID、服务器和输入内容。",
  DEVICE_ALIAS_CONFLICT: "这个备注已属于另一台设备，请换一个备注。",
};
function text(id: string, value: string) {
  if (element(id).textContent !== value) element(id).textContent = value;
}
function platformName(platform?: string) {
  return ({ macos: "macOS", windows: "Windows", linux: "Linux", android: "Android", ios: "iOS" } as Record<string, string>)[platform ?? ""] ?? "远程电脑";
}
function resetMetrics() {
  imageAt = 0;
  frameSize = frameRate = "";
  text("resolution", "—");
  text("fps", "—");
  element<HTMLButtonElement>("focus-view").disabled = true;
}
function setFocus(focused: boolean) {
  root.classList.toggle("focus-mode", focused);
  const button = element<HTMLButtonElement>("focus-view");
  const label = focused ? "还原布局" : "放大画面";
  button.textContent = label;
  if (focused) setDetails(false);
  button.setAttribute("aria-pressed", String(focused));
  button.setAttribute("aria-label", label);
  button.title = label;
}
function setDetails(open: boolean) {
  root.classList.toggle("details-open", open);
  element("details-toggle").setAttribute("aria-expanded", String(open));
  text("details-toggle", open ? "收起信息" : "连接信息");
}
element("details-toggle").onclick = () => setDetails(!root.classList.contains("details-open"));
element("focus-view").onclick = () => setFocus(!root.classList.contains("focus-mode"));
window.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && root.classList.contains("focus-mode")) setFocus(false);
});
function hidePassword() {
  input("password").type = "password";
  text("password-toggle", "显示");
  element("password-toggle").setAttribute("aria-label", "显示密码");
  element("password-toggle").setAttribute("aria-pressed", "false");
}
element("password-toggle").onclick = () => {
  if (input("password").type === "text") return hidePassword();
  input("password").type = "text";
  text("password-toggle", "隐藏");
  element("password-toggle").setAttribute("aria-label", "隐藏密码");
  element("password-toggle").setAttribute("aria-pressed", "true");
};
select("mode").onchange = () => {
  text("mode-description", select("mode").value === "control"
    ? "Agent 可以操作键鼠，你可以随时暂停。"
    : "Agent 可以查看屏幕，无法操作键鼠。");
};
function showError(error: unknown) {
  const code = error instanceof Error ? error.message : "INTERNAL_ERROR";
  element("error").textContent =
    messages[code] ??
    `操作未完成（${/^[A-Z_]+$/.test(code) ? code : "CONNECTION_ERROR"}）`;
  element("error").hidden = false;
}
function check(result: Data): Data {
  if (result.error) throw new Error(result.error.code ?? "INTERNAL_ERROR");
  return result;
}
async function devices() {
  const result = await invoke("devices");
  if (!started && result.defaults) {
    input("server").value = result.defaults.server;
    input("port").value = String(result.defaults.port);
  }
  const chosen = select("device").value || context?.device_alias || "";
  select("device").replaceChildren(new Option("连接新设备", ""));
  savedDevices = result.devices ?? [];
  for (const d of savedDevices)
    select("device").add(
      new Option(`${d.device_alias} · ${d.device_id}`, d.device_alias),
    );
  select("device").value = [...select("device").options].some(
    (o) => o.value === chosen,
  )
    ? chosen
    : "";
  deviceChanged();
}
function deviceChanged() {
  const saved = !!select("device").value;
  element("new-device").hidden = saved;
  element("new-device-label").hidden = saved;
  input("server").disabled = input("port").disabled = saved;
  input("password").placeholder = saved
    ? "留空使用已保存的密码"
    : "输入远端访问密码";
}
select("device").onchange = deviceChanged;
input("save").onchange = () => {
  for (const name of ["remember", "authorize"]) {
    input(name).disabled = !input("save").checked;
    if (!input("save").checked) input(name).checked = false;
  }
};
function renderStatus(s: Data) {
  const connected = s.state === "ready" || s.state === "waiting_frame";
  const lost = ["closed", "failed"].includes(s.state);
  if (lost && !disconnectedByUser && ["ready", "waiting_frame"].includes(status.state)) showError(new Error("CONNECTION_LOST"));
  const changedSession = s.session_id !== status.session_id;
  status = s;
  const connecting = busy || s.state === "waiting_frame";
  root.dataset.connected = String(connected);
  root.dataset.connecting = String(connecting);
  root.dataset.controlling = String(connected && s.control === true && s.control_allowed === true && !s.human_paused);
  element("connection").dataset.state = connected ? "online" : connecting ? "connecting" : lost && !disconnectedByUser ? "offline" : "idle";
  text("connection-label", connected ? "已连接" : connecting ? "正在连接" : lost ? "未连接" : "等待连接");
  element("connect-form").hidden = connected;
  element("session-card").hidden = !connected;
  text("sidebar-title", connected ? "连接信息" : "连接设备");
  text("device-name", connected ? s.device_alias || "远程电脑" : "远程画面");
  if (!connected || changedSession) setDetails(false);
  text("permission", connected ? s.human_paused ? "控制已暂停" : s.control_allowed ? "允许 Agent 操作" : "仅查看画面" : "等待授权");
  element("permission").dataset.state = !connected ? "idle" : s.human_paused ? "paused" : "active";
  const platform = platformName(s.platform);
  text("session-info", connected ? `${platform} · 显示器 ${(s.display ?? 0) + 1}` : "等待设备连接");
  text("session-device", s.device_alias || "远程电脑");
  text("session-platform", platform);
  const device = savedDevices.find((d) => d.device_alias === s.device_alias);
  element("session-device-id-row").hidden = !device;
  text("session-device-id", device ? String(device.device_id).replace(/(\d{3})(?=\d)/g, "$1 ") : "");
  text("session-access", s.control_allowed ? "查看与操作" : "仅查看");
  text("session-display", `显示器 ${(s.display ?? 0) + 1}`);
  text("session-agent", s.human_paused ? "已暂停" : s.control_allowed ? "允许操作" : "仅可观察");
  element("session-agent").dataset.paused = String(!!s.human_paused);
  element<HTMLButtonElement>("pause").disabled = !connected || !s.control_allowed || controlBusy;
  const pauseLabel = s.human_paused ? "恢复控制" : "暂停控制";
  text("pause-label", pauseLabel);
  element<HTMLButtonElement>("disconnect").disabled = !s.session_id || controlBusy || busy;
  element<HTMLButtonElement>("connect").disabled = busy || connected || !invoke;
  element<HTMLFieldSetElement>("connection-fields").disabled = busy;
  element("connect").setAttribute("aria-busy", String(busy));
  text("connect-label", busy ? "正在连接…" : "连接");
  if (!connected) {
    element("frame").hidden = true;
    element("empty").hidden = false;
    element("stale").hidden = true;
    resetMetrics();
    if (root.classList.contains("focus-mode")) setFocus(false);
  } else if (changedSession) {
    resetMetrics();
    element("frame").hidden = true;
    element("empty").hidden = false;
  }
  if (!imageAt && !videoFailed) {
    text("empty-title", connecting || connected ? "正在建立远程连接" : lost ? "会话已结束" : "未连接设备");
    text("empty-description", connecting || connected ? "正在等待远端画面，请稍候。" : lost ? "请重新连接设备。" : "选择设备并输入访问密码。");
    text("video-state", connecting || connected ? "等待画面" : "待连接");
    element("video-state").dataset.state = "idle";
  }
}
async function connect() {
  if (!invoke || busy || ["ready", "waiting_frame"].includes(status.state)) return;
  element("error").hidden = true;
  disconnectedByUser = false;
  busy = true;
  renderStatus(status);
  const saved = select("device").value;
  let args: Data = {
    mode: select("mode").value,
    display_index: Number(input("display").value),
    save_device: input("save").checked,
    remember_password: input("remember").checked,
    persistent_authorization: input("authorize").checked,
    ...(saved
      ? { device_alias: saved }
      : {
          device_id: input("device-id").value.replace(/\s/g, ""),
          server: input("server").value,
          port: Number(input("port").value),
          ...(input("label").value ? { label: input("label").value } : {}),
        }),
  };
  if (!saved || input("password").value)
    args.password = input("password").value;
  input("password").value = "";
  hidePassword();
  try {
    const result = await invoke("connect", args);
    args = {};
    renderStatus(result);
    if (result.persistence_error)
      showError(new Error(result.persistence_error));
    await devices();
  } catch (error) {
    args = {};
    showError(error);
  } finally {
    busy = false;
    renderStatus(status);
  }
}
element("connect").onclick = () => {
  void connect();
};
element("connect-form").addEventListener("keydown", (event) => {
  if (event.key === "Enter" && event.target instanceof HTMLInputElement) {
    event.preventDefault();
    void connect();
  }
});
element("pause").onclick = async () => {
  if (controlBusy) return;
  controlBusy = true;
  renderStatus(status);
  try {
    await invoke("control", {
      operation: status.human_paused ? "resume" : "pause",
    });
    renderStatus(await invoke("status"));
  } catch (e) {
    showError(e);
  } finally {
    controlBusy = false;
    renderStatus(status);
  }
};
element("disconnect").onclick = async () => {
  if (controlBusy) return;
  controlBusy = true;
  disconnectedByUser = true;
  renderStatus(status);
  try {
    await closePreview();
    await invoke("control", { operation: "disconnect" });
    element("error").hidden = true;
    resetMetrics();
    renderStatus(await invoke("status"));
  } catch (e) {
    disconnectedByUser = false;
    showError(e);
  } finally {
    controlBusy = false;
    renderStatus(status);
  }
};
async function closePreview() {
  ++videoEpoch;
  player?.stop();
  player = undefined;
  videoSession = undefined;
  videoGeneration = undefined;
  videoStarting = videoFailed = false;
  element("video-retry").hidden = true;
  element("video-browser").hidden = true;
}
async function startPreview() {
  if (player || videoStarting || videoFailed || document.hidden) return;
  const epoch = ++videoEpoch;
  videoSession = status.session_id;
  videoGeneration = status.generation;
  videoStarting = true;
  const failed = (error: Error) => {
    if (epoch !== videoEpoch) return;
    videoFailed = true;
    showError(error);
    element("video-retry").hidden = false;
    element("video-browser").hidden = !CROSSDESK_APPS;
    element("stale").hidden = !imageAt;
    text("video-state", "画面不可用");
    element("video-state").dataset.state = "stale";
    if (!imageAt) {
      text("empty-title", "暂时无法显示画面");
      text("empty-description", "会话仍然保留。请尝试重新加载画面。");
    }
  };
  try {
    const grant = await invoke("video_open");
    if (epoch !== videoEpoch || document.hidden) return;
    player = new VideoPlayer(element<HTMLCanvasElement>("frame"), (info) => {
      if (epoch !== videoEpoch) return;
      imageAt = Date.now();
      element("frame").hidden = false;
      element("empty").hidden = true;
      element("stale").hidden = true;
      const size = `${info.width} × ${info.height}`;
      const rate = info.fps > 0 ? `${info.fps.toFixed(1)} FPS` : "测量中";
      if (size !== frameSize) { frameSize = size; text("resolution", size); }
      if (rate !== frameRate) { frameRate = rate; text("fps", rate); }
      element<HTMLButtonElement>("focus-view").disabled = false;
      text("video-state", "实时");
      element("video-state").dataset.state = "live";
    }, failed);
    player.start(grant.url, grant.ticket, grant.protocol, grant.decoder);
  } catch (error) {
    failed(error instanceof Error ? error : new Error("VIDEO_UNAVAILABLE"));
  } finally {
    if (epoch === videoEpoch) videoStarting = false;
  }
}
element("video-retry").onclick = async () => {
  await closePreview();
  element("error").hidden = true;
  await startPreview();
};
element("video-browser").onclick = async () => {
  try {
    const result = await app!.callServerTool({name: "crossdesk_open_browser", arguments: {view_id: context!.view_id}});
    if (result.isError) throw new Error("VIDEO_UNAVAILABLE");
    started = false;
    await closePreview();
    element("error").hidden = true;
    for (const name of ["connect", "pause", "disconnect"])
      element<HTMLButtonElement>(name).disabled = true;
    element("panel-status").hidden = false;
    element("panel-status").textContent = "浏览器观看面板已打开。";
  } catch (error) { showError(error); }
};
async function poll() {
  if (polling || !invoke || !started || document.hidden || busy || controlBusy) return;
  polling = true;
  try {
    renderStatus(await invoke("status"));
    if (videoSession && (videoSession !== status.session_id ||
        videoGeneration !== status.generation)) {
      if (videoFailed) element("error").hidden = true;
      await closePreview();
    }
    if (status.state !== "ready" || document.hidden) {
      await closePreview();
      return;
    }
    await startPreview();
    const stale = !!imageAt && Date.now() - imageAt > 2000;
    element("stale").hidden = !stale;
    if (stale) {
      text("video-state", "等待画面");
      element("video-state").dataset.state = "stale";
      text("fps", "—");
      frameRate = "";
    }
  } catch (error) { showError(error); }
  finally { polling = false; }
}
document.addEventListener("visibilitychange", () => {
  if (document.hidden) void closePreview();
  else void poll();
});
window.addEventListener("pagehide", () => {
  void closePreview();
});
setInterval(() => {
  void poll();
}, 1000);
async function ready(next: Data) {
  if (
    started &&
    context?.view_id === next.view_id &&
    context?.view_generation === next.view_generation
  )
    return;
  if (started) await closePreview();
  context = next;
  element("panel-status").hidden = true;
  started = false;
  await invoke("ready");
  await devices();
  started = true;
  element<HTMLButtonElement>("connect").disabled = false;
  await poll();
}
async function start() {
  if (CROSSDESK_APPS) {
    const { App } = await import("@modelcontextprotocol/ext-apps");
    app = new App({ name: "CrossDesk MCP", version: "0.1.0" }, {});
    const names: Record<string, string> = {
      ready: "crossdesk_ui_ready",
      devices: "crossdesk_ui_list_devices",
      status: "crossdesk_ui_get_session",
      connect: "crossdesk_ui_connect",
      video_open: "crossdesk_video_open",
      control: "crossdesk_viewer_control",
    };
    let connected = false,
      initial: Data | undefined;
    invoke = async (name, args = {}) => {
      const result = await app!.callServerTool({
        name: names[name]!,
        arguments: {
          ...args,
          view_id: context!.view_id,
          view_generation: context!.view_generation,
          ui_token: context!.ui_token,
        },
      });
      const data =
        result.structuredContent ??
        JSON.parse((result.content?.[0] as Data)?.text ?? "{}");
      return check(data);
    };
    app.ontoolresult = (result) => {
      const meta = result._meta?.["crossdesk/ui"] as Data | undefined;
      if (!meta) return;
      initial = { ...(result.structuredContent ?? {}), ...meta };
      if (connected) void ready(initial).catch(showError);
    };
    app.onteardown = async () => {
      await closePreview();
      return {};
    };
    await app.connect();
    connected = true;
    if (initial) await ready(initial);
  } else {
    let csrf = "";
    const request = async (path: string, body?: Data) =>
      check(
        await (
          await fetch("/api/" + path, {
            method: body ? "POST" : "GET",
            credentials: "same-origin",
            headers: body
              ? { "Content-Type": "application/json", "X-CrossDesk-CSRF": csrf }
              : {},
            body: body ? JSON.stringify(body) : undefined,
            cache: "no-store",
          })
        ).json(),
      );
    const bootstrap = new URLSearchParams(location.hash.slice(1)).get(
      "bootstrap",
    );
    history.replaceState(null, "", location.pathname);
    const result = bootstrap
      ? await request("bootstrap", { token: bootstrap })
      : await request("context");
    csrf = result.csrf;
    invoke = (name, args = {}) =>
      request(name, { ...args, view_id: context!.view_id });
    await ready(bootstrap ? result : result.views.at(-1));
  }
}
void start().catch(showError);
