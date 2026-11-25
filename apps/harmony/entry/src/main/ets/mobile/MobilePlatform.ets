import { tr } from './Localization';
import { deviceInfo } from '@kit.BasicServicesKit';
import { common, wantAgent } from '@kit.AbilityKit';
import { PiPWindow, window, display } from '@kit.ArkUI';
import { backgroundTaskManager } from '@kit.BackgroundTasksKit';
import { picker, fileIo as fs, fileUri } from '@kit.CoreFileKit';
import { http } from '@kit.NetworkKit';
import { isAnnouncementURL } from './AnnouncementText';

export class MobileLifecycle {
  static foreground: () => void = () => {};
  static background: () => void = () => {};
  static destroy: () => void = () => {};
}
export class MobilePlatform {
  private pip?: PiPWindow.PiPController;
  private pipPending: number = -1;
  private backgroundQueue: Promise<void> = Promise.resolve();
  private muted: boolean = false;
  onMute: (muted: boolean) => void = () => {};
  onVisibility: (visible: boolean) => void = () => {};
  private generation: number = 0;
  private backgroundActive: boolean = false;
  private active: boolean = false;
  private autoLandscape: boolean = false;
  private windowRevision: number = 0;
  private windowQueue: Promise<void> = Promise.resolve();
  private foreground: boolean = true;
  choosing: boolean = false;
  pipActive: boolean = false;
  private requests: http.HttpRequest[] = [];
  readonly context: common.UIAbilityContext;
  private ended: (reason: string) => void;
  private release: () => void;
  private feedback: (message: string) => void;
  private backgroundCancelled: () => void = () => {
    this.backgroundActive = false;
    if (this.active && !this.foreground && !this.pipActive && !this.choosing) this.ended(tr("系统已停止后台会话"));
  };
  constructor(context: common.UIAbilityContext, ended: (reason: string) => void, release: () => void, feedback: (message: string) => void) {
    this.context = context; this.ended = ended; this.release = release; this.feedback = feedback;
    try { backgroundTaskManager.on('continuousTaskCancel', this.backgroundCancelled); } catch {}
  }
  async connected(autoLandscape: boolean = false): Promise<void> {
    if (this.active) return;
    this.autoLandscape = autoLandscape;
    this.active = true; const generation = ++this.generation;
    this.muted = false; this.updateWindow();
    this.backgroundQueue = this.backgroundQueue.then(async () => {
    try {
      const agent = await wantAgent.getWantAgent({ wants: [{ bundleName: this.context.abilityInfo.bundleName,
        abilityName: this.context.abilityInfo.name }], operationType: wantAgent.OperationType.START_ABILITY,
        requestCode: 0, wantAgentFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG] });
      if (generation !== this.generation || !this.active) return;
      await backgroundTaskManager.startBackgroundRunning(this.context, backgroundTaskManager.BackgroundMode.DATA_TRANSFER, agent);
      if (generation !== this.generation || !this.active) { await backgroundTaskManager.stopBackgroundRunning(this.context); return; }
      this.backgroundActive = true;
    } catch { this.backgroundActive = false; }
    });
    await this.backgroundQueue;
  }
  async surface(controller: XComponentController, width: number, height: number): Promise<void> {
    if (!this.active || this.pip || this.pipPending === this.generation) return;
    const generation = this.generation;
    this.pipPending = generation;
    try {
      if (!PiPWindow.isPiPEnabled()) return;
      const pip = await PiPWindow.create({ context: this.context, componentController: controller,
        templateType: PiPWindow.PiPTemplateType.VIDEO_LIVE, contentWidth: width, contentHeight: height,
        controlGroups: [PiPWindow.VideoLiveControlGroup.MUTE_SWITCH] });
      if (generation !== this.generation || !this.active) return;
      this.pip = pip; pip.setAutoStartEnabled(!this.choosing);
      this.setMuted(this.muted);
      pip.on('controlEvent', (event: PiPWindow.ControlEventParam) => {
        if (generation === this.generation && event.controlType === PiPWindow.PiPControlType.MUTE_SWITCH)
          this.onMute(event.status !== PiPWindow.PiPControlStatus.PLAY);
      });
      pip.on('stateChange', (state: PiPWindow.PiPState, reason: string) => {
        if (generation !== this.generation) return;
        if (state === PiPWindow.PiPState.ABOUT_TO_START) this.release();
        if (state === PiPWindow.PiPState.STARTED) { this.pipActive = true; this.onVisibility(true); }
        if (state === PiPWindow.PiPState.ABOUT_TO_RESTORE) { this.foreground = true; this.onVisibility(true); }
        if (state === PiPWindow.PiPState.STOPPED) {
          this.pipActive = false;
          this.onVisibility(this.foreground);
          if (this.foreground) this.updateWindow();
          if (!this.foreground && !this.choosing && this.active) this.ended(tr("画中画已关闭"));
        }
      });
    } catch { /* A continuous data-transfer task remains the fallback. */ }
    finally { if (this.pipPending === generation) this.pipPending = -1; }
  }
  setMuted(muted: boolean): void {
    this.muted = muted;
    try { this.pip?.updatePiPControlStatus(PiPWindow.PiPControlType.MUTE_SWITCH,
      muted ? PiPWindow.PiPControlStatus.PAUSE : PiPWindow.PiPControlStatus.PLAY); } catch {}
  }
  videoSize(width: number, height: number): void { try { this.pip?.updateContentSize(width, height); } catch { /* Not supported on this device. */ } }
  async background(): Promise<void> {
    this.foreground = false; this.release(); this.cancelRequests();
    this.onVisibility(this.pipActive);
    if (!this.active) return;
    const generation = this.generation;
    if (!this.choosing && this.pip && !this.pipActive) {
      try { await this.pip.startPiP(); return; } catch { /* Fall back to the registered task. */ }
    }
    await this.backgroundQueue;
    if (generation === this.generation && !this.foreground && this.active && !this.backgroundActive && !this.pipActive && !this.choosing)
      this.ended(tr("系统未允许后台会话，连接已断开"));
  }
  resume(): void { this.foreground = true; this.dismissPiP(); this.updateWindow(); this.onVisibility(true); }
  private async dismissPiP(): Promise<void> {
    if (!this.pipActive) return;
    this.pipActive = false;
    try { await this.pip?.stopPiP(); } catch { /* Already restored or closed. */ }
  }
  dispose(): void { this.disconnect(); try { backgroundTaskManager.off('continuousTaskCancel', this.backgroundCancelled); } catch {} }
  disconnect(): void {
    this.autoLandscape = false;
    this.active = false; this.choosing = false; this.generation++; this.cancelRequests();
    const pip = this.pip; this.pip = undefined; this.pipActive = false;
    if (pip) { try { pip.setAutoStartEnabled(false); pip.stopPiP().catch(() => {}); } catch { /* Already stopped. */ } }
    this.backgroundQueue = this.backgroundQueue.then(async () => {
      if (this.backgroundActive) { this.backgroundActive = false; try { await backgroundTaskManager.stopBackgroundRunning(this.context); } catch {} }
    });
    this.updateWindow();
  }
  private updateWindow(): void {
    const revision = ++this.windowRevision;
    this.windowQueue = this.windowQueue.then(async () => {
      if (revision !== this.windowRevision) return;
      try {
        // PiP and pickers can be topmost. Session settings belong to the main window.
        const win = this.context.windowStage.getMainWindowSync();
        const session = this.active;
        const phone = deviceInfo.deviceType === 'phone';
        if (phone) {
          // The window manager applies bar visibility in fullscreen and manages it in split screen.
          const bars: Array<'status' | 'navigation'> = session ? [] : ['status', 'navigation'];
          await win.setWindowSystemBarEnable(bars).catch(() => {});
          await win.setSpecificSystemBarEnabled('navigationIndicator', !session).catch(() => {});
        }
        if (revision !== this.windowRevision) return;
        await win.setWindowKeepScreenOn(session);
        if (revision !== this.windowRevision) return;
        // Tablets and resizable windows follow the user orientation in both views.
        const rect = win.getWindowProperties().windowRect;
        const narrow = Math.min(rect.width, rect.height) / display.getDefaultDisplaySync().densityPixels < 600;
        const fullScreen = win.getWindowStatus() === window.WindowStatusType.FULL_SCREEN;
        const orientation = !session && phone && narrow && fullScreen ? window.Orientation.PORTRAIT :
          session && this.autoLandscape && phone && narrow && fullScreen ?
            window.Orientation.AUTO_ROTATION_LANDSCAPE : window.Orientation.AUTO_ROTATION;
        await win.setPreferredOrientation(orientation);
      } catch { /* Window may already be going away. */ }
    });
  }
  private setChoosing(value: boolean): void {
    this.choosing = value; this.release();
    try { this.pip?.setAutoStartEnabled(!value); } catch { /* No PiP controller. */ }
  }
  async choose(directory: string): Promise<PreparedFile | undefined> {
    if (this.choosing) return undefined;
    this.setChoosing(true); const generation = this.generation; let path = '';
    try {
      await this.dismissPiP();
      if (generation !== this.generation || !this.active) return undefined;
      const uris = await new picker.DocumentViewPicker(this.context).select({ maxSelectNumber: 1 });
      if (!uris.length || generation !== this.generation || !this.active) return undefined;
      const name = new fileUri.FileUri(uris[0]).name || 'received-file';
      path = `${directory}/outgoing/${Date.now()}-${Math.floor(Math.random() * 1000000)}`;
      const input = await fs.open(uris[0], fs.OpenMode.READ_ONLY);
      try { await fs.copyFile(input.fd, path); } finally { await fs.close(input); }
      if (generation !== this.generation || !this.active) { await fs.unlink(path); return undefined; }
      return { path: path, name: name };
    } catch { if (path) try { await fs.unlink(path); } catch {} this.feedback(tr("无法读取所选文件或存储空间不足")); return undefined; }
    finally { if (generation === this.generation) this.setChoosing(false); }
  }
  async save(path: string, name: string): Promise<void> {
    if (this.choosing || !this.active) return;
    this.setChoosing(true); const generation = this.generation;
    try {
      await this.dismissPiP();
      if (generation !== this.generation || !this.active) return;
      const uris = await new picker.DocumentViewPicker(this.context).save({ newFileNames: [name] });
      if (!uris.length || generation !== this.generation || !this.active) return;
      const output = await fs.open(uris[0], fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
      try { await fs.copyFile(path, output.fd); } finally { await fs.close(output); }
      if (generation === this.generation) this.feedback(tr("文件已保存"));
    } catch { if (generation === this.generation) this.feedback(tr("文件保存失败，请重试")); }
    finally { if (generation === this.generation) this.setChoosing(false); }
  }
  async version(): Promise<string | undefined> {
    const request = http.createHttp(); this.requests.push(request);
    try {
      const response = await request.request('https://version.crossdesk.cn/version.json', {
        method: http.RequestMethod.GET, connectTimeout: 15000, readTimeout: 15000,
        expectDataType: http.HttpDataType.STRING, usingCache: false, maxLimit: 256 * 1024
      });
      if (response.responseCode !== 200 || typeof response.result !== 'string' || response.result.length > 256 * 1024) return undefined;
      return response.result;
    } catch { return undefined; }
    finally { this.requests = this.requests.filter((r: http.HttpRequest) => r !== request); request.destroy(); }
  }
  cancelRequests(): void { for (const request of this.requests) request.destroy(); this.requests = []; }
  openLink(url: string): void {
    if (!isAnnouncementURL(url)) return;
    this.context.startAbility({ action: 'ohos.want.action.viewData', uri: url }).catch(() => this.feedback(tr("无法打开网页")));
  }
}
export interface PreparedFile { path: string; name: string; }
