import { tr } from './Localization';
import { ResolutionMode, resolutionLabel } from './RemoteDisplayResolutions';
import { MobileController, MobileState } from './MobileController';
import { RemoteViewport, MouseInput, TouchInput, TouchPoint, clamp, NetworkStatistics, Traffic, asciiKey } from './ControllerModels';
import { MobileTheme, MobileSegment, MobileIcon, MobileIconButton } from './MobileWidgets';

interface KeyRow { keys: string[]; }
@Component
export struct MobileRemoteView {
  @ObjectLink @Watch('stateChanged') state: MobileState;
  controller?: MobileController;
  @State private viewWidth: number = 1;
  @State private viewHeight: number = 1;
  @State private videoWidth: number = 1;
  @State private videoHeight: number = 1;
  @State private videoLeft: number = 0;
  @State private videoTop: number = 0;
  @State private zoom: number = 1;
  @State private cursorX: number = 0;
  @State private cursorY: number = 0;
  @State private orbX: number = 12;
  @State private orbY: number = 100;
  @State private menu: string = '';
  @State private menuHeight: number = 390;
  @State private keyboardHeight: number = 220;
  private orbPositioned: boolean = false;
  @State private keyboard: boolean = false;
  @State private computerKeyboard: boolean = false;
  @State private shift: boolean = false;
  @State private modifiers: number[] = [];
  @State private keyboardX: number = 8;
  @State private keyboardY: number = 150;
  @State private mouseVisible: boolean = false;
  @State private leftHeld: boolean = false;
  @State private rightHeld: boolean = false;
  @State private wheelOffset: number = 0;
  private surface: XComponentController = new XComponentController();
  private viewport: RemoteViewport = new RemoteViewport();
  private mouse: MouseInput = new MouseInput((x: number, y: number, flag: number, scroll: number) => {
    this.controller?.pointer(x, y, flag, scroll); this.updateCursor();
  });
  private touches: TouchInput = new TouchInput(this.viewport, this.mouse);
  private longTimer: number = -1;
  private lastReset: number = -1;
  private lastWidth: number = 0;
  private lastHeight: number = 0;
  private lastRelative: boolean = true;
  private orbStartX: number = 0;
  private orbStartY: number = 0;
  private originX: number = 0;
  private originY: number = 0;
  private orbMoved: boolean = false;
  private barX: number = 0;
  private barY: number = 0;
  private wheelY: number = 0;
  private wheelLast: number = 0;
  private keyboardOriginX: number = 0;
  private keyboardOriginY: number = 0;
  aboutToAppear(): void { if (this.controller) this.controller.releaseInput = () => this.release(); this.stateChanged(); }
  aboutToDisappear(): void { this.release(); if (this.controller) this.controller.releaseInput = () => {}; this.controller?.surface('0'); }
  private stateChanged(): void {
    if (this.lastReset !== this.state.viewportReset || this.lastRelative !== this.state.relative) {
      this.release();
      if (this.lastReset !== this.state.viewportReset) { this.viewport.reset(); this.applyViewport(); }
      this.lastReset = this.state.viewportReset; this.lastRelative = this.state.relative;
    }
    this.touches.relative = this.state.relative;
    if (this.lastWidth !== this.state.width || this.lastHeight !== this.state.height) {
      this.lastWidth = this.state.width; this.lastHeight = this.state.height; this.fit();
    }
  }
  private release(): void {
    this.touches.cancel(); this.leftHeld = this.rightHeld = false; this.modifiers = []; this.wheelOffset = 0;
    if (this.longTimer >= 0) clearTimeout(this.longTimer); this.longTimer = -1;
  }
  private fit(): void { this.viewport.fit(this.viewWidth, this.viewHeight, this.state.width, this.state.height); this.applyViewport(); }
  private applyViewport(): void {
    this.videoWidth = this.viewport.baseWidth; this.videoHeight = this.viewport.baseHeight; this.zoom = this.viewport.scale;
    this.videoLeft = this.viewport.baseLeft + this.viewport.offsetX; this.videoTop = this.viewport.baseTop + this.viewport.offsetY; this.updateCursor();
  }
  private updateCursor(): void {
    this.cursorX = this.viewport.left() + this.mouse.x * this.viewport.width(); this.cursorY = this.viewport.top() + this.mouse.y * this.viewport.height();
  }
  private touch(event: TouchEvent): void {
    if (event.source === SourceType.Mouse) return;
    const kind = event.type === TouchType.Down ? 'down' : event.type === TouchType.Move ? 'move' : event.type === TouchType.Up ? 'up' : 'cancel';
    if (kind === 'down') {
      this.touches.begin(); if (this.longTimer >= 0) clearTimeout(this.longTimer);
      if (event.touches.length === 1) this.longTimer = setTimeout(() => this.touches.hold(), 500);
    }
    if (kind === 'up' || kind === 'cancel' || event.touches.length > 1) { if (this.longTimer >= 0) clearTimeout(this.longTimer); this.longTimer = -1; }
    this.touches.touch(event.touches.map((t: TouchObject): TouchPoint => ({ x: t.x, y: t.y })), kind); this.applyViewport();
  }
  private physicalMouse(event: MouseEvent): void {
    if (!this.viewport.contains(event.x, event.y) && event.action !== MouseAction.Release) return;
    this.mouse.move(this.viewport.x(event.x), this.viewport.y(event.y));
    const button = event.button === MouseButton.Left ? 0 : event.button === MouseButton.Right ? 1 : event.button === MouseButton.Middle ? 2 : -1;
    if (button >= 0 && (event.action === MouseAction.Press || event.action === MouseAction.Release))
      this.controller?.pointer(this.mouse.x, this.mouse.y, (event.action === MouseAction.Press ? 1 : 2) + button * 2, 0);
  }
  private orbTouch(event: TouchEvent): void {
    const p = event.changedTouches[0]; if (!p) return;
    if (event.type === TouchType.Down) { this.release(); this.orbStartX = p.screenX; this.orbStartY = p.screenY; this.originX = this.orbX; this.originY = this.orbY; this.orbMoved = false; }
    if (event.type === TouchType.Move) {
      const dx = p.screenX - this.orbStartX, dy = p.screenY - this.orbStartY;
      if (Math.hypot(dx, dy) > 4) { this.orbMoved = true; this.menu = ''; }
      if (this.orbMoved) { this.orbX = clamp(this.originX + dx, 8, this.viewWidth - 62); this.orbY = clamp(this.originY + dy, 8, this.viewHeight - 62); }
    }
    if (event.type === TouchType.Up && !this.orbMoved) this.menu = this.menu ? '' : 'controls';
  }
  private barTouch(event: TouchEvent, button: number): void {
    const p = event.changedTouches[0]; if (!p) return;
    if (event.type === TouchType.Down) {
      this.barX = p.screenX; this.barY = this.wheelY = this.wheelLast = p.screenY;
      if (button !== 1) this.mouse.button(button === 0, true);
    } else if (event.type === TouchType.Move) {
      if (button === 1) {
        const delta = p.screenY - this.wheelY; this.wheelOffset = ((p.screenY - this.wheelLast) % 8 + 8) % 8;
        if (Math.abs(delta) > 8) { this.mouse.scroll(delta); this.wheelY = p.screenY; }
      } else this.mouse.drag(p.screenX - this.barX, p.screenY - this.barY, this.viewport.width(), this.viewport.height());
      this.barX = p.screenX; this.barY = p.screenY;
    } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
      if (button !== 1) this.mouse.button(button === 0, false); this.wheelOffset = 0;
    }
    this.leftHeld = this.mouse.left; this.rightHeld = this.mouse.right;
  }
  private action(index: number): void {
    if (index === 0) { this.keyboard = !this.keyboard; this.menu = ''; this.modifiers = []; if (this.keyboard) { this.keyboardX = (this.viewWidth - this.keyboardWidth()) / 2; this.keyboardY = Math.max(8, this.viewHeight - this.keyboardHeight - 20); } }
    if (index === 1) this.controller?.setRelative(!this.state.relative);
    if (index === 2) { this.release(); this.mouseVisible = !this.mouseVisible; this.menu = ''; }
    if (index === 3) { this.state.modal = 'displays'; this.menu = ''; }
    if (index === 4) this.menu = 'video';
    if (index === 5) this.controller?.mute();
    if (index === 6) this.controller?.chooseFile();
    if (index === 7) this.menu = 'network';
    if (index === 8) this.controller?.sas();
    if (index === 9) { this.state.modal = 'clipboard'; this.menu = ''; }
    if (index === 10) this.controller?.togglePrivacy();
  }
  private labels(): string[] {
    return [tr("键盘"), this.state.relative ? tr("相对鼠标") : tr("绝对鼠标"), this.mouseVisible ? tr("收起鼠标") : tr("虚拟鼠标"), tr("显示器"), tr("画面设置"), this.state.muted ? tr("静音") : tr("声音"), tr("发送文件"), tr("网络状态"), 'Ctrl+Alt+Del', tr("剪贴板"), this.state.privacyButtonTitle];
  }
  private controlIds(): number[] {
    return [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10].filter((index: number) =>
      (index !== 8 || this.state.platform === 'windows') && (index !== 10 || this.state.privacyVisible));
  }
  private icons(): string[] { return ['keyboard', 'mouse', 'mouse', 'display', 'sliders', this.state.muted ? 'muted' : 'audio', 'folder', 'chart', 'lock', 'clipboard', this.state.privacyEnabled ? 'eye_off' : 'eye']; }
  private columns(): number { return this.viewHeight < 400 && this.viewWidth >= 400 ? 4 : 3; }
  private menuWidth(): number { return Math.max(1, Math.min(this.viewWidth - 16, this.columns() === 4 ? 380 : Math.max(280, Math.min(340, this.viewWidth - 160)))); }
  private menuX(): number { return clamp(this.orbX > this.viewWidth / 2 ? this.orbX - this.menuWidth() - 12 : this.orbX + 66, 8, this.viewWidth - this.menuWidth() - 8); }
  private mousePosition(): Position {
    let x = this.orbX >= 154 ? this.orbX - 146 : this.orbX + 60, y = this.orbY + 3;
    if (x + 140 > this.viewWidth - 8) { x = this.orbX; y = this.orbY + 60; if (y + 48 > this.viewHeight - 8) y = this.orbY - 54; }
    if (this.keyboard && x < this.keyboardX + this.keyboardWidth() && x + 140 > this.keyboardX && y < this.keyboardY + this.keyboardHeight && y + 48 > this.keyboardY) {
      y = this.keyboardY - 54; if (y < 8) y = this.keyboardY + this.keyboardHeight + 6;
    }
    return { x: clamp(x, 8, this.viewWidth - 148), y: clamp(y, 8, this.viewHeight - 56) };
  }
  private keyboardWidth(): number { return Math.max(1, Math.min(this.computerKeyboard ? 520 : 440, this.viewWidth - 16)); }
  private keyboardTitle(key: string): string {
    switch (key) {
      case 'copy': return tr("复制");
      case 'paste': return tr("粘贴");
      case 'select_all': return tr("全选");
      case 'space': return tr("空格");
      case 'computer': return tr("电脑");
      case 'clipboard': return tr("剪贴板");
      default: return this.shift && key.length === 1 ? key.toUpperCase() : key;
    }
  }
  private keyboardRows(): KeyRow[] {
    return this.computerKeyboard ? [
      { keys: ['Esc', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6'] }, { keys: ['Tab', 'Ctrl', 'Alt', 'Win', '↑', '⌫'] },
      { keys: ['copy', 'paste', 'select_all', '←', '↓', '→'] }, { keys: ['ABC', 'space', 'Enter'] }
    ] : [{ keys: ['1','2','3','4','5','6','7','8','9','0'] }, { keys: ['q','w','e','r','t','y','u','i','o','p'] },
      { keys: ['a','s','d','f','g','h','j','k','l'] }, { keys: ['⇧','z','x','c','v','b','n','m','⌫'] },
      { keys: ['computer','clipboard','space','.','Enter'] }];
  }
  private keyboardKey(value: string): void {
    if (value === '⇧') { this.shift = !this.shift; return; }
    if (value === 'computer' || value === 'ABC') { this.computerKeyboard = value === 'computer'; return; }
    if (value === 'clipboard') { this.state.modal = 'clipboard'; return; }
    const modifier = value === 'Ctrl' ? 17 : value === 'Alt' ? 18 : value === 'Win' ? 91 : 0;
    if (modifier) { this.modifiers = this.modifiers.includes(modifier) ? this.modifiers.filter((m: number) => m !== modifier) : [...this.modifiers, modifier]; return; }
    if (value === 'copy' || value === 'paste' || value === 'select_all') { this.controller?.shortcut(value === 'copy' ? 67 : value === 'paste' ? 86 : 65); return; }
    const names = ['space','⌫','Enter','Esc','Tab','↑','↓','←','→']; const codes = [32,8,13,27,9,38,40,37,39];
    let code = names.includes(value) ? codes[names.indexOf(value)] : /^F[1-6]$/.test(value) ? 111 + Number(value.substring(1)) : 0;
    if (!code && this.modifiers.length) { const key = asciiKey(this.shift ? value.toUpperCase() : value); code = key & 255; if (key & 256) this.modifiers = [...this.modifiers, 16]; }
    if (code) { this.controller?.chord(code, this.modifiers); this.modifiers = []; }
    else this.controller?.text(this.shift ? value.toUpperCase() : value);
  }
  private keyboardDrag(event: GestureEvent): void {
    this.keyboardX = clamp(this.keyboardOriginX + event.offsetX, 8, this.viewWidth - this.keyboardWidth() - 8);
    this.keyboardY = clamp(this.keyboardOriginY + event.offsetY, 8, Math.max(8, this.viewHeight - this.keyboardHeight - 8));
  }
  @Builder
  ResolutionMenu(modes: ResolutionMode[], displayId: number, displayKey: string) {
    Menu() {
      ForEach(modes, (mode: ResolutionMode) => {
        MenuItem({ content: resolutionLabel(mode) })
          .onClick(() => this.controller?.updateResolution(mode, displayId, displayKey))
      }, (mode: ResolutionMode) => resolutionLabel(mode))
    }
  }

  @Builder
  NetworkRow(title: string, traffic: Traffic | undefined) {
    Row({ space: 4 }) {
      Text(title).fontSize(11).minFontSize(8).maxFontSize(11).maxLines(1).fontWeight(title === tr("合计") ? 600 : 400).fontColor(MobileTheme.ink).width(48)
      Text(NetworkStatistics.rate(traffic?.inbound ?? -1)).fontSize(11).minFontSize(8).maxLines(1).layoutWeight(1).textAlign(TextAlign.End)
      Text(NetworkStatistics.rate(traffic?.outbound ?? -1)).fontSize(11).minFontSize(8).maxLines(1).layoutWeight(1).textAlign(TextAlign.End)
      Text(NetworkStatistics.loss(traffic?.loss ?? -1)).fontSize(11).width(46).textAlign(TextAlign.End)
    }.padding({ left: 4, right: 4 }).height(26).width('100%').borderRadius(5).backgroundColor(title === tr("合计") ? '#122E7ADB' : Color.Transparent)
  }
  @Builder
  NetworkDetail(label: string, value: string, encrypted: boolean = false) {
    Row({ space: 4 }) {
      Text(label).fontSize(12).fontColor(MobileTheme.secondary).layoutWeight(1)
      if (encrypted) { MobileIcon({ name: 'lock', iconExtent: 14, color: MobileTheme.green }) }
      Text(value).fontSize(12).fontColor(encrypted ? MobileTheme.green : MobileTheme.ink).textAlign(TextAlign.End)
    }.width('100%').constraintSize({ minHeight: 21 })
  }
  @Builder
  NetworkCodec(label: string, value: string) {
    Row({ space: 8 }) {
      Text(label).fontSize(12).fontColor(MobileTheme.secondary).maxLines(1)
      Text(value).fontSize(12).fontColor(MobileTheme.ink).textAlign(TextAlign.End).layoutWeight(1)
        .maxLines(1).textOverflow({ overflow: TextOverflow.MARQUEE })
        .marqueeOptions({ start: true, step: 0.5, loop: -1, fromStart: true })
    }.width('100%').constraintSize({ minHeight: 21 })
  }
  @Builder
  Menu() {
    Column({ space: 10 }) {
      Row({ space: 6 }) {
        if (this.menu !== 'controls') { MobileIconButton({ icon: 'back', label: tr("返回控制栏"), iconExtent: 30, iconSize: 19, action: () => this.menu = 'controls' }) }
        Row({ space: 6 }) {
          if (this.menu === 'controls') { Row() {}.width(6).height(6).borderRadius(3).backgroundColor(MobileTheme.green) }
          Text(this.menu === 'controls' ? tr("已连接") : this.menu === 'video' ? tr("画面设置") : tr("网络状态")).fontSize(14).fontWeight(600).fontColor(MobileTheme.ink)
        }.layoutWeight(1)
        MobileIconButton({ icon: 'close', label: tr("关闭控制栏"), iconExtent: 30, iconSize: 20, action: () => this.menu = '' })
      }.width('100%')
      Scroll() {
        Column({ space: 8 }) {
          if (this.menu === 'controls') {
            GridRow({ columns: this.columns(), gutter: 7 }) {
              ForEach(this.controlIds(), (index: number) => {
                GridCol() {
                  Column({ space: 4 }) {
                    MobileIcon({ name: this.icons()[index], iconExtent: 19 })
                    Text(this.labels()[index]).fontSize(10).minFontSize(8).maxFontSize(10).fontColor(MobileTheme.ink).maxLines(2).textAlign(TextAlign.Center)
                  }
                    .height(52).width('100%').justifyContent(FlexAlign.Center)
                    .backgroundColor('#F0F0F2').borderRadius(10)
                    .enabled(index !== 10 || this.state.ready && this.state.privacyCanToggle)
                    .opacity(index === 10 && (!this.state.ready || !this.state.privacyCanToggle) ? 0.45 : 1)
                    .accessibilitySelected(index === 10 && this.state.privacyEnabled)
                    .accessibilityText(index === 10 ? `${this.labels()[index]}，${this.state.privacyStatus}` : this.labels()[index]).accessibilityLevel('yes').onClick(() => this.action(index))
                }
              }, (index: number) => index.toString())
            }
            if (this.state.privacyFeedback) { Text(this.state.privacyFeedback).fontSize(11).fontColor(MobileTheme.secondary).width('100%') }
            if (this.state.transfer) { Text(this.state.transfer).fontSize(11).fontColor(MobileTheme.secondary).width('100%') }
            if (this.state.receivedPath) { Button(tr("保存收到的文件"), { type: ButtonType.Normal }).fontSize(14).fontColor(MobileTheme.blue).backgroundColor(Color.White).height(42).width('100%').onClick(() => this.controller?.saveFile()) }
          } else if (this.menu === 'video') {
            Text(tr("被控端分辨率")).fontSize(12).width('100%')
            Button(`${this.state.resolutionCurrent} ▾`, { type: ButtonType.Normal })
              .fontSize(13).fontColor(MobileTheme.ink).backgroundColor('#F0F0F2').borderRadius(7)
              .width('100%').height(34).enabled(this.state.resolutionEnabled)
              .bindMenu(() => { this.ResolutionMenu(this.state.resolutionModes, this.state.selectedDisplay, this.state.resolutionKey) })
            if (this.state.resolutionFeedback) { Text(this.state.resolutionFeedback).fontSize(12).fontColor(MobileTheme.secondary).width('100%') }
            Text(this.state.videoFeedback).fontSize(12).fontColor(MobileTheme.secondary).width('100%')
            Text(tr("画面质量")).fontSize(12).width('100%')
            MobileSegment({ labels: [tr("低"), tr("中"), tr("高")], selection: this.state.videoQuality,
              available: this.state.videoSupported, changed: (i: number) => this.controller?.updateVideo(0, i) })
            Text(tr("画面采集帧率")).fontSize(12).width('100%')
            MobileSegment({ labels: ['30 fps', '60 fps'], selection: this.state.videoRate === 60 ? 1 : 0,
              available: this.state.videoSupported, changed: (i: number) => this.controller?.updateVideo(1, i ? 60 : 30) })
            Text(tr("画面偏好")).fontSize(12).width('100%')
            MobileSegment({ labels: [tr("帧率优先"), tr("画质优先"), tr("平衡")], selection: this.state.videoSelectedPreference,
              available: this.state.videoSupported, changed: (i: number) => this.controller?.updateVideo(2, i) })
          } else {
            Row({ space: 4 }) {
              Text('').width(48)
              Text(tr("接收")).fontSize(11).fontColor(MobileTheme.secondary).layoutWeight(1).textAlign(TextAlign.End)
              Text(tr("发送")).fontSize(11).fontColor(MobileTheme.secondary).layoutWeight(1).textAlign(TextAlign.End)
              Text(tr("丢包率")).fontSize(11).minFontSize(8).maxFontSize(11).maxLines(2).fontColor(MobileTheme.secondary).width(46).textAlign(TextAlign.End)
            }.padding({ left: 4, right: 4 }).height(24).width('100%')
            Column() {
              this.NetworkRow(tr("视频"), this.controller?.network.current(this.state.networkTime)?.video)
              this.NetworkRow(tr("音频"), this.controller?.network.current(this.state.networkTime)?.audio)
              this.NetworkRow(tr("数据"), this.controller?.network.current(this.state.networkTime)?.data)
              this.NetworkRow(tr("合计"), this.controller?.network.current(this.state.networkTime)?.total)
            }.width('100%')
            Divider().strokeWidth(0.5).color(MobileTheme.line).margin({ top: 2, bottom: 2 })
            this.NetworkDetail(tr("帧率"), `${this.controller?.network.fps(this.state.networkTime) ?? '—'} FPS`)
            this.NetworkDetail(tr("分辨率"), this.state.hasFrame ? `${this.state.width} × ${this.state.height}` : '—')
            this.NetworkDetail(tr("画面延时"), NetworkStatistics.delay(this.controller?.network.latency(this.state.networkTime) ?? -1))
            this.NetworkDetail(tr("连接延时（RTT）"), NetworkStatistics.delay(this.state.networkTime - (this.controller?.network.rttTime ?? 0) < 3000 ? this.controller?.network.rtt ?? -1 : -1))
            this.NetworkDetail(tr("连接方式"), this.controller?.network.mode(this.state.networkTime) ?? '—')
            this.NetworkDetail(tr("媒体加密"), this.controller?.network.current(this.state.networkTime) === undefined ? '—' : this.controller?.network.report?.srtp ? tr("SRTP 已启用") : tr("未启用"), this.controller?.network.current(this.state.networkTime)?.srtp === true)
            this.NetworkCodec(tr("被控端编码器"), this.controller?.network.remoteEncoder || '—')
            this.NetworkCodec(tr("控制端解码器"), this.controller?.network.report?.decoder || '—')
          }
        }.width('100%')
      }.constraintSize({ maxHeight: Math.max(48, this.viewHeight - 132) }).width('100%').scrollBar(BarState.Off)
      Button(tr("断开连接"), { type: ButtonType.Normal }).fontSize(14).fontColor(Color.White).width('100%').height(34).borderRadius(8).backgroundColor(MobileTheme.red)
        .onClick(() => { this.release(); this.state.modal = 'disconnect'; this.menu = ''; this.keyboard = false; })
    }.padding(12).width(this.menuWidth()).backgroundColor(Color.White).borderRadius(18)
      .border({ width: 0.5, color: MobileTheme.line }).shadow({ radius: 24, color: '#33000000', offsetY: 6 })
      .onAreaChange((_old: Area, next: Area) => this.menuHeight = Number(next.height))
      .position({ x: this.menuX(), y: clamp(this.orbY, 8, Math.max(8, this.viewHeight - this.menuHeight - 8)) })
  }
  @Builder
  Keyboard() {
    Column({ space: 3 }) {
      Row() {
        Stack() { Row() {}.width(32).height(4).backgroundColor(MobileTheme.secondary).borderRadius(2) }.layoutWeight(1).height(28).accessibilityText(tr("拖动键盘"))
          .gesture(PanGesture().onActionStart(() => { this.keyboardOriginX = this.keyboardX; this.keyboardOriginY = this.keyboardY; })
            .onActionUpdate((e: GestureEvent) => this.keyboardDrag(e)))
        MobileIconButton({ icon: 'close', label: tr("收起键盘"), iconExtent: 28, iconSize: 18, action: () => { this.keyboard = false; this.modifiers = []; } })
      }.width('100%')
      Scroll() {
      Column({ space: 3 }) {
      ForEach(this.keyboardRows(), (row: KeyRow) => {
        Row({ space: 3 }) {
          ForEach(row.keys, (key: string) => {
            Text(this.keyboardTitle(key)).fontSize(key.length > 2 ? 11 : 14).minFontSize(8).maxFontSize(key.length > 2 ? 11 : 14).maxLines(1)
              .height(31).layoutWeight(key === 'space' ? 3 : 1).textAlign(TextAlign.Center).borderRadius(5).fontColor(MobileTheme.ink)
              .shadow({ radius: 0.5, color: '#33000000', offsetY: 1 })
              .backgroundColor((key === '⇧' && this.shift) || this.modifiers.includes(key === 'Ctrl' ? 17 : key === 'Alt' ? 18 : key === 'Win' ? 91 : -1) ? '#9BC7F7' : key.length > 1 || key === '⇧' ? '#AFB8C5' : '#FFFFFF')
              .accessibilityText(this.keyboardTitle(key)).accessibilityLevel('yes').onClick(() => this.keyboardKey(key))
          }, (key: string) => key)
        }.width('100%')
      })
      }.width('100%')
      }.width('100%').constraintSize({ maxHeight: Math.max(31, this.viewHeight - 60) }).scrollBar(BarState.Off)
    }.padding(5).width(this.keyboardWidth()).backgroundColor('#D1D6DE').borderRadius(12)
      .shadow({ radius: 20, color: '#33000000', offsetY: 5 }).onAreaChange((_old: Area, next: Area) => this.keyboardHeight = Number(next.height))
      .position({ x: clamp(this.keyboardX, 8, this.viewWidth - this.keyboardWidth() - 8), y: clamp(this.keyboardY, 8, Math.max(8, this.viewHeight - this.keyboardHeight - 8)) })
  }
  @Builder
  MouseBar() {
    Row({ space: 2 }) {
      ForEach([0, 1, 2], (button: number) => {
        Stack() {
          if (button === 1) {
            Stack() {
              Column({ space: 4 }) {
                ForEach([0, 1, 2, 3, 4, 5, 6], (line: number) => { Row() {}.height(3).width(14).backgroundColor('#737378') })
              }.translate({ y: this.wheelOffset - 8 })
            }.width(18).height(30).clip(true)
          } else { MobileIcon({ name: button === 0 ? 'mouse_left' : 'mouse_right', iconExtent: 22 }) }
        }.width(44).height(44).borderRadius(7).clip(true)
          .backgroundColor(button === 0 && this.leftHeld || button === 2 && this.rightHeld ? '#9BC7F7' : '#E9E9EE')
          .accessibilityText(button === 0 ? tr("鼠标左键，按住拖动") : button === 1 ? tr("滚轮，上下滑动") : tr("鼠标右键，按住拖动"))
          .onTouch((event: TouchEvent) => this.barTouch(event, button))
      })
    }.padding(2).width(140).height(48).backgroundColor('#FFFFFF').borderRadius(10)
      .border({ width: 0.5, color: MobileTheme.line }).shadow({ radius: 12, color: '#33000000', offsetY: 4 }).position(this.mousePosition())
  }
  build() {
    Stack({ alignContent: Alignment.TopStart }) {
      XComponent({ id: 'mobileVideo', type: XComponentType.SURFACE, controller: this.surface })
        .width(this.videoWidth).height(this.videoHeight).position({ x: this.videoLeft, y: this.videoTop })
        .scale({ x: this.zoom, y: this.zoom }).onLoad(() => this.controller?.surface(this.surface.getXComponentSurfaceId(), this.surface))
        .onDestroy(() => this.controller?.surface('0'))
      if (!this.state.hasFrame) {
        Column({ space: 12 }) {
          LoadingProgress().width(32).height(32).color(Color.White)
          Text(tr("等待远端画面…")).fontSize(14).fontColor(Color.White)
        }.padding(20).constraintSize({ minWidth: 200 }).backgroundColor('#B0202024').borderRadius(14)
          .position({ x: (this.viewWidth - 200) / 2, y: (this.viewHeight - 104) / 2 }).hitTestBehavior(HitTestMode.None)
      }
      Column() {}.width('100%').height('100%').focusable(true).defaultFocus(true)
        .onTouch((e: TouchEvent) => this.touch(e)).onMouse((e: MouseEvent) => this.physicalMouse(e))
        .onAxisEvent((event: AxisEvent) => { const delta = event.getVerticalAxisValue(); if (delta) this.mouse.scroll(-delta); })
        .onKeyEvent((e: KeyEvent) => { if (e.type === KeyType.Down || e.type === KeyType.Up) { this.controller?.physicalKey(e.keyCode, e.type === KeyType.Down); return true; } return false; })
        .onBlur(() => this.controller?.release())
        .accessibilityText(tr("远程桌面：轻触点击、长按右键、双指捏合缩放、放大后双指拖动画面，原始比例下双指滚动"))
      MobileIcon({ name: 'cursor', iconExtent: 24, color: '#000000' })
        .position({ x: this.cursorX - 1, y: this.cursorY - 1 }).hitTestBehavior(HitTestMode.None)
      if (this.menu) { Column() {}.width('100%').height('100%').onClick(() => this.menu = ''); this.Menu() }
      if (this.keyboard) { this.Keyboard() }
      if (this.mouseVisible && !this.menu && this.state.hasFrame) { this.MouseBar() }
      Stack() {
        Image($r('app.media.mobile_brand')).width(46).height(46).objectFit(ImageFit.Cover).borderRadius(23).draggable(false).hitTestBehavior(HitTestMode.None)
      }.width(54).height(54).backgroundColor('#F5F5F7').borderRadius(27).shadow({ radius: 14, color: '#33000000', offsetY: 4 })
        .position({ x: this.orbX, y: this.orbY }).accessibilityText(tr("远程控制菜单，拖动可移动")).accessibilityLevel('yes')
        .onTouch((e: TouchEvent) => this.orbTouch(e))
    }.width('100%').height('100%').backgroundColor(Color.Black).clip(true)
      .onAreaChange((_old: Area, next: Area) => {
        this.release(); this.viewWidth = Number(next.width); this.viewHeight = Number(next.height); this.fit();
        if (!this.orbPositioned && this.viewWidth > 100) { this.orbX = this.viewWidth - 66; this.orbY = 17; this.orbPositioned = true; }
        this.orbX = clamp(this.orbX, 8, this.viewWidth - 62); this.orbY = clamp(this.orbY, 8, this.viewHeight - 62);
        this.keyboardX = clamp(this.keyboardX, 8, this.viewWidth - this.keyboardWidth() - 8);
      })
  }
}
