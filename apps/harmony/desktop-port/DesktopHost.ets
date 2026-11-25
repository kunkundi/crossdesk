import { i18n } from '@kit.LocalizationKit';
import native from 'libslint_desktop.so';
import { TextInputHost } from './TextInputHost';
import { TrayHost } from './TrayHost';
import { FileHost } from './FileHost';
import { DisplayHost, DesktopDisplay } from './DisplayHost';
import { DesktopAccess } from './DesktopAccess';
import { AutostartHost, AutostartEvent } from './AutostartHost';
import { desktopPermissions, hostPermissions } from './DesktopCapabilities';
import { pointer } from '@kit.InputKit';
import { common, abilityAccessCtrl } from '@kit.AbilityKit';
import { display, window } from '@kit.ArkUI';
import { pasteboard } from '@kit.BasicServicesKit';
import { certificateManager } from '@kit.DeviceCertificateKit';

export interface DesktopEvent {
  op: string;
  displays?: DesktopDisplay[];
  decor?: number;
  noFrame?: boolean;
  topmost?: boolean;
  cursor?: number;
  anchor?: number;
  inputType?: number;
  commit?: boolean;
  id?: number;
  request?: number;
  ok?: boolean;
  controlDevice?: boolean;
  screenRecording?: boolean;
  text?: string;
  message?: string;
  url?: string;
  path?: string;
  title?: string;
  show?: string;
  settings?: string;
  exit?: string;
  visible?: boolean;
  width?: number;
  height?: number;
  minWidth?: number;
  minHeight?: number;
  maxWidth?: number;
  maxHeight?: number;
  x?: number;
  y?: number;
  dx?: number;
  dy?: number;
  fullscreen?: boolean;
  maximized?: boolean;
  minimized?: boolean;
  surface?: string;
  density?: number;
  active?: boolean;
  kind?: number;
  button?: number;
  code?: number;
  down?: boolean;
  control?: boolean;
  shift?: boolean;
  unicode?: number;
  capsLock?: boolean;
  numLock?: boolean;
  imeSession?: number;
  stateRevision?: number;
}

// Window/OS service host only. The desktop C++ application owns every product
// action and keeps running when another window gains focus or is minimized.
export class DesktopHost {
  private static stage: window.WindowStage;
  private static context: common.UIAbilityContext;
  private static windows: Map<number, window.Window> = new Map();
  private static queues: Map<number, Promise<void>> = new Map();
  private static clipboardQueue: Promise<void> = Promise.resolve();
  private static clipboardRevision: number = 0;
  private static clipboardCacheRevision: number = -1;
  private static clipboardCache: string = '';
  private static clipboardObserved: boolean = false;
  private static clipboardChanged: () => void = () => {
    if (!DesktopHost.started || DesktopHost.terminating) return;
    ++DesktopHost.clipboardRevision;
    DesktopHost.clipboardCache = '';
    DesktopHost.send({ op: 'clipboardChanged' });
  };
  private static properties: Map<number, DesktopEvent> = new Map();
  private static timer: number = -1;
  private static cursorSupported: boolean = true;
  private static cursorHidden: boolean = false;
  private static trayRemoval: Promise<void> = Promise.resolve();
  private static started: boolean = false;
  private static terminating: boolean = false;
  private static activeWindow: number = 1;
  private static mainShown: boolean = false;
  private static stateRevisions: Map<number, number> = new Map();

  static send(event: DesktopEvent): void { native.send(JSON.stringify(event)); }

  static requestExit(): boolean {
    if (!DesktopHost.started || DesktopHost.terminating) return false;
    DesktopHost.send({ op: 'tray', text: 'exit' });
    return true;
  }

  static restore(): void {
    const target = DesktopHost.windows.get(1);
    if (target === undefined) return;
    DesktopHost.send({ op: 'restore', id: 1 });
  }

  static foreground(): void {
    if (!DesktopHost.started || DesktopHost.terminating) return;
    AutostartHost.refresh();
  }

  static start(stage: window.WindowStage, context: common.UIAbilityContext, loginLaunch: boolean = false): void {
    DesktopHost.stage = stage; DesktopHost.context = context;
    const screen = display.getDefaultDisplaySync();
    const storage = new LocalStorage();
    storage.setOrCreate('slintWindowId', 1);
    DesktopHost.windows.set(1, stage.getMainWindowSync());
    stage.loadContent('pages/Index', storage).then(async () => {
      await FileHost.prepare(context);
      const screens = await DisplayHost.snapshot();
      DesktopHost.observe(1, stage.getMainWindowSync());
      // Publish actual grants before native auto-connect can start capture.
      DesktopAccess.start(context, desktopPermissions, (event: DesktopEvent) => {
        if (event.op === 'clipboardChanged') DesktopHost.clipboardChanged();
        else DesktopHost.send(event);
      }, hostPermissions);
      native.start(JSON.stringify({ loginLaunch, language: i18n.System.getSystemLanguage(), filesDir: context.filesDir, cacheDir: context.cacheDir,
        width: screen.width, height: screen.height, density: screen.densityPixels,
        displayId: screen.id, displays: screens, name: 'Harmony PC',
        certificates: certificateManager.getCertificateStorePath({ certType: certificateManager.CertType.CA_CERT_SYSTEM }) }));
      DesktopHost.started = true;
      AutostartHost.start((event: AutostartEvent) => native.send(JSON.stringify(event)));
      try {
        pasteboard.getSystemPasteboard().on('update', DesktopHost.clipboardChanged);
        DesktopHost.clipboardObserved = true;
      } catch (error) { console.warn(`Slint clipboard observation: ${error.message}`); }
      DisplayHost.observe((screens: DesktopDisplay[]) => {
        DesktopHost.send({ op: 'displays', displays: screens });
        DesktopHost.windows.forEach((target: window.Window, id: number) => DesktopHost.metrics(id, target));
      });
      DesktopHost.timer = setInterval(() => DesktopHost.poll(), 8);
    }).catch((error: Error) => console.error(`Slint startup: ${error.message}`));
  }

  private static observe(id: number, target: window.Window): void {
    const current = (): boolean => DesktopHost.windows.get(id) === target && !DesktopHost.terminating;
    const watch = (subscribe: () => void): void => {
      try { subscribe(); }
      catch (error) { console.warn(`Slint window observation: ${error.message}`); }
    };
    watch(() => target.on('windowEvent', (event: window.WindowEventType) => {
      if (!current()) return;
      if (event === window.WindowEventType.WINDOW_ACTIVE) {
        DesktopHost.activeWindow = id;
        DesktopAccess.refresh();
        DesktopHost.foreground();
      }
      if (event === window.WindowEventType.WINDOW_ACTIVE || event === window.WindowEventType.WINDOW_INACTIVE)
        DesktopHost.send({ op: 'active', id, active: event === window.WindowEventType.WINDOW_ACTIVE });
    }));
    watch(() => target.on('windowRectChange', () => { if (current()) DesktopHost.metrics(id, target); }));
    watch(() => target.on('displayIdChange', () => { if (current()) DesktopHost.metrics(id, target); }));
    DesktopHost.metrics(id, target);
    watch(() => target.on('windowStatusDidChange', (status: window.WindowStatusType) => {
      if (!current()) return;
      const revision = (DesktopHost.stateRevisions.get(id) ?? 0) + 1;
      DesktopHost.stateRevisions.set(id, revision);
      // Minimization does not change the underlying fullscreen/maximized mode.
      const state: DesktopEvent = status === window.WindowStatusType.MINIMIZE ?
        { op: 'state', id, stateRevision: revision, minimized: true } :
        { op: 'state', id, stateRevision: revision, fullscreen: status === window.WindowStatusType.FULL_SCREEN,
          maximized: status === window.WindowStatusType.MAXIMIZE, minimized: false };
      const saved = DesktopHost.properties.get(id) ?? { op: 'properties', id };
      saved.fullscreen = state.fullscreen ?? saved.fullscreen;
      saved.maximized = state.maximized ?? saved.maximized;
      saved.minimized = state.minimized;
      saved.stateRevision = revision;
      DesktopHost.properties.set(id, saved);
      DesktopHost.send(state);
    }));
    watch(() => target.on('windowWillClose', async () => {
      if (!current()) return false;
      DesktopHost.send({ op: 'close', id });
      return true; // The shared desktop callback decides hide/disconnect/exit.
    }));
  }

  private static forget(id: number, target: window.Window): void {
    DesktopHost.windows.delete(id);
    TextInputHost.release(id);
    // Some SDKs do not expose all listeners. Cleanup must still reach destroy.
    try { target.off('windowEvent'); } catch (_) {}
    try { target.off('windowRectChange'); } catch (_) {}
    try { target.off('displayIdChange'); } catch (_) {}
    try { target.off('windowStatusDidChange'); } catch (_) {}
    try { target.off('windowWillClose'); } catch (_) {}
  }

  private static metrics(id: number, target: window.Window): void {
    try {
      const point = DisplayHost.global(target);
      DesktopHost.send({ op: 'metrics', id, x: point.x, y: point.y,
        density: DisplayHost.screen(target).densityPixels, decor: DesktopHost.decorHeight(target) });
    } catch (error) { console.warn(`Slint window metrics: ${error.message}`); }
  }

  private static poll(): void {
    const events: DesktopEvent[] = JSON.parse(native.poll()) as DesktopEvent[];
    events.forEach((event: DesktopEvent) => {
      if (event.request !== undefined) {
        const service = (): Promise<void> => DesktopHost.service(event).catch((error: Error) =>
          DesktopHost.send({ op: 'reply', request: event.request, ok: false, message: error.message }));
        // Slint edits and the shared clipboard monitor run on different native
        // threads. The OS permits only one copy/paste operation at a time.
        if (event.op.startsWith('clipboard')) DesktopHost.clipboardQueue = DesktopHost.clipboardQueue.then(service);
        else service();
      } else if (event.id !== undefined) {
        const id = event.id;
        const prior = DesktopHost.queues.get(id) ?? Promise.resolve();
        const next = prior.then(() => DesktopHost.windowCommand(event)).catch((error: Error) => console.error(`Slint window ${id}: ${error.message}`));
        DesktopHost.queues.set(id, next);
        next.then(() => { if (DesktopHost.queues.get(id) === next) DesktopHost.queues.delete(id); });
      } else if (event.op === 'exit') {
        DesktopHost.terminating = true;
        DesktopAccess.stop();
        AutostartHost.stop();
        if (DesktopHost.clipboardObserved) {
          try { pasteboard.getSystemPasteboard().off('update', DesktopHost.clipboardChanged); }
          catch (error) { console.warn(`Slint clipboard cleanup: ${error.message}`); }
          DesktopHost.clipboardObserved = false;
        }
        clearInterval(DesktopHost.timer);
        DesktopHost.trayRemoval.finally(() => DesktopHost.context.terminateSelf());
      } else if (event.op === 'keyboardPermission') {
        DesktopAccess.requestKeyboard();
      } else if (event.op === 'autostartRefresh') {
        AutostartHost.refresh();
      } else if (event.op === 'trayRemove') {
        DesktopHost.trayRemoval = TrayHost.remove(DesktopHost.context)
          .catch((error: Error) => console.warn(`Slint tray removal: ${error.message}`));
      } else if (event.op === 'error') {
        console.error(`Slint desktop: ${event.message}`);
      }
    });
  }

  private static async windowCommand(event: DesktopEvent): Promise<void> {
    const id = event.id ?? 0;
    if (event.op === 'properties') {
      // Ignore mode commands based on a state older than the latest native
      // transition. Keep independent title/size/stacking updates applicable.
      if ((event.stateRevision ?? 0) < (DesktopHost.stateRevisions.get(id) ?? 0)) {
        event.fullscreen = undefined; event.maximized = undefined; event.minimized = undefined;
      }
      const prior = DesktopHost.properties.get(id);
      DesktopHost.properties.set(id, { op: 'properties', id, title: event.title, noFrame: event.noFrame, topmost: event.topmost,
        minWidth: event.minWidth, minHeight: event.minHeight, maxWidth: event.maxWidth, maxHeight: event.maxHeight,
        fullscreen: event.fullscreen ?? prior?.fullscreen,
        maximized: event.maximized ?? prior?.maximized, minimized: event.minimized ?? prior?.minimized,
        stateRevision: DesktopHost.stateRevisions.get(id) ?? 0 });
    }
    if (event.op === 'destroy') {
      DesktopHost.properties.delete(id);
      DesktopHost.stateRevisions.delete(id);
    }
    let target = DesktopHost.windows.get(id);
    if (event.op === 'visible' && event.visible && target === undefined) {
      target = await DesktopHost.stage.getMainWindowSync().createSubWindowWithOptions(`crossdesk-slint-${id}`,
        { title: DesktopHost.properties.get(id)?.title ?? 'CrossDesk', decorEnabled: !DesktopHost.properties.get(id)?.noFrame, isModal: false,
          zLevelAboveParentLoosened: !DesktopHost.properties.get(id)?.topmost, maximizeSupported: true });
      DesktopHost.windows.set(id, target);
      try {
        const storage = new LocalStorage();
        storage.setOrCreate('slintWindowId', id);
        await target.loadContent('pages/Index', storage);
        await DesktopHost.resizeContent(target, event.width ?? 1216, event.height ?? 903);
        if (event.x !== undefined && event.y !== undefined) await DisplayHost.move(target, event.x, event.y);
        const saved = DesktopHost.properties.get(id);
        if (saved !== undefined) await DesktopHost.applyProperties(target, saved);
        // Publish geometry only after restoring it; the default placement must
        // not overwrite the shared desktop's saved position during recreation.
        DesktopHost.observe(id, target);
      } catch (error) {
        DesktopHost.forget(id, target);
        try { await target.destroyWindow(); } catch (_) {}
        throw new Error(error.message);
      }
    }
    if (target === undefined) return;
    if (event.op === 'ime') { TextInputHost.request(event, target, (reply: DesktopEvent) => DesktopHost.send(reply)); return; }
    if (event.op === 'cursor') {
      if (!DesktopHost.cursorSupported) return;
      // Slint 1.17.1 MouseCursor order is pinned by os-services.patch.
      const styles: pointer.PointerStyle[] = [pointer.PointerStyle.DEFAULT, pointer.PointerStyle.DEFAULT,
        pointer.PointerStyle.HELP, pointer.PointerStyle.HAND_POINTING, pointer.PointerStyle.RUNNING,
        pointer.PointerStyle.LOADING, pointer.PointerStyle.CROSS, pointer.PointerStyle.TEXT_CURSOR,
        pointer.PointerStyle.CURSOR_COPY, pointer.PointerStyle.CURSOR_COPY, pointer.PointerStyle.MOVE,
        pointer.PointerStyle.CURSOR_FORBID, pointer.PointerStyle.CURSOR_FORBID, pointer.PointerStyle.HAND_OPEN,
        pointer.PointerStyle.HAND_GRABBING, pointer.PointerStyle.WEST_EAST, pointer.PointerStyle.NORTH_SOUTH,
        pointer.PointerStyle.NORTH, pointer.PointerStyle.EAST, pointer.PointerStyle.SOUTH, pointer.PointerStyle.WEST,
        pointer.PointerStyle.NORTH_EAST, pointer.PointerStyle.NORTH_WEST, pointer.PointerStyle.SOUTH_EAST,
        pointer.PointerStyle.SOUTH_WEST, pointer.PointerStyle.WEST_EAST, pointer.PointerStyle.NORTH_SOUTH,
        pointer.PointerStyle.NORTH_EAST_SOUTH_WEST, pointer.PointerStyle.NORTH_WEST_SOUTH_EAST];
      try {
        await pointer.setPointerStyle(target.getWindowProperties().id, styles[event.kind ?? 0] ?? pointer.PointerStyle.DEFAULT);
        if (DesktopHost.cursorHidden !== (event.kind === 1)) {
          await pointer.setPointerVisible(event.kind !== 1);
          DesktopHost.cursorHidden = event.kind === 1;
        }
      } catch (error) {
        DesktopHost.cursorSupported = false;
        console.warn(`Slint cursor service unavailable: ${error.message}`);
      }
      return;
    }
    if (event.op === 'visible') {
      if (event.visible) {
        if (id === 1 && !DesktopHost.mainShown) {
          const status = target.getWindowStatus();
          if (status !== window.WindowStatusType.MAXIMIZE && status !== window.WindowStatusType.FULL_SCREEN)
            await DesktopHost.resizeContent(target, event.width ?? 1216, event.height ?? 903);
          DesktopHost.mainShown = true;
        }
        await DesktopHost.show(id, target);
        if (id === 1) DesktopAccess.requestHost(target.getUIContext());
      } else if (id === 1) await target.minimize();
      else { DesktopHost.forget(id, target); await target.destroyWindow(); }
    } else if (event.op === 'resize') {
      await DesktopHost.resizeContent(target, event.width ?? 1216, event.height ?? 903);
    } else if (event.op === 'move') {
      await DisplayHost.move(target, event.x ?? 0, event.y ?? 0);
    } else if (event.op === 'properties') await DesktopHost.applyProperties(target, event);
    else if (event.op === 'destroy' && id !== 1) {
      DesktopHost.forget(id, target); DesktopHost.properties.delete(id);
      await target.destroyWindow();
    }
  }

  private static decorHeight(target: window.Window): number {
    try { return target.getWindowDecorVisible() ? Math.round(target.getWindowDecorHeight() * DisplayHost.screen(target).densityPixels) : 0; }
    catch { return 0; }
  }

  private static async resizeContent(target: window.Window, width: number, height: number): Promise<void> {
    await target.resize(Math.max(1, Math.round(width)), Math.max(1, Math.round(height) + DesktopHost.decorHeight(target)));
  }

  private static async show(id: number, target: window.Window): Promise<void> {
    // restore() restores a minimized main window on 2in1; showWindow() only
    // restores child windows. Avoid resizing an already-visible/maximized one.
    if (id === 1 && target.getWindowStatus() === window.WindowStatusType.MINIMIZE) await target.restore();
    else await target.showWindow();
  }

  private static async applyProperties(target: window.Window, event: DesktopEvent): Promise<void> {
    if (event.noFrame !== undefined && target.getWindowDecorVisible() === event.noFrame)
      target.setWindowDecorVisible(!event.noFrame);
    // A decor-free subwindow has no native title component. Harmony rejects
    // setWindowTitle for it, which must not prevent the Slint status bar from
    // receiving its size/stacking properties or becoming visible.
    if (event.title !== undefined && target.getWindowDecorVisible()) {
      try { await target.setWindowTitle(event.title); }
      catch (error) { console.warn(`Slint window title: ${error.message}`); }
    }
    if (event.id !== 1 && event.topmost !== undefined) {
      try { await target.setSubWindowZLevel(event.topmost ? 10000 : 0); }
      catch (error) { console.warn(`Slint window stacking: ${error.message}`); }
    }
    const decor = DesktopHost.decorHeight(target);
    const limits: window.WindowLimits = { minWidth: Math.max(1, Math.round(event.minWidth ?? 1)),
      minHeight: Math.max(1, Math.round(event.minHeight ?? 1) + decor) };
    if (event.maxWidth !== undefined) limits.maxWidth = Math.max(limits.minWidth ?? 1, Math.round(event.maxWidth));
    if (event.maxHeight !== undefined) limits.maxHeight = Math.max(limits.minHeight ?? 1, Math.round(event.maxHeight) + decor);
    await target.setWindowLimits(limits);
    if (event.fullscreen === undefined && event.maximized === undefined && event.minimized === undefined) return;
    if ((event.stateRevision ?? 0) < (DesktopHost.stateRevisions.get(event.id ?? 0) ?? 0)) return;
    const saved = DesktopHost.properties.get(event.id ?? 0);
    const minimized = event.minimized ?? saved?.minimized ?? false;
    const fullscreen = event.fullscreen ?? saved?.fullscreen ?? false;
    const maximized = event.maximized ?? saved?.maximized ?? false;
    let status = target.getWindowStatus();
    if (minimized) {
      if (status !== window.WindowStatusType.MINIMIZE) await target.minimize();
      return;
    }
    if (status === window.WindowStatusType.MINIMIZE) {
      await DesktopHost.show(event.id ?? 0, target);
      status = target.getWindowStatus();
    }
    if (fullscreen) {
      if (status !== window.WindowStatusType.FULL_SCREEN) await target.maximize(window.MaximizePresentation.ENTER_IMMERSIVE);
    } else if (maximized) {
      if (status !== window.WindowStatusType.MAXIMIZE) await target.maximize(window.MaximizePresentation.EXIT_IMMERSIVE);
    } else if (status === window.WindowStatusType.MAXIMIZE || status === window.WindowStatusType.FULL_SCREEN) await target.recover();
  }

  private static async service(event: DesktopEvent): Promise<void> {
    const response: DesktopEvent = { op: 'reply', request: event.request, ok: true };
    const board = pasteboard.getSystemPasteboard();
    // A Ctrl+V gesture grants one read. Background synchronization must never
    // consume that grant before the editor does. It requires READ_PASTEBOARD;
    // the explicit paste uses the system's normal gesture authorization.
    if ((event.op === 'clipboardRead' || event.op === 'clipboardHas') &&
      abilityAccessCtrl.createAtManager().checkAccessTokenSync(DesktopHost.context.applicationInfo.accessTokenId,
        'ohos.permission.READ_PASTEBOARD') !== abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED) {
      response.ok = false;
      DesktopHost.clipboardCache = '';
      DesktopHost.clipboardCacheRevision = -1;
      DesktopAccess.requestClipboard();
      response.message = 'Clipboard synchronization requires READ_PASTEBOARD permission';
    } else if (event.op === 'clipboardRead' || event.op === 'clipboardPaste') {
      const revision = DesktopHost.clipboardRevision;
      const explicitPaste = event.op === 'clipboardPaste';
      if (explicitPaste || !DesktopHost.clipboardObserved || DesktopHost.clipboardCacheRevision !== revision) {
        const text = board.hasDataType(pasteboard.MIMETYPE_TEXT_PLAIN) ? (await board.getData()).getPrimaryText() : '';
        if (!explicitPaste && abilityAccessCtrl.createAtManager().checkAccessTokenSync(
          DesktopHost.context.applicationInfo.accessTokenId, 'ohos.permission.READ_PASTEBOARD') !==
          abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED) {
          DesktopHost.clipboardCache = ''; DesktopHost.clipboardCacheRevision = -1;
          DesktopHost.send({ op: 'reply', request: event.request, ok: false, message: 'Clipboard permission revoked' });
          return;
        }
        response.text = text;
        // A change during getData must be read again. Explicit paste never
        // populates the background cache from a one-use gesture grant.
        if (!explicitPaste && revision === DesktopHost.clipboardRevision) {
          DesktopHost.clipboardCache = text; DesktopHost.clipboardCacheRevision = revision;
        }
      } else response.text = DesktopHost.clipboardCache;
    } else if (event.op === 'clipboardHas') response.ok = board.hasDataType(pasteboard.MIMETYPE_TEXT_PLAIN);
    else if (event.op === 'clipboardWrite') {
      await board.setData(pasteboard.createData(pasteboard.MIMETYPE_TEXT_PLAIN, event.text ?? ''));
      DesktopHost.clipboardChanged();
    } else if (event.op === 'keepScreenOn') {
      await DesktopHost.stage.getMainWindowSync().setWindowKeepScreenOn(event.active === true);
    } else if (event.op === 'openUrl') await DesktopHost.context.openLink(event.url ?? '');
    else if (event.op === 'trayCreate') response.ok = await TrayHost.create(DesktopHost.context, event,
      (reply: DesktopEvent) => DesktopHost.send(reply));
    else if (event.op === 'selectFile' || event.op === 'selectFolder') {
      const owner = DesktopHost.windows.get(DesktopHost.activeWindow) ?? DesktopHost.stage.getMainWindowSync();
      response.path = await FileHost.select(DesktopHost.context, event.op === 'selectFolder', owner);
    } else response.ok = false;
    DesktopHost.send(response);
  }
}
