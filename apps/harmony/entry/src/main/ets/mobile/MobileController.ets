import { tr } from './Localization';
import { RemoteDisplayResolutions, RemoteResolutionStore, ResolutionDisplay, ResolutionMode, ResolutionRequest, ResolutionPreference, resolutionLabel } from './RemoteDisplayResolutions';
import { RemotePrivacyScreen, PrivacyStatus } from './RemotePrivacyScreen';
import bridge from 'libcrossdesk.so';
import { common, bundleManager } from '@kit.AbilityKit';
import { display, promptAction } from '@kit.ArkUI';
import { util } from '@kit.ArkTS';
import { pasteboard, systemDateTime } from '@kit.BasicServicesKit';
import { MobileStore, RecentDevice } from './MobileStore';
import { MobilePlatform, MobileLifecycle } from './MobilePlatform';
import { AnnouncementInbox, AnnouncementItem, AnnouncementRequest, AnnouncementResponse } from './AnnouncementInbox';
import { VideoSettings, VideoValues, NetworkStatistics, NetworkReport, RecentPresence, PresenceMessage, asciiKey } from './ControllerModels';

interface Command {
  modifiers?: number[];
  trustKey?: string;
  displayResolutions?: ResolutionPreference[];
  displayId?: number; displayKey?: string; width?: number; height?: number;
  op: string; session?: number; connection?: number; server?: string; port?: number; identity?: string;
  logs?: string; certificates?: string; directory?: string; previewDirectory?: string; remote?: string; password?: string; version?: string;
  id?: string; x?: number; y?: number; flag?: number; scroll?: number; code?: number; down?: boolean;
  index?: number; muted?: boolean; visible?: boolean; text?: string; message?: Object; quality?: number; frameRate?: number;
  preference?: number; blockLocalInput?: boolean; request?: number; path?: string; name?: string; body?: string; local?: boolean;
}
interface NativeEvent {
  type: string; session: number; connection?: number; status?: number; controller?: boolean; generation?: number;
  id?: string; identity?: string; message?: Object; report?: NetworkReport; width?: number; height?: number;
  frame?: number; latency?: number; latencyCount?: number; text?: string; reason?: string; name?: string; progress?: number;
  sending?: boolean; path?: string; ok?: boolean; request?: number | string; latest?: string;
}
export interface RemoteDisplay { name: string; }
interface HostInfo { host_name: string; platform: string; app_version: string; supports_video_settings: boolean; displays: RemoteDisplay[]; display_resolutions?: ResolutionDisplay[]; }
interface SettingAck { quality: number; frame_rate: number; preference: number; request_id: number; accepted: boolean; }
interface ServiceStatus { available: boolean; interactive_stage?: string; }
interface VideoCodecStatus { encoder: string; }
interface ControlData { display_resolution?: ResolutionRequest; display_resolutions?: ResolutionDisplay[]; type: number; host_info?: HostInfo; video_settings?: SettingAck; service_status?: ServiceStatus; privacy_status?: PrivacyStatus; video_codec?: VideoCodecStatus; }
interface PresenceRequest { type: string; devices: string[]; subscribe: boolean; }
interface SignalType { type: string; }
interface TrustInfo { pairing: string; fingerprint: string; }

@Observed
export class MobileState {
  trustFingerprint: string = '';
  page: string = 'home';
  consent: boolean = false;
  privacyAvailable: boolean = false;
  modal: string = '';
  notice: string = '';
  server: string = 'api.crossdesk.cn';
  port: string = '9099';
  remote: string = '';
  password: string = '';
  remember: boolean = false;
  signal: number = -1;
  signalText: string = tr("未连接");
  identity: string = '';
  connecting: boolean = false;
  ready: boolean = false;
  relative: boolean = true;
  muted: boolean = false;
  previews: boolean = false;
  videoPreference: number = 1;
  autoLandscape: boolean = false;
  version: string = '0.1.0';
  update: string = '';
  updateStatus: string = '';
  recent: RecentDevice[] = [];
  width: number = 16;
  height: number = 9;
  hasFrame: boolean = false;
  displays: RemoteDisplay[] = [];
  selectedDisplay: number = 0;
  viewportReset: number = 0;
  platform: string = '';
  hostName: string = '';
  transfer: string = '';
  receivedPath: string = '';
  receivedName: string = '';
  remoteClipboard: string = '';
  clipboardText: string = '';
  videoQuality: number = 1;
  videoRate: number = 30;
  videoSelectedPreference: number = 1;
  videoSupported: boolean = false;
  videoFeedback: string = '';
  resolutionModes: ResolutionMode[] = [];
  resolutionKey: string = '';
  resolutionCurrent: string = tr("设备不支持");
  resolutionEnabled: boolean = false;
  resolutionFeedback: string = '';
  privacyStatus: string = '';
  privacyButtonTitle: string = tr("隐私屏");
  privacyFeedback: string = '';
  privacyEnabled: boolean = false;
  privacyActive: boolean = false;
  privacyCanToggle: boolean = false;
  privacyCanLock: boolean = false;
  privacyVisible: boolean = true;
  networkTime: number = 0;
  unread: number = 0;
  announcements: AnnouncementItem[] = [];
  announcement?: AnnouncementItem;
  documentTitle: string = '';
  documentText: string = '';
  documentBack: string = 'about';
}
export class MobileController {
  state: MobileState = new MobileState();
  store: MobileStore;
  platform: MobilePlatform;
  video: VideoSettings = new VideoSettings();
  private resolutions: RemoteDisplayResolutions = new RemoteDisplayResolutions();
  private resolutionTimeout: number = -1;
  private resolutionScope: string = '';
  private resolutionStore: RemoteResolutionStore;
  private privacyScreen: RemotePrivacyScreen = new RemotePrivacyScreen();
  network: NetworkStatistics = new NetworkStatistics();
  presence: RecentPresence;
  inbox: AnnouncementInbox;
  releaseInput: () => void = () => {};
  private foreground: boolean = true;
  private context: common.UIAbilityContext;
  private timer: number = -1;
  private seconds: number = 0;
  private session: number = 0;
  private connection: number = 0;
  private started: boolean = false;
  private starting: boolean = false;
  private signalGeneration: number = -1;
  private pendingPassword: string = '';
  private connectingId: string = '';
  private sendingFile: boolean = false;
  private serviceAvailable: boolean = false;
  private settingsTimeout: number = -1;
  private versionRequest: number = 0;
  private localVersionRequest: number = 0;
  private remoteVersionRequest: number = 0;
  private checkingLocal: boolean = false;
  private checkedRemote: boolean = false;
  private remoteVersion: string = '';
  private receivedHostInfo: boolean = false;
  private previewEligible: boolean = false;
  private historySaved: boolean = false;
  private previewPending: boolean = false;
  private previewSaved: boolean = false;
  private previewAttempts: number = 0;
  private uiEpoch: number = 0;
  private privacy: string = '';
  constructor(context: common.UIAbilityContext) {
    this.context = context; this.store = new MobileStore(context);
    this.resolutionStore = new RemoteResolutionStore((key: string) => this.store.text(key, '[]'),
      (key: string, value: string) => this.store.put(key, value));
    this.state.consent = this.store.flag('consent'); this.state.server = this.store.text('server', 'api.crossdesk.cn');
    this.state.port = this.store.text('port', '9099'); this.state.remote = this.store.text('lastRemote');
    this.state.relative = this.store.flag('relative', true); this.state.videoPreference = this.store.number('videoPreference', 1);
    this.state.autoLandscape = this.store.flag('autoLandscape', false);
    this.state.previews = this.store.flag('previews');
    try { this.state.version = bundleManager.getBundleInfoForSelfSync(bundleManager.BundleFlag.GET_BUNDLE_INFO_DEFAULT).versionName; } catch {}
    try {
      this.privacy = util.TextDecoder.create('utf-8').decodeWithStream(context.resourceManager.getRawFileContentSync('PRIVACY.md'));
      this.state.privacyAvailable = this.privacy.trim().length > 0;
    } catch { this.state.privacyAvailable = false; }
    this.platform = new MobilePlatform(context, (reason: string) => this.end(reason), () => this.release(), (text: string) => this.toast(text));
    this.platform.onMute = (muted: boolean) => { if (muted !== this.state.muted) this.mute(); };
    this.platform.onVisibility = (visible: boolean) => { if (this.state.ready) this.send({ op: 'visible', visible: visible }); };
    this.presence = new RecentPresence((ids: string[]) => {
      const request: PresenceRequest = { type: 'recent_connections_presence', devices: ids, subscribe: true };
      this.send({ op: 'signal', message: request });
    });
    this.inbox = new AnnouncementInbox((request: AnnouncementRequest) => this.send({ op: 'signal', message: request }),
      (scope: string) => this.store.text(`inbox:${scope}`, '{}'), (scope: string, body: string) => this.store.put(`inbox:${scope}`, body));
    this.inbox.changed = () => { this.state.announcements = this.inbox.items.slice(); this.state.unread = this.inbox.unread; };
    MobileLifecycle.foreground = () => this.resume(); MobileLifecycle.background = () => this.background(); MobileLifecycle.destroy = () => this.destroy();
    this.history(); this.timer = setInterval(() => this.poll(), 50);
    if (!this.state.consent) this.state.modal = 'consent'; else this.resume();
  }
  private send(command: Command): void { bridge.mobileCommand(JSON.stringify(command)); }
  scope(): string { return MobileStore.scope(this.state.server, Number(this.state.port)); }
  now(): number { return systemDateTime.getUptime(systemDateTime.TimeType.STARTUP, false); }
  toast(text: string): void { if (this.foreground) promptAction.showToast({ message: text, duration: 3000 }); else this.state.notice = text; }
  async consent(): Promise<void> {
    if (!this.state.privacyAvailable) { this.state.notice = tr("隐私政策无法读取，请重新安装应用"); return; }
    try { this.store.put('consent', true); this.state.consent = true; this.state.modal = ''; await this.ensureSignal(); this.checkLocal(); }
    catch { this.toast(tr("无法保存联网授权，请重试")); }
  }
  revoke(): void {
    this.end(''); this.stopSignal(); this.state.consent = false; this.state.previews = false;
    this.store.put('consent', false); this.store.put('previews', false); this.store.clearPreviews(); this.history();
    this.state.update = ''; this.state.updateStatus = ''; this.state.page = 'home'; this.state.modal = 'consent';
  }
  resume(): void {
    this.foreground = true; this.platform.resume(); this.syncPrivacy();
    if (this.state.consent) { this.ensureSignal(); this.checkLocal(); this.inbox.connect(this.state.signal === 1); }
    else this.state.modal = 'consent';
  }
  background(): void {
    this.foreground = false; this.uiEpoch++; this.release(); this.platform.background();
    this.localVersionRequest = this.remoteVersionRequest = 0; this.checkingLocal = false; this.inbox.connect(false);
    if (!this.state.ready && !this.platform.choosing) { if (this.state.connecting) this.end(''); this.stopSignal(); }
  }
  destroy(): void { this.foreground = false; this.end(''); this.stopSignal(); this.platform.dispose(); if (this.timer >= 0) clearInterval(this.timer); this.timer = -1; }
  private async ensureSignal(): Promise<void> {
    if (!this.foreground || !this.state.consent || this.started || this.starting) return;
    this.starting = true; const session = ++this.session, scope = this.scope();
    try {
      let identity = await this.store.secret(`identity:${scope}`);
      const trustKey = await this.store.trustKey();
      // A public ID alone is not a login credential. Discard legacy incomplete records.
      if (identity && !/^[0-9]{9}@.+$/.test(identity)) { await this.store.removeSecret(`identity:${scope}`); identity = ''; }
      if (session !== this.session || !this.foreground || !this.state.consent) return;
      this.state.signalText = tr("正在连接…"); this.state.signal = 0; this.started = true;
      this.send({ op: 'start', session: session, server: this.state.server, port: Number(this.state.port), identity: identity,
        trustKey: trustKey, logs: this.context.cacheDir, certificates: '/etc/security/certificates', directory: this.store.directory,
        previewDirectory: this.store.previewDirectory, version: this.state.version });
    } catch { this.state.signalText = tr("安全存储不可用，请解锁设备后重试"); this.state.signal = -1; }
    finally { if (session === this.session) this.starting = false; }
  }
  private stopSignal(): void {
    this.session++; this.starting = this.started = false; this.signalGeneration = -1;
    this.send({ op: 'stop' }); this.state.signal = -1; this.state.identity = ''; this.state.signalText = tr("未连接");
    this.presence.connect(false, this.now()); this.inbox.connect(false); this.platform.cancelRequests();
    this.localVersionRequest = this.remoteVersionRequest = 0; this.checkingLocal = false;
  }
  reconnect(): void { if (!this.state.consent) { this.state.modal = 'consent'; return; } this.stopSignal(); this.ensureSignal(); }
  saveSettings(server: string, port: string): void {
    server = server.trim(); const number = Number(port);
    if (!server || server.length > 253 || /[\s/@?#]/.test(server) || !Number.isInteger(number) || number < 1 || number > 65535) {
      this.toast(tr("请输入有效的服务器地址和端口")); return;
    }
    const changed = server !== this.state.server || port !== this.state.port;
    this.store.put('server', server); this.store.put('port', port); this.state.server = server; this.state.port = port;
    if (changed) { this.end(''); this.stopSignal(); this.inbox.reset(); this.history(); this.ensureSignal(); }
    this.navigate('settings');
  }
  navigate(page: string): void { this.state.page = page; this.state.modal = ''; }
  async pairingInfo(copy: boolean): Promise<void> {
    try {
      const seed = await this.store.trustKey();
      const info = JSON.parse(bridge.mobileTrustInfo(seed, this.state.identity)) as TrustInfo;
      if (!this.foreground || this.state.page !== 'settings') return;
      this.state.trustFingerprint = info.fingerprint.replace(/(.{4})(?!$)/g, '$1 ');
      if (copy) {
        try {
          await pasteboard.getSystemPasteboard().setData(pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, info.pairing));
          this.toast(tr("配对信息已复制。"));
        } catch { this.toast(tr("无法复制配对信息，请重试。")); }
      }
    } catch { this.toast(tr("安全存储不可用，请解锁设备后重试")); }
  }
  history(): void {
    const values = this.store.recent(this.scope()), now = this.now(); this.presence?.watch(values.map((d: RecentDevice) => d.id), now);
    this.state.recent = values.filter((d: RecentDevice) => this.presence?.online(d.id, now))
      .concat(values.filter((d: RecentDevice) => !this.presence?.online(d.id, now)));
  }
  async deleteRecent(id: string): Promise<void> {
    try { await this.store.delete(this.scope(), id); this.history(); } catch { this.toast(tr("记录或密码删除失败，请重试")); }
  }
  async requestConnection(id: string): Promise<void> {
    id = id.replace(/\s/g, ''); this.state.remote = id;
    if (!/^[0-9]{9}$/.test(id)) { this.toast(tr("请输入 9 位设备 ID")); return; }
    if (!this.state.consent) { this.state.modal = 'consent'; return; }
    const epoch = ++this.uiEpoch, scope = this.scope();
    this.state.password = ''; this.state.notice = '';
    this.state.remember = this.store.recent(scope).find((d: RecentDevice) => d.id === id)?.remember ?? false;
    try {
      const password = await this.store.secret(`password:${scope}:${id}`);
      if (epoch !== this.uiEpoch || !this.foreground || !this.state.consent) return;
      if (password) { this.state.remember = true; this.begin(password); return; }
    } catch { if (epoch === this.uiEpoch) this.state.notice = tr("已保存密码无法读取，请重新输入"); }
    if (epoch === this.uiEpoch) this.state.modal = 'password';
  }
  begin(password: string): void {
    if (!this.state.consent || !/^[0-9]{9}$/.test(this.state.remote) || !password || password.length > 128 || password.includes('@')) { this.toast(tr("请输入有效的访问密码")); return; }
    this.joinSent = false; this.connection++; this.connectingId = this.state.remote; this.pendingPassword = password;
    this.resolutionScope = this.scope();
    this.state.connecting = true; this.state.ready = false; this.state.modal = 'connecting'; this.state.notice = '';
    this.state.muted = false; this.state.platform = ''; this.state.hostName = ''; this.state.receivedPath = this.state.receivedName = this.state.transfer = '';
    this.state.remoteClipboard = ''; this.state.displays = []; this.state.width = 16; this.state.height = 9; this.state.hasFrame = false;
    this.state.selectedDisplay = 0; this.state.viewportReset++; this.remoteVersion = ''; this.receivedHostInfo = false;
    this.serviceAvailable = this.checkedRemote = this.sendingFile = false;
    this.previewEligible = this.state.previews; this.historySaved = false; this.previewSaved = this.previewPending = false; this.previewAttempts = 0;
    this.network = new NetworkStatistics(); let edge = 1920;
    try { const screen = display.getDefaultDisplaySync(); edge = Math.max(screen.width, screen.height); } catch {}
    this.video = new VideoSettings(this.state.videoPreference, edge); this.syncVideo();
    this.resetResolutions();
    this.privacyScreen = new RemotePrivacyScreen(); this.syncPrivacy();
    this.store.put('lastRemote', this.state.remote); this.state.password = '';
    this.ensureSignal(); this.joinIfReady();
  }
  private joinSent: boolean = false;
  private joinIfReady(): void {
    if (!this.state.connecting || this.joinSent || this.state.signal !== 1 || !this.state.identity) return;
    this.joinSent = true; this.send({ op: 'connect', connection: this.connection, remote: this.connectingId, password: this.pendingPassword,
      displayResolutions: this.resolutionStore.load(this.resolutionScope, this.connectingId) });
  }
  end(reason: string): void {
    this.release(); this.send({ op: 'disconnect' }); this.platform.disconnect();
    this.resetResolutions();
    this.connection++; this.uiEpoch++; this.pendingPassword = ''; this.joinSent = false; this.sendingFile = false;
    this.privacyScreen = new RemotePrivacyScreen(); this.syncPrivacy();
    this.state.connecting = this.state.ready = false; this.state.page = 'home'; this.state.modal = ''; this.state.password = '';
    this.state.receivedPath = this.state.receivedName = this.state.remoteClipboard = this.state.clipboardText = ''; this.remoteVersionRequest = 0;
    if (this.settingsTimeout >= 0) clearTimeout(this.settingsTimeout); this.settingsTimeout = -1;
    this.history(); if (reason) { this.state.notice = reason; this.state.modal = 'notice'; }
    if (this.foreground && this.state.consent) this.checkLocal();
  }
  release(): void { this.releaseInput(); this.send({ op: 'release' }); }
  pointer(x: number, y: number, flag: number, scroll: number): void { if (this.state.modal === 'privacyUnlock') return; this.send({ op: 'pointer', x: x, y: y, flag: flag, scroll: scroll }); }
  physicalKey(code: number, down: boolean): void { if (this.state.modal === 'privacyUnlock') return; this.send({ op: 'key', code: code, down: down }); }
  key(code: number, down: boolean): void { if (this.state.modal === 'privacyUnlock') return; this.send({ op: 'virtualKey', code: code, down: down }); }
  chord(code: number, modifiers: number[] = []): void {
    if (!this.state.ready || !this.state.consent || this.state.modal === 'privacyUnlock') return;
    this.send({ op: 'chord', code, modifiers: [17, 16, 18, 91].filter((key: number) => modifiers.includes(key)), connection: this.connection });
  }
  shortcut(code: number): void { this.chord(code, [this.state.platform === 'macos' ? 91 : 17]); }
  text(text: string): void {
    if (text.length > 2048 || Array.from(text).some((c: string) => asciiKey(c) === 0)) { this.toast(tr("中文等文本请通过剪贴板发送，再点击粘贴")); return; }
    for (const c of text) { const code = asciiKey(c); this.chord(code & 255, code & 256 ? [16] : []); }
  }
  setRelative(value: boolean): void { this.release(); this.state.relative = value; this.store.put('relative', value); }
  mute(): void { this.state.muted = !this.state.muted; this.platform.setMuted(this.state.muted); this.send({ op: 'mute', muted: this.state.muted }); }
  selectDisplay(index: number): void {
    if (index < 0 || index >= this.state.displays.length) return;
    this.release(); this.state.selectedDisplay = index; this.state.viewportReset++; this.state.hasFrame = false; this.network.reset();
    this.send({ op: 'display', index: index }); this.state.modal = ''; this.syncResolutions();
  }
  updateVideo(field: number, value: number): void {
    const old = this.video.selection;
    const next: VideoValues = { quality: field === 0 ? value : old.quality, frameRate: field === 1 ? value : old.frameRate, preference: field === 2 ? value : old.preference };
    const id = this.video.begin(next); if (id < 0) return;
    this.send({ op: 'videoSettings', quality: next.quality, frameRate: next.frameRate, preference: next.preference, request: id });
    if (this.settingsTimeout >= 0) clearTimeout(this.settingsTimeout);
    this.settingsTimeout = setTimeout(() => { this.video.expire(id); this.syncVideo(); }, 5000); this.syncVideo();
  }
  updateResolution(mode: ResolutionMode, displayId: number, displayKey: string): void {
    if (!this.state.ready || !this.state.consent) return;
    const display = this.resolutions.display(this.state.selectedDisplay);
    if (!display || display.display_id !== displayId || display.display_key !== displayKey) return;
    if (!this.resolutions.pending && display.width === mode.width && display.height === mode.height &&
        display.modes.some((value: ResolutionMode) => value.width === mode.width && value.height === mode.height)) {
      this.resolutionStore.remember(this.resolutionScope, this.connectingId,
        { display_key: displayKey, width: mode.width, height: mode.height });
      return;
    }
    const request = this.resolutions.begin(displayId, mode);
    if (!request) return;
    this.send({ op: 'displayResolution', displayId: request.display_id, displayKey: request.display_key,
      width: request.width, height: request.height, request: request.request_id });
    if (this.resolutionTimeout >= 0) clearTimeout(this.resolutionTimeout);
    this.resolutionTimeout = setTimeout(() => { this.resolutions.expire(request.request_id); this.syncResolutions(); }, 10000);
    this.syncResolutions();
  }
  private resetResolutions(): void {
    if (this.resolutionTimeout >= 0) clearTimeout(this.resolutionTimeout);
    this.resolutionTimeout = -1; this.resolutions = new RemoteDisplayResolutions(); this.syncResolutions();
  }
  private syncResolutions(): void {
    const display = this.resolutions.display(this.state.selectedDisplay);
    this.state.resolutionModes = display?.modes ?? [];
    this.state.resolutionKey = display?.display_key ?? '';
    this.state.resolutionCurrent = display ? resolutionLabel(display) : tr("设备不支持");
    this.state.resolutionEnabled = this.state.ready && !this.resolutions.pending && this.state.resolutionModes.length > 0;
    this.state.resolutionFeedback = this.resolutions.feedback();
    if (!this.resolutions.pending && this.resolutionTimeout >= 0) { clearTimeout(this.resolutionTimeout); this.resolutionTimeout = -1; }
  }
  setPreference(value: number): void { this.store.put('videoPreference', value); this.state.videoPreference = value; }
  setAutoLandscape(value: boolean): void { this.store.put('autoLandscape', value); this.state.autoLandscape = value; }
  setPreviews(value: boolean): void {
    this.store.put('previews', value); this.state.previews = value;
    if (!value) { this.send({ op: 'cancelPreview' }); this.previewEligible = false; this.store.clearPreviews(); this.history(); }
    this.state.modal = '';
  }
  clearPreviews(): void { this.send({ op: 'cancelPreview' }); this.previewEligible = false; this.store.clearPreviews(); this.history(); this.toast(tr("已清除预览")); }
  clipboard(): void {
    const text = this.state.clipboardText;
    if (new util.TextEncoder().encodeInto(text).length > 128 * 1024) { this.toast(tr("文本超过 128 KB")); return; }
    if (text) { this.send({ op: 'clipboard', text: text }); this.toast(tr("已发送，请在远端粘贴")); }
  }
  async copyRemote(): Promise<void> {
    if (!this.state.remoteClipboard) { this.toast(tr("尚未收到远端文本")); return; }
    try { await pasteboard.getSystemPasteboard().setData(pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, this.state.remoteClipboard)); this.toast(tr("已复制远端文本")); }
    catch { this.toast(tr("复制失败，请重试")); }
  }
  sas(): void {
    if (this.state.platform !== 'windows') return;
    if (!this.serviceAvailable) { this.toast(tr("请在远端启用 CrossDesk 系统服务后重试")); return; }
    this.send({ op: 'sas' });
  }
  async chooseFile(): Promise<void> {
    if (this.sendingFile) { this.toast(tr("请等待当前文件发送完成")); return; }
    const connection = this.connection; const file = await this.platform.choose(this.store.directory);
    if (connection !== this.connection || !this.state.ready || !file) { if (file) this.store.discardOutgoing(file.path); return; }
    this.sendingFile = true; this.state.transfer = tr("发送 {0} · 0%", [String(file.name)]);
    this.send({ op: 'sendFile', path: file.path, name: file.name });
  }
  saveFile(): void { if (this.state.receivedPath) this.platform.save(this.state.receivedPath, this.state.receivedName); }
  surface(id: string, controller?: XComponentController): void {
    this.send({ op: 'surface', id: id }); if (controller) this.platform.surface(controller, this.state.width, this.state.height);
  }
  async checkLocal(manual: boolean = false): Promise<void> {
    if (!this.state.consent || !this.foreground || this.checkingLocal) return;
    this.checkingLocal = true; const request = ++this.versionRequest; this.localVersionRequest = request;
    const epoch = this.uiEpoch; if (manual) this.state.updateStatus = tr("正在检查…");
    const body = await this.platform.version();
    if (request !== this.localVersionRequest || epoch !== this.uiEpoch || !this.foreground || !this.state.consent) return;
    this.checkingLocal = false;
    if (body) this.send({ op: 'version', body: body, local: true, version: this.state.version, request: request });
    else if (manual) this.state.updateStatus = tr("检查失败，请稍后重试");
  }
  private async checkRemote(version: string): Promise<void> {
    if (this.checkedRemote || !this.foreground || !this.state.consent || !this.state.ready) return;
    this.checkedRemote = true;
    if (!version || !/^\d+\.\d+\.\d+/.test(version)) { this.state.notice = tr("被控端版本过低，请升级到最新版本。"); this.state.modal = 'upgrade'; return; }
    const request = ++this.versionRequest, connection = this.connection; this.remoteVersionRequest = request;
    const body = await this.platform.version();
    if (body && request === this.remoteVersionRequest && connection === this.connection && this.foreground && this.state.consent)
      this.send({ op: 'version', body: body, version: version, request: request });
  }
  document(name: string, title: string, back: string = 'about'): void {
    try {
      this.state.documentText = name === 'PRIVACY.md' ? this.privacy : util.TextDecoder.create('utf-8').decodeWithStream(this.context.resourceManager.getRawFileContentSync(name));
      this.state.documentTitle = title; this.state.documentBack = back; this.state.page = 'document'; this.state.modal = '';
    } catch { this.toast(tr("无法读取文档")); }
  }
  dismissPrivacyUnlock(): void {
    this.privacyScreen.unlockNotice = false;
    if (this.state.modal === 'privacyUnlock') this.state.modal = '';
  }
  lockPrivacy(): void { this.sendPrivacy(true); }
  togglePrivacy(): void { this.sendPrivacy(false); }
  private sendPrivacy(lock: boolean): void {
    if (!this.state.ready || !this.state.consent) return;
    const enable = !this.privacyScreen.active(), request = this.privacyScreen.begin(this.now(), lock);
    if (request < 0) return;
    this.send({ op: 'privacy', flag: lock ? 3 : enable ? 1 : 2, request: request,
      blockLocalInput: true });
    this.syncPrivacy();
  }
  private syncPrivacy(): void {
    const now = this.now(); this.privacyScreen.tick(now);
    this.state.privacyStatus = this.privacyScreen.status(now); this.state.privacyFeedback = this.privacyScreen.feedback(now);
    this.state.privacyButtonTitle = this.privacyScreen.buttonTitle(now);
    this.state.privacyVisible = this.privacyScreen.desktopAvailable;
    this.state.privacyCanLock = this.privacyScreen.canLock(now);
    if (this.foreground && this.state.ready && this.privacyScreen.unlockNotice && !this.state.modal) {
      this.release();
      this.state.modal = 'privacyUnlock';
    } else if (!this.privacyScreen.unlockNotice && this.state.modal === 'privacyUnlock') {
      this.state.modal = '';
    }
    this.state.privacyEnabled = this.privacyScreen.enabled(now);
    this.state.privacyActive = this.privacyScreen.active(); this.state.privacyCanToggle = this.privacyScreen.canToggle(now);
  }
  private syncVideo(): void {
    this.state.videoQuality = this.video.selection.quality; this.state.videoRate = this.video.selection.frameRate;
    this.state.videoSelectedPreference = this.video.selection.preference; this.state.videoSupported = this.video.supported;
    this.state.videoFeedback = this.video.feedback();
  }
  private poll(): void {
    try { const events = JSON.parse(bridge.mobilePoll()) as NativeEvent[]; events.forEach((event: NativeEvent) => this.event(event)); }
    catch { this.state.signalText = tr("原生通信库不可用，请重新构建并安装"); }
    if (++this.seconds % 20 === 0) {
      if (this.state.ready) this.syncPrivacy();
      this.state.networkTime = this.now(); this.presence.tick(this.state.networkTime); this.history();
    }
  }
  private event(e: NativeEvent): void {
    if (e.type !== 'version' && (e.session !== this.session || (e.connection !== undefined && e.connection !== this.connection))) {
      if (e.type === 'preview' && e.path) this.store.discardRaw(e.path); return;
    }
    if (e.type === 'identity' && e.identity) {
      this.state.identity = e.identity.split('@')[0]; const scope = this.scope();
      if (e.identity.includes('@')) this.store.saveSecret(`identity:${scope}`, e.identity).catch(() => this.toast(tr("设备身份保存失败，请检查安全存储")));
      this.joinIfReady(); this.configureInbox();
    } else if (e.type === 'signal' && !e.controller) {
      if (e.generation !== this.signalGeneration) { this.signalGeneration = e.generation ?? -1; this.presence.connect(false, this.now()); this.inbox.connect(false); }
      this.state.signal = e.status ?? -1; this.state.identity = e.id || this.state.identity;
      const labels = [tr("正在连接…"), tr("已连接"), tr("连接失败"), tr("服务器已断开"), tr("正在重连…"), tr("服务器已断开"), tr("服务器证书校验失败")];
      this.state.signalText = labels[this.state.signal] ?? tr("未连接"); this.presence.connect(this.state.signal === 1, this.now()); this.configureInbox(); this.joinIfReady();
    } else if (e.type === 'signalMessage' && e.generation === this.signalGeneration && e.message) {
      const type = (e.message as SignalType).type;
      if (type === 'announcements_changed') this.inbox.refresh();
      else if (type === 'announcements') this.inbox.receive(e.message as AnnouncementResponse);
      else this.presence.receive(e.message as PresenceMessage, this.now());
    } else if (e.type === 'requestFailed') this.inbox.fail(String(e.request ?? ''));
    else if (e.type === 'connection') {
      if (e.status === 1 && !this.state.ready) this.connected();
      else if (e.status === 6) {
        this.end(''); this.state.remote = this.connectingId; this.state.notice = tr("访问密码错误，请重新输入"); this.state.modal = 'password';
      } else if ((e.status ?? 0) >= 3) {
        const reasons = ['', '', '', tr("连接已断开"), tr("连接失败"), tr("远端已关闭连接"), tr("访问密码错误"), tr("找不到该设备 ID"), tr("远端当前不可用")];
        this.end(reasons[e.status ?? 0] ?? tr("连接已结束"));
      }
    } else if (e.type === 'ended') this.end(e.reason ?? '');
    else if (e.type === 'data' && e.message) this.data(e.message as ControlData);
    else if (e.type === 'frame') {
      this.state.hasFrame = true;
      this.state.width = e.width ?? 16; this.state.height = e.height ?? 9;
      this.network.frame(e.frame ?? 0, e.latency ?? -1, this.state.width, this.state.height, this.now(), e.latencyCount ?? -1);
      this.platform.videoSize(this.state.width, this.state.height);
      if (this.previewEligible && this.historySaved && !this.previewSaved && !this.previewPending && this.previewAttempts < 3) {
        this.previewAttempts++; this.previewPending = true; this.send({ op: 'preview' });
      }
    } else if (e.type === 'network' && e.report) this.network.receive(e.report, this.now());
    else if (e.type === 'clipboard') this.state.remoteClipboard = e.text ?? '';
    else if (e.type === 'feedback') this.toast(e.text ?? '');
    else if (e.type === 'file') {
      if (e.sending && ((e.progress ?? 0) < 0 || (e.progress ?? 0) >= 1)) this.sendingFile = false;
      this.state.transfer = (e.progress ?? 0) < 0 ? tr("{0} 传输失败", [String(e.name)]) : `${e.sending ? '发送' : '接收'} ${e.name} · ${(e.progress ?? 0) >= 1 ? '已完成' : Math.floor((e.progress ?? 0) * 100) + '%'}`;
      if (e.path) { this.state.receivedPath = e.path; this.state.receivedName = e.name ?? 'received-file'; }
    } else if (e.type === 'privacyFailed') { this.privacyScreen.failed(Number(e.request)); this.syncPrivacy(); }
    else if (e.type === 'resolutionFailed') { this.resolutions.expire(Number(e.request)); this.syncResolutions(); }
    else if (e.type === 'settingsFailed') { this.video.expire(Number(e.request)); this.syncVideo(); }
    else if (e.type === 'preview') {
      this.previewPending = false;
      if (e.ok && e.path && this.previewEligible && this.state.previews) {
        this.previewSaved = true; this.store.savePreview(this.scope(), this.connectingId, e.path).then(() => this.history()).catch(() => this.previewSaved = false);
      } else if (e.path) this.store.discardRaw(e.path);
    } else if (e.type === 'version') {
      if (e.request === this.localVersionRequest && this.foreground && this.state.consent) {
        if (e.ok) { this.state.update = e.latest ?? ''; this.state.updateStatus = this.state.update ? tr("发现新版本 {0}", [String(this.state.update)]) : tr("已是最新版本"); }
        else this.state.updateStatus = tr("暂无鸿蒙版本信息，请稍后重试");
      } else if (e.request === this.remoteVersionRequest && e.ok && e.latest && this.foreground && this.state.ready) {
        this.state.notice = tr("被控端版本过低，请升级到最新版本。"); this.state.modal = 'upgrade';
      }
    } else if (e.type === 'error') {
      this.state.signalText = e.text ?? tr("连接失败"); if (this.state.connecting) { this.end(e.text ?? tr("连接失败")); this.stopSignal(); }
      else if (!this.state.ready) { const text = e.text ?? tr("操作失败"); this.stopSignal(); this.state.signalText = text; } else this.toast(e.text ?? tr("操作失败"));
    }
  }
  private configureInbox(): void {
    if (this.state.identity) this.inbox.configure(`${this.scope()}:${this.state.identity}`);
    this.inbox.connect(this.state.signal === 1 && this.foreground && this.state.consent);
  }
  private connected(): void {
    this.state.ready = true; this.state.connecting = false; this.state.modal = ''; this.state.page = 'session';
    const device: RecentDevice = { id: this.connectingId, name: this.state.hostName || this.connectingId,
      platform: this.state.platform, remember: this.state.remember, preview: '' };
    const password = this.pendingPassword; this.pendingPassword = '';
    const scope = this.scope(), connection = this.connection;
    this.store.saveRecent(scope, device, password).then(() => {
      if (connection === this.connection && scope === this.scope()) {
        this.historySaved = true;
        if (this.state.hostName) this.store.metadata(scope, device.id, this.state.hostName, this.state.platform);
        this.history();
      }
    }).catch(() => this.toast(tr("密码保存失败，本次连接仍可使用")));
    this.platform.connected(this.state.autoLandscape);
    this.syncResolutions();
    if (this.video.supported && this.video.requestId === 0) this.updateVideo(0, this.video.selection.quality);
    if (this.receivedHostInfo) this.checkRemote(this.remoteVersion);
  }
  private data(message: ControlData): void {
    if (message.type === 14 && message.video_codec) { this.network.remoteEncoder = message.video_codec.encoder; return; }
    if (message.type === 10 && message.privacy_status) { this.privacyScreen.receive(message.privacy_status, this.now()); this.syncPrivacy(); return; }
    if (message.type === 5) {
      this.serviceAvailable = message.service_status?.available === true;
      this.privacyScreen.setInteractiveStage(message.service_status?.interactive_stage ?? '');
      this.syncPrivacy(); return;
    }
    if (message.type === 12 && message.video_settings) {
      const v = message.video_settings;
      this.video.receive({ quality: v.quality, frameRate: v.frame_rate, preference: v.preference }, v.request_id, v.accepted === true);
      if (!this.video.pending && this.settingsTimeout >= 0) clearTimeout(this.settingsTimeout); this.syncVideo(); return;
    }
    if (message.type === 16 && message.display_resolution && message.display_resolutions) {
      const confirmed = this.resolutions.receive(message.display_resolution, message.display_resolutions);
      if (confirmed) this.resolutionStore.remember(this.resolutionScope, this.connectingId, confirmed);
      if (!this.resolutions.pending && this.resolutionTimeout >= 0) clearTimeout(this.resolutionTimeout);
      this.syncResolutions(); return;
    }
    if (message.host_info) {
      const info = message.host_info; this.state.platform = info.platform; this.state.hostName = info.host_name;
      this.state.displays = Array.isArray(info.displays) ? info.displays.slice(0, 8) : [];
      this.resolutions.host(info.display_resolutions === undefined ? [] : info.display_resolutions); this.syncResolutions();
      this.store.metadata(this.scope(), this.connectingId, info.host_name, info.platform);
      const supported = this.video.supported; this.video.support(info.supports_video_settings === true); this.syncVideo();
      if (this.state.ready && this.video.supported && !supported) this.updateVideo(0, this.video.selection.quality);
      this.receivedHostInfo = true; this.remoteVersion = info.app_version; this.checkRemote(info.app_version);
    }
  }
}
