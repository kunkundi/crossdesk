import type { App } from "@modelcontextprotocol/ext-apps";
import "./viewer.css";
import { HumanInput } from "./human-input.js";
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
            <label><input id="remember-unlock" type="checkbox" disabled><span>保存锁屏密码并允许自动解锁<small>独立保存在本机系统凭据库</small></span></label>
            <label><input id="authorize" type="checkbox" disabled><span>允许下次自动连接<small>沿用本次授权范围</small></span></label>
          </div>
          <div id="saved-unlock-field" hidden><label for="saved-unlock-password">系统锁屏密码</label><input id="saved-unlock-password" type="password" autocomplete="off" maxlength="256"></div>
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
      <dl class="session-details"><div id="session-device-id-row" hidden><dt>设备 ID</dt><dd id="session-device-id"></dd></div><div><dt>访问权限</dt><dd id="session-access"></dd></div><div><dt>显示器</dt><dd id="session-display"></dd></div><div><dt>Agent 状态</dt><dd id="session-agent"></dd></div><div><dt>锁屏密码</dt><dd id="session-unlock-credential"></dd></div><div id="session-unlock-row" hidden><dt>解锁状态</dt><dd class="session-unlock-details"><span id="session-unlock-state"></span><button id="unlock-open" type="button" hidden>输入密码</button></dd></div></dl>
      <label class="check"><input id="recovery" type="checkbox">断线自动恢复</label>
      <p class="session-note">关闭页面不会断开连接。</p>
    </div>
  </aside>
  <section class="workspace" aria-label="远程画面与会话控制">
    <div class="viewer-card">
      <div class="viewer-toolbar">
        <div class="viewer-title"><h2 id="device-name">远程画面</h2><span id="video-state">待连接</span></div>
        <div class="viewer-controls"><button id="take-over" disabled>人工接管</button><button id="pause" disabled><span id="pause-label">暂停控制</span></button><button id="disconnect" class="danger" disabled>断开连接</button><button id="focus-view" aria-label="放大画面" aria-pressed="false" disabled>放大画面</button></div>
      </div>
      <div class="screen" id="screen">
        <canvas id="frame" role="img" aria-label="远程桌面实时画面" hidden></canvas>
        <canvas id="agent-cursor" aria-hidden="true"></canvas>
        <textarea id="remote-input" aria-label="远程键盘输入" autocomplete="off" autocapitalize="off" spellcheck="false"></textarea>
        <div class="control-halo" aria-hidden="true"></div>
        <div id="empty"><h3 id="empty-title">未连接设备</h3><p id="empty-description">选择设备并输入访问密码。</p></div>
        <div class="stale" id="stale" role="status" hidden>画面暂未更新</div>
      </div>
      <div class="telemetry"><div class="stream-metrics"><span id="resolution">—</span><span id="fps">—</span></div><span id="session-info">等待设备连接</span><span id="permission">等待授权</span></div>
    </div>
    <div class="video-options"><button id="video-retry" hidden>重新加载画面</button><button id="video-browser" hidden>在浏览器中观看</button></div>
    <p id="panel-status" class="panel-status" role="status" hidden></p>
  </section>
</main>
<dialog id="unlock-dialog" class="unlock-dialog" aria-labelledby="unlock-title" aria-describedby="unlock-status">
  <div class="unlock-dialog-header"><h2 id="unlock-title">解锁远程电脑</h2><button id="unlock-close" type="button" aria-label="关闭解锁弹窗">×</button></div>
  <p id="unlock-status" role="status"></p>
  <form id="unlock-form">
    <label for="unlock-password">系统锁屏密码</label><input id="unlock-password" type="password" autocomplete="off" maxlength="256" required autofocus>
    <div class="checks"><label><input id="unlock-remember" type="checkbox"><span>保存密码并允许以后自动解锁<small>独立保存在本机系统凭据库</small></span></label></div>
    <div class="unlock-buttons"><button id="unlock-submit" class="primary" type="submit">解锁</button><button id="unlock-cancel" type="button">取消解锁</button></div>
  </form>
</dialog>
<div id="unlock-toast" class="unlock-toast" role="status" hidden><div><strong id="unlock-toast-title"></strong><p id="unlock-toast-message"></p></div><button id="unlock-toast-close" type="button" aria-label="关闭解锁提示">×</button></div>`;
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
let unlockBusy = false;
let unlockRequestId: string | undefined;
let unlockSessionId: string | undefined;
let unlockState: string | undefined;
let unlockError = "";
let unlockToastTimer: ReturnType<typeof setTimeout> | undefined;
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
const humanInput = new HumanInput(element("screen"), element<HTMLTextAreaElement>("remote-input"),
  (args) => invoke("input", args), showError);
function ownsInput() { return status.control_owner === "human" && status.human_view === context?.view_id; }
const messages: Record<string, string> = {
  HUMAN_CONTROL_ACTIVE: "远端正在由人工操作，请先交还 Agent。",
  STALE_OBSERVATION: "画面已变化，请等待新画面后继续操作。",
  STALE_CONTROL: "控制权已交接。",
  INPUT_QUEUE_FULL: "输入过快，已释放按键，请稍后继续。",
  NATIVE_INPUT_UNSUPPORTED: "请更新原生 CLI 以启用人工接管。",
  VIDEO_UNSUPPORTED: "当前浏览器不支持此视频格式，请使用支持 WebCodecs 的浏览器。",
  VIDEO_UNAVAILABLE: "视频通道暂不可用。可重新加载画面；内嵌面板受限时请在浏览器中观看。",
  VIDEO_INVALID: "视频格式发生变化，请重新加载画面。",
  VIDEO_LIMIT: "观看面板数量已达上限，请关闭其他面板。",
  AUTH_FAILED: "访问密码错误，请重新输入。",
  AUTH_REQUIRED: "请输入访问密码，或在本机 setup 中配置凭据。",
  CONNECTION_FAILED: "连接失败，请检查设备是否在线及服务器设置。",
  CONNECTION_TIMEOUT: "等待远端画面超时，请检查远端状态后重试。",
  CONNECTION_LOST: "远程连接已中断。请检查远端状态并重新连接。",
  signaling_timeout: "连接信令服务器超时，请检查本机网络、代理和服务器状态。",
  signaling_disconnected: "信令连接已中断，请检查本机网络、代理和服务器状态。",
  signaling_login_failed: "信令服务器登录失败，请检查服务器配置。",
  tls_certificate_error: "信令服务器证书验证失败，请检查证书和本机时间。",
  cli_identity_not_supported: "信令服务器需要支持 CLI 身份注册，请更新服务器。",
  join_request_failed: "发送设备连接请求失败，请检查信令连接。",
  incorrect_password: "访问密码错误，请重新输入。",
  device_not_found: "设备 ID 无效或已过期，请确认设备 ID。",
  device_offline_or_unavailable: "远端设备暂不可连接，请检查被控端的在线状态。",
  connection_timeout: "等待远端连接超时，请检查被控端和网络状态。",
  connection_failed: "远端连接建立失败，请检查双方网络和中继服务器状态。",
  connection_lost: "远端连接已中断，请检查双方网络状态。",
  remote_disconnected: "远端已断开连接。",
  display_not_available: "所选显示器当前不可用，请检查显示器索引。",
  media_initialization_failed: "本机媒体组件初始化失败，请检查原生 CLI 运行环境。",
  control_event_overflow: "连接事件队列已满，请稍后重新连接。",
  SESSION_REPLACED: "连接已恢复，请使用更新后的会话。",
  SESSION_LIMIT: "活动连接已达到八个，请先断开一个连接。",
  DEVICE_BUSY: "此设备已有活动连接。",
  CREDENTIAL_STORE_UNAVAILABLE: "系统凭据库不可用，密码未保存。",
  CREDENTIAL_ROLLBACK_FAILED: "凭据保存中断，请通过本机 setup 重新配置密码。",
  PAIRING_REQUIRED:
    "请在终端运行 Agent 工具结果中的 setup --open-panel 配对命令，再打开面板。",
  PANEL_UNAUTHORIZED: "面板授权已失效，请重新打开。",
  VIEW_UNAUTHORIZED: "此面板已失效，请使用最新打开的面板。",
  CONTROL_DENIED: "当前会话没有控制权限，或已被人工暂停。",
  UNLOCK_PENDING: "请先完成或取消当前解锁请求。",
  UNLOCK_CREDENTIAL_REQUIRED: "请输入系统锁屏密码。",
  UNLOCK_EXPIRED: "解锁请求已失效，请让 Agent 重新观察锁屏并发起请求。",
  UNLOCK_NOT_CONFIRMED: "系统解锁状态尚待确认，请检查远端画面。",
  NATIVE_UNLOCK_UNSUPPORTED: "请更新原生 CLI，以启用锁屏解锁。",
  SAVE_DEVICE_REQUIRED: "请先保存设备，再保存锁屏密码。",
  SAVE_UNLOCK_REQUIRED: "请勾选保存锁屏密码。",
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
  humanInput.setFrame(undefined);
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
  if (!ownsInput() && event.key === "Escape" && !element<HTMLDialogElement>("unlock-dialog").open && root.classList.contains("focus-mode")) setFocus(false);
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
  if (result.error) {
    const error = typeof result.error === "string"
      ? { code: "CONNECTION_FAILED", reason: result.error } : result.error;
    throw new Error(Object.hasOwn(messages, error.reason)
      ? error.reason : error.code ?? "INTERNAL_ERROR");
  }
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
  for (const name of ["remember", "remember-unlock", "authorize"]) {
    input(name).disabled = !input("save").checked;
    if (!input("save").checked) input(name).checked = false;
  }
  savedUnlockChanged();
};
function savedUnlockChanged() {
  element("saved-unlock-field").hidden = !input("remember-unlock").checked;
  if (!input("remember-unlock").checked) input("saved-unlock-password").value = "";
}
input("remember-unlock").onchange = savedUnlockChanged;
function hideUnlockToast() {
  clearTimeout(unlockToastTimer);
  unlockToastTimer = undefined;
  element("unlock-toast").hidden = true;
}
function openUnlockDialog() {
  const dialog = element<HTMLDialogElement>("unlock-dialog");
  if (status.unlock?.state !== "awaiting_password" || dialog.open) return;
  hideUnlockToast();
  dialog.showModal();
}
element("unlock-open").onclick = openUnlockDialog;
element("unlock-close").onclick = () => element<HTMLDialogElement>("unlock-dialog").close();
element<HTMLDialogElement>("unlock-dialog").onclose = () => {
  if (element<HTMLDialogElement>("unlock-dialog").open) return;
  input("unlock-password").value = "";
  input("unlock-remember").checked = false;
};
element("unlock-toast-close").onclick = hideUnlockToast;
function renderUnlock(s: Data) {
  const request = s.unlock;
  const pending = request?.state === "awaiting_password";
  const visible = !!request && ["ready", "waiting_frame"].includes(s.state);
  const sessionChanged = s.session_id !== unlockSessionId;
  const changed = sessionChanged || request?.request_id !== unlockRequestId;
  const stateChanged = changed || request?.state !== unlockState;
  const finished = ["submitted", "verified", "cancelled", "failed"].includes(request?.state);
  const dialog = element<HTMLDialogElement>("unlock-dialog");
  if (changed || !pending || !visible) {
    input("unlock-password").value = "";
    input("unlock-remember").checked = false;
  }
  if (changed || finished) unlockError = "";
  if (changed || !visible) hideUnlockToast();
  if (dialog.open && (changed || !visible || finished)) dialog.close();
  unlockRequestId = request?.request_id;
  unlockSessionId = s.session_id;
  unlockState = request?.state;
  const device = savedDevices.find((d) => d.device_alias === s.device_alias);
  if (request?.credential_saved && device) device.unlock_credential_provider = "keyring";
  const provider = request?.credential_saved ? "keyring" : device?.unlock_credential_provider;
  text("session-unlock-credential", provider === "keyring" ? "已保存 · 自动解锁"
    : provider === "environment" ? "环境变量 · 自动解锁" : "待配置");
  element("session-unlock-row").hidden = !visible;
  const states: Record<string, string> = {
    preparing: "准备中", awaiting_password: "等待密码", submitting: "提交中",
    submitted: "已提交", verifying: "验证中", verified: "已解锁", cancelled: "已取消", failed: "提交失败",
  };
  text("session-unlock-state", states[request?.state] ?? "—");
  element("session-unlock-state").dataset.state = request?.state ?? "";
  element("unlock-open").hidden = !visible || !pending;
  element<HTMLButtonElement>("unlock-open").disabled = unlockBusy;
  element("unlock-form").hidden = !pending;
  input("unlock-password").disabled = unlockBusy;
  input("unlock-remember").disabled = unlockBusy || !device;
  element<HTMLButtonElement>("unlock-submit").disabled = unlockBusy;
  element<HTMLButtonElement>("unlock-cancel").disabled = unlockBusy;
  text("unlock-status", unlockError || (unlockBusy || request?.state === "submitting"
    ? "正在提交解锁，请稍候。" : "请确认远端系统密码框已聚焦，然后输入锁屏密码。"));
  if (visible && pending && stateChanged) openUnlockDialog();
  if (visible && finished) {
    text("unlock-toast-title", request.state === "verified" ? "系统已解锁" : request.state === "submitted" ? "解锁请求已提交"
      : request.state === "cancelled" ? "解锁已取消" : "解锁操作未完成");
    let message = ["submitted", "verified"].includes(request.state) ? "Agent 将检查画面并继续任务。" : "";
    if (request.credential_saved) message = "锁屏密码已保存，Agent 将检查画面并继续任务。";
    if (request.error) message = messages[request.error.code] ?? "请检查远端画面后再发起解锁。";
    if (request.persistence_error) message = request.persistence_error === "UNLOCK_NOT_CONFIRMED"
      ? "锁屏密码需要在解锁确认后保存，请检查远端画面。" : "密码保存失败，可通过本机 setup 重新配置。";
    text("unlock-toast-message", message);
    element("unlock-toast-message").hidden = !message;
    element("unlock-toast").dataset.state = request.state;
    if (stateChanged && !sessionChanged) {
      element("unlock-toast").hidden = false;
      clearTimeout(unlockToastTimer);
      unlockToastTimer = setTimeout(hideUnlockToast, 6000);
    }
  }
}
element<HTMLFormElement>("unlock-form").onsubmit = async (event) => {
  event.preventDefault();
  if (unlockBusy || !unlockRequestId) return;
  const args: Data = { request_id: unlockRequestId, password: input("unlock-password").value,
    remember_password: input("unlock-remember").checked };
  input("unlock-password").value = "";
  unlockError = "";
  unlockBusy = true;
  renderUnlock(status);
  try {
    const result = await invoke("unlock", args);
    renderStatus({ ...status, unlock: result.unlock });
    await devices();
  } catch (error) {
    unlockError = messages[error instanceof Error ? error.message : ""] ?? "解锁请求暂时无法提交，请检查连接状态。";
  }
  finally { args.password = undefined; unlockBusy = false; renderUnlock(status); }
};
element("unlock-cancel").onclick = async () => {
  if (!unlockRequestId || unlockBusy) return;
  input("unlock-password").value = "";
  try {
    const result = await invoke("unlock", { request_id: unlockRequestId, cancel: true });
    renderStatus({ ...status, unlock: result.unlock });
  } catch (error) {
    unlockError = messages[error instanceof Error ? error.message : ""] ?? "取消请求暂时无法提交，请检查连接状态。";
    renderUnlock(status);
  }
};
function renderStatus(s: Data) {
  const connected = s.state === "ready" || s.state === "waiting_frame";
  const lost = ["closed", "failed"].includes(s.state);
  if (lost && !disconnectedByUser && ["ready", "waiting_frame"].includes(status.state)) showError(new Error("CONNECTION_LOST"));
  const changedSession = s.session_id !== status.session_id;
  status = s;
  humanInput.update(connected && ownsInput() ? s.human_input_epoch : undefined);
  text("take-over", ownsInput() ? "交还 Agent" : "人工接管");
  element<HTMLButtonElement>("take-over").disabled = !connected || !s.control_allowed ||
    s.viewer_input_protocol !== "events/1" || controlBusy || (!imageAt && !ownsInput());
  renderUnlock(s);
  const connecting = busy || s.state === "waiting_frame";
  root.dataset.connected = String(connected);
  root.dataset.connecting = String(connecting);
  root.dataset.controlling = String(connected && s.control === true && s.control_allowed === true && !s.human_paused);
  element("connection").dataset.state = connected ? "online" : connecting ? "connecting" : lost && !disconnectedByUser ? "offline" : "idle";
  text("connection-label", connected ? "已连接" : connecting ? "正在连接" : lost ? "未连接" : "等待连接");
  element("connect-form").hidden = connected || s.state === "reconnecting";
  element("session-card").hidden = !connected && s.state !== "reconnecting";
  text("sidebar-title", connected ? "连接信息" : "连接设备");
  text("device-name", connected ? s.device_alias || "远程电脑" : "远程画面");
  if (!connected || changedSession) setDetails(false);
  text("permission", connected ? s.control_owner === "human" ? ownsInput() ? "正在人工操作" : "其他面板正在操作" : s.human_paused ? "控制已暂停" : s.control_allowed ? "允许 Agent 操作" : "仅查看画面" : "等待授权");
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
  text("session-agent", s.state === "reconnecting" ? "正在恢复连接" : s.control_owner === "human" ? "人工接管中" : s.human_paused ? "已暂停" : s.control_allowed ? "允许操作" : "仅可观察");
  input("recovery").checked = s.reconnect_enabled === true;
  input("recovery").disabled = !s.session_id;
  element("session-agent").dataset.paused = String(!!s.human_paused);
  element<HTMLButtonElement>("pause").disabled = !connected || !s.control_allowed || controlBusy || s.control_owner === "human";
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
    remember_unlock_password: input("remember-unlock").checked,
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
  if (input("remember-unlock").checked) args.unlock_password = input("saved-unlock-password").value;
  input("password").value = "";
  input("saved-unlock-password").value = "";
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
input("recovery").onchange = async () => {
  input("recovery").disabled = true;
  try {
    await invoke("recovery", { enabled: input("recovery").checked });
    renderStatus(await invoke("status"));
  } catch (error) { showError(error); renderStatus(status); }
};
element("take-over").onclick = async () => {
  if (controlBusy) return;
  controlBusy = true;
  humanInput.release();
  renderStatus(status);
  try {
    await invoke("control", { operation: ownsInput() ? "resume" : "take_over" });
    renderStatus(await invoke("status"));
  } catch (error) { showError(error); }
  finally { controlBusy = false; renderStatus(status); }
};
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
      humanInput.setFrame(info);
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
    }, failed, element<HTMLCanvasElement>("agent-cursor"));
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
  element<HTMLDialogElement>("unlock-dialog").close();
  hideUnlockToast();
  input("unlock-password").value = "";
  input("saved-unlock-password").value = "";
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
      unlock: "crossdesk_ui_unlock",
      recovery: "crossdesk_ui_recovery",
      input: "crossdesk_ui_input",
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
    const previous = sessionStorage.getItem("crossdesk-view");
    const selected = bootstrap ? result : result.views.find((view: Data) => view.view_id === previous) ?? result.views.at(-1);
    await ready(selected);
    sessionStorage.setItem("crossdesk-view", selected.view_id);
  }
}
void start().catch(showError);
