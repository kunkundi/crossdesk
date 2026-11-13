import { tr, currentLanguage } from '../mobile/Localization';
import { common } from '@kit.AbilityKit';
import { MobileController, MobileState, RemoteDisplay } from '../mobile/MobileController';
import { MobileRemoteView } from '../mobile/MobileRemoteView';
import { MobileTheme, MobileSegment, MobileIcon, MobileIconButton, MobilePrimaryButton } from '../mobile/MobileWidgets';
import { RecentDevice } from '../mobile/MobileStore';
import { AnnouncementItem } from '../mobile/AnnouncementInbox';
import { AnnouncementSpan, announcementSpans } from '../mobile/AnnouncementText';
import { util } from '@kit.ArkTS';
import { KeyboardAvoidMode } from '@kit.ArkUI';

interface LicenseDocument { title: string; asset: string; sha256?: string; }
interface LicenseComponent { id: string; name: string; license: string; version: string; sourceURL: string; documents: LicenseDocument[]; }
interface LicenseCatalog { components: LicenseComponent[]; }
interface SourceMetadata { revision: string; miniRTCRevision: string; isModified: boolean; sourceURL: string; buildDocuments: LicenseDocument[]; }
@Entry
@Component
struct Mobile {
  @State private state: MobileState = new MobileState();
  @State private viewWidth: number = 400;
  @State private viewHeight: number = 720;
  @State private idFocused: boolean = false;
  @State private serverExpanded: boolean = false;
  @State private customServer: boolean = false;
  @State private previewToggle: boolean = false;
  @State private privacyText: string = '';
  @State private editingServer: string = '';
  @State private editingPort: string = '';
  @State private showPassword: boolean = false;
  @State private accepted: boolean = false;
  @State private deleteId: string = '';
  @State private refreshing: boolean = false;
  @State private licenses: LicenseComponent[] = [];
  @State private component: LicenseComponent | undefined = undefined;
  @State private source: SourceMetadata | undefined = undefined;
  private controller?: MobileController;
  aboutToAppear(): void {
    // Keep the entire password/clipboard sheet above the software keyboard.
    this.getUIContext().setKeyboardAvoidMode(KeyboardAvoidMode.RESIZE);
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    this.controller = new MobileController(context); this.state = this.controller.state;
    try {
      const decoder = util.TextDecoder.create('utf-8');
      this.privacyText = decoder.decodeWithStream(context.resourceManager.getRawFileContentSync('PRIVACY.md'));
      this.licenses = (JSON.parse(decoder.decodeWithStream(context.resourceManager.getRawFileContentSync('ThirdPartyLicenses.json'))) as LicenseCatalog).components;
      this.source = JSON.parse(decoder.decodeWithStream(context.resourceManager.getRawFileContentSync('SourceMetadata.json'))) as SourceMetadata;
    } catch { /* The package verification rejects missing legal resources. */ }
  }
  onBackPress(): boolean {
    if (this.state.modal === 'connecting') { this.controller?.end(''); return true; }
    if (this.state.modal) { if (this.state.modal !== 'consent') this.dismissModal(); return true; }
    if (this.state.ready) { this.state.modal = 'disconnect'; return true; }
    if (this.state.page === 'home') return false;
    this.back(); return true;
  }
  private navigate(page: string): void {
    if (page === 'settings') {
      this.editingServer = this.state.server; this.editingPort = this.state.port;
      this.customServer = this.state.server !== 'api.crossdesk.cn' || this.state.port !== '9099';
      this.previewToggle = this.state.previews;
    }
    this.getUIContext().animateTo({ duration: 200, curve: Curve.EaseInOut }, () => this.controller?.navigate(page));
  }
  private back(): void {
    const page = this.state.page;
    if (page === 'settings' && this.customServer && this.serverExpanded && !this.saveServer()) return;
    this.navigate(page === 'document' ? this.state.documentBack : page === 'announcement' ? 'announcements' :
      page === 'component' ? 'licenses' : page === 'licenses' || page === 'source' ? 'about' : page === 'privacy' || page === 'about' ? 'settings' : 'home');
    if (this.state.page === 'home' && !this.state.consent) this.state.modal = 'consent';
  }
  private title(): string {
    switch (this.state.page) {
      case 'settings': return tr("设置"); case 'privacy': return tr("隐私与授权");
      case 'about': return tr("关于 CrossDesk"); case 'licenses': return tr("开源组件"); case 'component': return this.component?.name ?? '';
      case 'source': return tr("源码与构建说明"); case 'announcements': return tr("通知公告"); case 'announcement': return tr("公告详情");
      case 'document': return this.state.documentTitle; default: return '';
    }
  }
  private deviceId(value: string): string { return value.replace(/[^0-9]/g, '').substring(0, 9); }
  private formatId(value: string): string { return this.deviceId(value).replace(/(.{3})(?=.)/g, '$1 '); }
  private policyParagraphs(body: string): string[] {
    const text = body.replace(/\r\n/g, '\n');
    const start = text.indexOf('## 中文\n'), end = start < 0 ? -1 : text.indexOf('## English', start);
    const chinese = currentLanguage() === 0 || currentLanguage() === 3;
    const localized = chinese && start >= 0 && end > start ? text.substring(start + '## 中文\n'.length, end) :
      !chinese && end >= 0 ? text.substring(end + '## English'.length) : text;
    return localized.split(/\n\s*\n/).map((paragraph: string) => paragraph.trim()).filter((paragraph: string) => !!paragraph && paragraph !== '---');
  }
  private openAnnouncement(item: AnnouncementItem): void { this.controller?.inbox.markRead(item); this.state.announcement = item; this.navigate('announcement'); }
  private saveServer(): boolean {
    this.controller?.saveSettings(this.editingServer, this.editingPort);
    const saved = this.state.server === this.editingServer.trim() && this.state.port === this.editingPort;
    if (saved) this.serverExpanded = false;
    return saved;
  }
  private dismissModal(): void {
    if (this.state.modal === 'privacyUnlock') this.controller?.dismissPrivacyUnlock();
    if (this.state.modal === 'password') { this.state.password = ''; this.showPassword = false; }
    this.previewToggle = this.state.previews; this.state.modal = '';
  }
  private confirmModal(): void {
    const modal = this.state.modal; this.state.modal = '';
    if (modal === 'disconnect') this.controller?.end('');
    if (modal === 'revoke') this.controller?.revoke();
    if (modal === 'previews') { this.controller?.setPreviews(true); this.previewToggle = true; }
    if (modal === 'clearPreviews') this.controller?.clearPreviews();
    if (modal === 'delete') this.controller?.deleteRecent(this.deleteId);
    if (modal === 'deleteAnnouncement' && this.state.announcement) { this.controller?.inbox.dismiss(this.state.announcement); this.navigate('announcements'); }
  }
  @Builder
  SectionTitle(label: string) {
    Text(label).fontSize(13).fontColor(MobileTheme.secondary).width('100%').padding({ left: 16, top: 12, bottom: 8 })
  }
  @Builder
  Footer(label: string) {
    Text(label).fontSize(12).lineHeight(18).fontColor(MobileTheme.secondary).width('100%')
      .padding({ left: 16, right: 16, top: 8, bottom: 16 })
  }
  @Builder
  Rule() { Divider().strokeWidth(0.5).color(MobileTheme.line) }
  @Builder
  ValueRow(label: string, value: string) {
    Row({ space: 16 }) {
      Text(label).fontSize(16).fontColor(MobileTheme.ink)
      Text(value).fontSize(15).fontColor(MobileTheme.secondary).layoutWeight(1).textAlign(TextAlign.End).copyOption(CopyOptions.LocalDevice)
    }.constraintSize({ minHeight: 52 }).width('100%').padding({ left: 16, right: 16, top: 14, bottom: 14 })
  }
  @Builder
  Action(label: string, action: () => void, danger: boolean = false) {
    Button(label, { type: ButtonType.Normal, stateEffect: true }).fontSize(16).fontWeight(500)
      .fontColor(danger ? MobileTheme.red : MobileTheme.blue).backgroundColor(Color.White)
      .height(48).width('100%').borderRadius(12).onClick(action)
  }
  @Builder
  SettingRow(label: string, detail: string, action: () => void) {
    Button({ type: ButtonType.Normal, stateEffect: true }) {
      Row({ space: 8 }) {
        Text(label).fontSize(16).fontColor(MobileTheme.ink).layoutWeight(1).textAlign(TextAlign.Start)
        if (detail) { Text(detail).fontSize(15).fontColor(MobileTheme.secondary).maxLines(1).constraintSize({ maxWidth: '46%' }).textOverflow({ overflow: TextOverflow.Ellipsis }) }
        MobileIcon({ name: 'chevron', iconExtent: 16, color: '#B7B7BD' })
      }.width('100%').constraintSize({ minHeight: 52 }).padding({ left: 16, right: 12, top: 12, bottom: 12 })
    }.width('100%').padding(0).backgroundColor(Color.White).borderRadius(0).onClick(action).accessibilityText(`${label} ${detail}`)
  }
  @Builder
  ConnectionCard() {
    Column({ space: 16 }) {
      Text(tr("远程桌面")).fontSize(20).fontWeight(600).fontColor(MobileTheme.ink).width('100%')
      Row({ space: 10 }) {
        TextInput({ text: this.formatId(this.state.remote), placeholder: tr("对端 ID") }).type(InputType.Number).maxLength(11)
          .fontSize(20).fontWeight(500).fontColor(MobileTheme.ink).placeholderColor(MobileTheme.secondary)
          .height(54).layoutWeight(1).backgroundColor(MobileTheme.background).borderRadius(12)
          .border({ width: this.idFocused ? 2 : 1, color: this.idFocused ? MobileTheme.blue : MobileTheme.line })
          .padding({ left: 14, right: 14 }).onFocus(() => this.idFocused = true).onBlur(() => this.idFocused = false)
          .onChange((value: string) => this.state.remote = this.deviceId(value)).onSubmit(() => this.controller?.requestConnection(this.state.remote))
        Column() {
          MobilePrimaryButton({ label: tr("连接"), buttonHeight: 54, available: this.state.remote.length > 0,
            action: () => this.controller?.requestConnection(this.state.remote) })
        }.width(96)
      }.width('100%')
    }.padding(18).width('100%').backgroundColor(Color.White).borderRadius(20)
      .border({ width: 0.5, color: MobileTheme.line }).shadow({ radius: 14, color: '#0E000000', offsetY: 5 })
  }
  @Builder
  RecentCard(device: RecentDevice) {
    Column() {
      Stack() {
        Text(device.platform === 'windows' ? 'Windows' : device.platform === 'macos' ? 'macOS' : device.platform === 'linux' ? 'Linux' : tr("远程电脑"))
          .fontSize(19).fontWeight(600).fontColor(Color.White).width('100%').height('100%').textAlign(TextAlign.Center)
        if (this.state.previews && device.preview) { Image(`file://${device.preview}`).objectFit(ImageFit.Cover).width('100%').height('100%') }
      }.aspectRatio(16 / 9).width('100%').backgroundColor(device.platform === 'windows' ? '#316AB7' : device.platform === 'macos' ? '#8057B9' : device.platform === 'linux' ? '#AE642B' : '#677587')
      Column({ space: 4 }) {
        Text(device.name || device.id).fontSize(12).fontWeight(600).maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis }).width('100%')
        Row({ space: 4 }) {
          Text(`ID ${device.id}`).fontSize(10).fontColor(MobileTheme.secondary).layoutWeight(1)
          if (device.remember) { MobileIcon({ name: 'key', iconExtent: 11, color: MobileTheme.secondary }).accessibilityText(tr("已保存密码")) }
          Text(this.controller?.presence.online(device.id, this.controller?.now() ?? 0) ? tr("在线") : tr("离线")).fontSize(10)
            .fontColor(this.controller?.presence.online(device.id, this.controller?.now() ?? 0) ? MobileTheme.green : MobileTheme.secondary)
        }.width('100%')
      }.padding(9).width('100%')
    }.width('100%').backgroundColor(Color.White).border({ width: 0.5, color: MobileTheme.line }).borderRadius(14).clip(true)
      .onClick(() => this.controller?.requestConnection(device.id))
      .gesture(LongPressGesture().onAction(() => { this.deleteId = device.id; this.state.modal = 'recentActions'; }))
  }
  @Builder
  Recent() {
    Column({ space: 14 }) {
      Row() {
        Text(tr("最近连接")).fontSize(20).fontWeight(600).fontColor(MobileTheme.ink).layoutWeight(1)
        if (this.state.recent.length) { Text(tr("{0} 台设备", [String(this.state.recent.length)])).fontSize(12).fontColor(MobileTheme.secondary) }
      }.width('100%')
      if (this.state.recent.length === 0) {
        Column() {
          MobileIcon({ name: 'history', iconExtent: 34, color: '#B6B6BB' })
          Text(tr("还没有连接记录")).fontSize(17).fontWeight(600).fontColor(MobileTheme.ink).margin({ top: 12, bottom: 8 })
          Text(tr("成功连接后，这里会显示最近的远程设备。")).fontSize(13).lineHeight(19).fontColor(MobileTheme.secondary).textAlign(TextAlign.Center)
        }.layoutWeight(1).width('100%').justifyContent(FlexAlign.Center).padding({ left: 6, right: 6 })
      } else {
        Scroll() {
          GridRow({ columns: { xs: 1, sm: 2, md: 3, lg: 4 }, breakpoints: { value: ['300vp', '564vp', '756vp'], reference: BreakpointsReference.ComponentSize }, gutter: 12 }) {
            ForEach(this.state.recent, (device: RecentDevice) => { GridCol() { this.RecentCard(device) } }, (device: RecentDevice) => device.id)
          }.width('100%')
        }.layoutWeight(1).width('100%').align(Alignment.Top).scrollBar(BarState.Off)
      }
    }.padding(16).width('100%').height('100%').backgroundColor(Color.White).borderRadius(18).border({ width: 0.5, color: MobileTheme.line })
  }
  @Builder
  Home() {
    Column() {
      Row({ space: 12 }) {
        Button({ type: ButtonType.Normal }) {
          Row({ space: 6 }) {
            Row() {}.width(6).height(6).borderRadius(3).backgroundColor(this.state.signal === 1 ? MobileTheme.green : MobileTheme.secondary)
            Text(this.state.signalText).fontSize(12).fontWeight(600).fontColor(this.state.signal === 1 ? MobileTheme.green : MobileTheme.secondary)
          }
        }.padding({ left: 11, right: 11 }).height(34).backgroundColor(Color.White).borderRadius(17).onClick(() => this.controller?.reconnect())
        Blank()
        Stack({ alignContent: Alignment.TopEnd }) {
          MobileIconButton({ icon: 'envelope', label: tr("通知公告"), action: () => this.navigate('announcements') })
          if (this.state.unread > 0) {
            Text(this.state.unread > 99 ? '99+' : `${this.state.unread}`).fontSize(10).fontWeight(600).fontColor(Color.White)
              .textAlign(TextAlign.Center).padding({ left: 4, right: 4 }).constraintSize({ minWidth: 16 }).height(16)
              .backgroundColor(MobileTheme.red).borderRadius(8).translate({ x: 5, y: -3 }).hitTestBehavior(HitTestMode.None)
          }
        }
        Stack({ alignContent: Alignment.TopEnd }) {
          MobileIconButton({ icon: 'settings', label: tr("设置"), action: () => this.navigate('settings') })
          if (this.state.update) {
            Text('!').fontSize(10).fontWeight(600).fontColor(Color.White).textAlign(TextAlign.Center).width(16).height(16)
              .backgroundColor(MobileTheme.red).borderRadius(8).translate({ x: 5, y: -3 }).hitTestBehavior(HitTestMode.None)
          }
        }
      }.padding({ left: 20, right: 20 }).height(56).width('100%').constraintSize({ maxWidth: 1200 })
      Scroll() {
        if (this.viewWidth >= 900) {
          Row({ space: 24 }) {
            Column() { this.ConnectionCard() }.width(360)
            Column() { this.Recent() }.layoutWeight(1).height('100%')
          }.alignItems(VerticalAlign.Top).width('100%').constraintSize({ maxWidth: 1200 })
            .height(Math.max(360, this.viewHeight - 56)).padding({ left: 20, right: 20, top: 24, bottom: 12 })
        } else {
          Column({ space: 20 }) {
            this.ConnectionCard()
            Column() { this.Recent() }.layoutWeight(1).width('100%')
          }.width('100%').height(Math.max(440, this.viewHeight - 56)).padding({ left: 20, right: 20, top: 24, bottom: 12 })
        }
      }.layoutWeight(1).width('100%').align(Alignment.Top).scrollBar(BarState.Off)
    }.width('100%').height('100%')
  }
  @Builder
  PreviewRow() {
    Row({ space: 12 }) {
      Text(tr("保存远程画面预览")).fontSize(16).fontColor(MobileTheme.ink).layoutWeight(1)
      Toggle({ type: ToggleType.Switch, isOn: this.previewToggle }).selectedColor(MobileTheme.green)
        .onChange((value: boolean) => {
          this.previewToggle = value;
          if (value && !this.state.previews) this.state.modal = 'previews';
          else if (!value) this.controller?.setPreviews(false);
        }).accessibilityText(tr("保存远程画面预览"))
    }.constraintSize({ minHeight: 52 }).padding({ left: 16, right: 16, top: 8, bottom: 8 }).width('100%')
  }
  @Builder
  Settings() {
    Column() {
      this.SectionTitle(tr("鼠标控制"))
      Column() {
        MobileSegment({ labels: [tr("相对位置"), tr("绝对位置")], selection: this.state.relative ? 0 : 1,
          changed: (i: number) => this.controller?.setRelative(i === 0) })
      }.padding(12).width('100%').backgroundColor(Color.White).borderRadius(26).border({ width: 0.5, color: MobileTheme.line })
      this.SectionTitle(tr("画面偏好"))
      Column() {
        MobileSegment({ labels: [tr("帧率优先"), tr("画质优先"), tr("平衡")], selection: this.state.videoPreference,
          changed: (i: number) => this.controller?.setPreference(i) })
      }.padding(12).width('100%').backgroundColor(Color.White).borderRadius(26).border({ width: 0.5, color: MobileTheme.line })
      this.SectionTitle(tr("屏幕方向"))
      Row() {
        Text(tr("连接建立后锁定横屏")).fontSize(16).fontColor(MobileTheme.ink).layoutWeight(1)
        Toggle({ type: ToggleType.Switch, isOn: this.state.autoLandscape }).selectedColor(MobileTheme.green)
          .onChange((value: boolean) => this.controller?.setAutoLandscape(value)).accessibilityText(tr("连接建立后锁定横屏"))
      }.padding({ left: 16, right: 16, top: 10, bottom: 10 }).width('100%').backgroundColor(Color.White)
        .borderRadius(26).border({ width: 0.5, color: MobileTheme.line })
      Text(tr("默认关闭。开启后，手机连接时进入并保持横屏；关闭时跟随设备方向。平板和多窗口不受影响。"))
        .fontSize(12).fontColor(MobileTheme.secondary).width('100%').padding({ left: 16, right: 16, top: 8, bottom: 12 })
      Column() {
        this.SettingRow(tr("服务器"), this.customServer ? tr("自定义") : tr("默认"), () => this.serverExpanded = !this.serverExpanded)
        if (this.serverExpanded) {
          this.Rule()
          Row() {
            Text(tr("使用自定义服务器")).fontSize(16).fontColor(MobileTheme.ink).layoutWeight(1)
            Toggle({ type: ToggleType.Switch, isOn: this.customServer }).selectedColor(MobileTheme.green).onChange((value: boolean) => {
              this.customServer = value;
              if (!value) { this.editingServer = 'api.crossdesk.cn'; this.editingPort = '9099'; this.controller?.saveSettings(this.editingServer, this.editingPort); }
            }).accessibilityText(tr("使用自定义服务器"))
          }.padding({ left: 16, right: 16, top: 8, bottom: 8 }).width('100%')
          if (this.customServer) {
            Column({ space: 8 }) {
              TextInput({ text: this.editingServer, placeholder: tr("服务器地址") }).fontSize(17).height(48).borderRadius(12)
                .backgroundColor(MobileTheme.background).onChange((v: string) => this.editingServer = v)
              TextInput({ text: this.editingPort, placeholder: tr("服务器端口") }).fontSize(17).height(48).borderRadius(12)
                .backgroundColor(MobileTheme.background).type(InputType.Number).onChange((v: string) => this.editingPort = v)
              this.Action(tr("完成"), () => { this.saveServer(); })
            }.padding({ left: 12, right: 12, bottom: 12 }).width('100%')
          }
        }
      }.width('100%').backgroundColor(Color.White).borderRadius(26).clip(true).border({ width: 0.5, color: MobileTheme.line }).margin({ top: 28 })
      Text(this.state.identity ? tr("本机 ID  {0}", [String(this.formatId(this.state.identity))]) : this.state.signalText).fontSize(12).fontColor(MobileTheme.secondary)
        .textAlign(TextAlign.Center).padding({ left: 16, right: 16, top: 12, bottom: 16 }).width('100%').copyOption(CopyOptions.LocalDevice)
      this.SectionTitle(tr("本机配对信息"))
      Column() {
        this.SettingRow(tr("查看指纹"), '', () => this.controller?.pairingInfo(false))
        if (this.state.trustFingerprint) {
          Text(this.state.trustFingerprint).fontSize(14).fontFamily('monospace').fontColor(MobileTheme.secondary)
            .padding(16).width('100%').copyOption(CopyOptions.LocalDevice)
        }
        this.Rule()
        this.SettingRow(tr("复制配对信息"), '', () => this.controller?.pairingInfo(true))
      }.width('100%').backgroundColor(Color.White).borderRadius(26).clip(true).border({ width: 0.5, color: MobileTheme.line })
      Text(tr("将配对信息添加到被控端的可信设备列表，并核对指纹。"))
        .fontSize(12).fontColor(MobileTheme.secondary).width('100%').padding({ left: 16, right: 16, top: 8, bottom: 12 })
      this.SectionTitle(tr("隐私"))
      Column() {
        this.SettingRow(tr("隐私与授权"), this.state.consent ? tr("已授权") : tr("未授权"), () => this.navigate('privacy'))
        this.Rule()
        this.PreviewRow()
        this.Rule()
        Button({ type: ButtonType.Normal }) { Text(tr("清除预览图")).fontSize(16).fontColor(MobileTheme.red).width('100%').textAlign(TextAlign.Start) }
          .height(48).width('100%').padding({ left: 16 }).backgroundColor(Color.White)
          .onClick(() => this.state.modal = 'clearPreviews')
      }.width('100%').backgroundColor(Color.White).borderRadius(26).clip(true).border({ width: 0.5, color: MobileTheme.line })
      Column() { this.SettingRow(tr("关于"), this.state.update ? tr("新版本 ●") : '', () => this.navigate('about')) }
        .width('100%').backgroundColor(Color.White).borderRadius(26).clip(true).border({ width: 0.5, color: MobileTheme.line }).margin({ top: 28 })
    }.width('100%')
  }
  @Builder
  Privacy() {
    Column() {
      Column() {
        this.ValueRow(tr("授权状态"), this.state.consent ? tr("已授权") : tr("未授权"))
        this.Rule()
        this.SettingRow(tr("隐私政策"), '', () => this.controller?.document('PRIVACY.md', tr("隐私政策"), 'privacy'))
      }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
      this.Footer(tr("设备身份及选择保存的访问密码加密保存在本机。画面预览默认关闭，开启后从下一次连接起保存。剪贴板内容在结束会话时清除。离开应用后通过画中画或后台任务保持会话，可随时返回应用断开。"))
      if (this.state.consent) { this.Action(tr("撤回联网授权"), () => this.state.modal = 'revoke', true) }
      else { MobilePrimaryButton({ label: tr("阅读并授权"), action: () => this.state.modal = 'consent' }) }
    }.width('100%')
  }
  @Builder
  About() {
    Column() {
      Column() {
        this.ValueRow(tr("版本"), this.state.version)
        if (this.state.update) { this.Rule(); this.SettingRow(tr("新版本可用"), `v${this.state.update}`, () => this.controller?.platform.openLink('https://crossdesk.cn')) }
        if (this.state.updateStatus) { this.Footer(this.state.updateStatus) }
        this.Rule()
        this.Action(tr("检查更新"), () => this.controller?.checkLocal(true))
      }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
      Column() {
        this.SettingRow(tr("软件许可"), 'GPL-3.0-only', () => this.controller?.document('LICENSE', tr("软件许可")))
        this.Rule()
        this.SettingRow(tr("开源软件权利说明"), '', () => this.controller?.document('OPEN_SOURCE_NOTICE.txt', tr("开源软件权利说明")))
        this.Rule()
        this.SettingRow(tr("开源组件"), '', () => this.navigate('licenses'))
        this.Rule()
        this.SettingRow(tr("源码与构建说明"), '', () => this.navigate('source'))
      }.backgroundColor(Color.White).borderRadius(12).clip(true).width('100%').border({ width: 0.5, color: MobileTheme.line }).margin({ top: 28 })
      this.Footer(tr("许可与版权声明可离线查阅。"))
    }.width('100%')
  }
  @Builder
  Announcements() {
    Column({ space: 12 }) {
      if (!this.state.consent) { Text(tr("同意联网后可查看公告")).fontSize(14); this.Action(tr("查看联网说明"), () => this.state.modal = 'consent') }
      if (this.controller?.inbox.failed) { this.Action(tr("加载失败，点击重试"), () => this.controller?.inbox.refresh()) }
      if (this.controller?.inbox.loading) { LoadingProgress().width(28).height(28) }
      if (this.controller?.inbox.saveFailed) { Text(tr("已读或删除状态保存失败，请重试")).fontSize(12).fontColor(MobileTheme.red) }
      if (!this.state.announcements.length && this.controller?.inbox.loaded && !this.controller?.inbox.loading) { Text(tr("暂无公告")).fontSize(16).fontColor(MobileTheme.secondary).padding(40) }
      ForEach(this.state.announcements, (item: AnnouncementItem) => {
        Column({ space: 8 }) {
          Row({ space: 8 }) { if (!item.read) { Row() {}.width(6).height(6).borderRadius(3).backgroundColor(MobileTheme.blue) } Text(item.title).fontSize(17).fontWeight(600).layoutWeight(1) }.width('100%')
          Text(new Date(item.updated_at * 1000).toLocaleDateString('zh-CN')).fontSize(12).fontColor(MobileTheme.secondary).width('100%')
          Text(item.body).fontSize(13).fontColor(MobileTheme.secondary).maxLines(2).textOverflow({ overflow: TextOverflow.Ellipsis }).width('100%')
        }.padding(18).width('100%').backgroundColor(Color.White).borderRadius(18).border({ width: 0.5, color: MobileTheme.line })
          .onClick(() => this.openAnnouncement(item)).gesture(LongPressGesture().onAction(() => { this.state.announcement = item; this.state.modal = 'deleteAnnouncement'; }))
      }, (item: AnnouncementItem) => `${item.id}:${item.revision}:${item.read}`)
      if ((this.controller?.inbox.total ?? 0) > this.state.announcements.length) { this.Action(tr("加载更多"), () => this.controller?.inbox.more()) }
    }.width('100%')
  }
  @Builder
  Content() {
    if (this.state.page === 'settings') { this.Settings() }
    else if (this.state.page === 'privacy') { this.Privacy() }
    else if (this.state.page === 'about') { this.About() }
    else if (this.state.page === 'announcements') { this.Announcements() }
    else if (this.state.page === 'announcement') {
      Column({ space: 16 }) {
        Text(this.state.announcement?.title ?? '').fontSize(24).fontWeight(600).width('100%').copyOption(CopyOptions.LocalDevice)
        Text() {
          ForEach(announcementSpans(this.state.announcement?.body ?? ''), (part: AnnouncementSpan) => {
            if (part.url) {
              Span(part.text).fontColor(MobileTheme.blue).onClick(() => this.controller?.platform.openLink(part.url))
            } else { Span(part.text) }
          })
        }.fontSize(16).lineHeight(25).width('100%').copyOption(CopyOptions.LocalDevice)
        this.Action(tr("删除公告"), () => this.state.modal = 'deleteAnnouncement', true)
      }.width('100%')
    } else if (this.state.page === 'document') {
      if (this.state.documentTitle === tr("隐私政策")) { this.PolicyText(this.state.documentText) }
      else {
      Column({ space: 12 }) {
        ForEach(this.state.documentText.match(/[\s\S]{1,4000}/g) ?? [], (paragraph: string) => {
          Text(paragraph).fontSize(15).lineHeight(24).width('100%').copyOption(CopyOptions.LocalDevice)
        })
      }.width('100%')
      }
    } else if (this.state.page === 'licenses') {
      Column() {
        this.SectionTitle(tr("OpenFEC 告知"))
        Column() { this.Footer(tr("OpenFEC 提供前向纠错能力，遵循 CeCILL-C 1.0 许可。其许可规定了有限保证及责任限制，请阅读组件详情。")) }
          .width('100%').backgroundColor(Color.White).borderRadius(12).border({ width: 0.5, color: MobileTheme.line })
        Column() {
          ForEach(this.licenses, (component: LicenseComponent, index: number) => {
            if (index > 0) { this.Rule() }
            Button({ type: ButtonType.Normal }) {
              Row({ space: 12 }) {
                Column({ space: 4 }) {
                  Text(component.name).fontSize(16).fontColor(MobileTheme.ink).width('100%')
                  Text(component.license).fontSize(12).fontColor(MobileTheme.secondary).width('100%')
                }.layoutWeight(1).alignItems(HorizontalAlign.Start)
                MobileIcon({ name: 'chevron', iconExtent: 16, color: '#B7B7BD' })
              }.width('100%').padding({ left: 16, right: 12, top: 12, bottom: 12 })
            }.width('100%').padding(0).backgroundColor(Color.White).onClick(() => { this.component = component; this.navigate('component'); })
          }, (component: LicenseComponent) => component.id)
        }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line }).margin({ top: 28 })
        this.Footer(tr("各组件遵循所列开源许可证。许可与版权声明可离线查阅，查看源码需要网络连接。"))
      }.width('100%')
    } else if (this.state.page === 'component') {
      Column() {
        Column() {
          this.ValueRow(tr("版本"), this.component?.version ?? '')
          this.Rule()
          this.ValueRow(tr("许可"), this.component?.license ?? '')
          this.Rule()
          this.SettingRow(tr("查看源码"), '', () => this.controller?.platform.openLink(this.component?.sourceURL ?? ''))
        }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
        this.SectionTitle(tr("许可与版权声明"))
        Column() {
          ForEach(this.component?.documents ?? [], (document: LicenseDocument, index: number) => {
            if (index > 0) { this.Rule() }
            this.SettingRow(document.title, '', () => this.controller?.document(document.asset, document.title, 'component'))
          })
        }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
      }.width('100%')
    } else if (this.state.page === 'source') {
      Column() {
        this.SectionTitle(tr("本版本源码与构建说明"))
        Column() {
          this.ValueRow(tr("源码版本"), (this.source?.revision ?? '').substring(0, 12))
          this.Rule()
          this.ValueRow('MiniRTC', (this.source?.miniRTCRevision ?? '').substring(0, 12))
          if (this.source?.isModified) { this.Rule(); this.Footer(tr("开发版本包含尚未发布的修改，以下链接为基础版本源码。")) }
          this.Rule()
          this.SettingRow(tr("查看源码"), '', () => this.controller?.platform.openLink(this.source?.sourceURL ?? ''))
        }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
        this.SectionTitle(tr("构建文档"))
        Column() {
          ForEach(this.source?.buildDocuments ?? [], (document: LicenseDocument, index: number) => {
            if (index > 0) { this.Rule() }
            this.SettingRow(document.title, '', () => this.controller?.document(document.asset, document.title, 'source'))
          })
        }.width('100%').backgroundColor(Color.White).borderRadius(12).clip(true).border({ width: 0.5, color: MobileTheme.line })
      }.width('100%')
    }
  }
  @Builder
  SheetHeader(title: string, subtitle: string = '') {
    Row({ space: 12 }) {
      Column({ space: 5 }) {
        Text(title).fontSize(22).fontWeight(600).fontColor(MobileTheme.ink).width('100%')
        if (subtitle) { Text(subtitle).fontSize(15).fontColor(MobileTheme.secondary).width('100%') }
      }.layoutWeight(1)
      MobileIconButton({ icon: 'close', label: tr("关闭"), iconExtent: 36, iconSize: 20, action: () => this.dismissModal() })
    }.width('100%')
  }
  @Builder
  PasswordSheet() {
    Scroll() {
      Column() {
        Row() {}.width(36).height(5).backgroundColor('#C6C6CB').borderRadius(3).margin({ bottom: 20 })
        this.SheetHeader(tr("连接远程桌面"), tr("对端 ID  {0}", [String(this.formatId(this.state.remote))]))
        Row() {
          TextInput({ text: this.state.password, placeholder: tr("访问密码") }).type(this.showPassword ? InputType.Normal : InputType.Password)
            .showPasswordIcon(false).maxLength(128).fontSize(17).height(50).layoutWeight(1).backgroundColor(Color.Transparent)
            .padding({ left: 14, right: 4 }).onChange((value: string) => this.state.password = value).onSubmit(() => this.controller?.begin(this.state.password))
          MobileIconButton({ icon: this.showPassword ? 'eye_off' : 'eye', label: this.showPassword ? tr("隐藏密码") : tr("显示密码"), iconExtent: 44,
            action: () => this.showPassword = !this.showPassword })
        }.width('100%').backgroundColor(Color.White).borderRadius(10).border({ width: 0.5, color: MobileTheme.line }).margin({ top: 18 })
        if (this.state.notice) { Text(this.state.notice).fontSize(13).fontColor(MobileTheme.red).width('100%').margin({ top: 8 }) }
        Row({ space: 12 }) {
          Column({ space: 3 }) {
            Text(tr("保存密码")).fontSize(15).fontWeight(600).fontColor(MobileTheme.ink).width('100%')
            Text(tr("密码将加密保存在此设备上")).fontSize(11).fontColor(MobileTheme.secondary).width('100%')
          }.layoutWeight(1)
          Toggle({ type: ToggleType.Switch, isOn: this.state.remember }).selectedColor(MobileTheme.green)
            .onChange((value: boolean) => this.state.remember = value).accessibilityText(tr("保存密码"))
        }.width('100%').margin({ top: 18, bottom: 18 })
        MobilePrimaryButton({ label: tr("连接"), available: this.state.password.length > 0, action: () => this.controller?.begin(this.state.password) })
      }.padding({ left: 22, right: 22, top: 10, bottom: 22 }).width('100%')
    }.width(this.viewWidth < 600 ? '100%' : Math.min(520, this.viewWidth - 48)).constraintSize({ maxHeight: this.viewHeight * 0.92 })
      .backgroundColor(MobileTheme.background).borderRadius(24).scrollBar(BarState.Off)
      .shadow({ radius: 28, color: '#18000000', offsetY: 8 })
  }
  @Builder
  PolicyText(body: string) {
    Column({ space: 14 }) {
      ForEach(this.policyParagraphs(body), (paragraph: string) => {
        Text(paragraph.replace(/^#+\s*/, '').replace(/\*\*/g, '')).fontSize(paragraph.startsWith('更新日期：') ? 12 : 16)
          .fontWeight(paragraph.startsWith('#') ? 600 : 400).lineHeight(24)
          .fontColor(paragraph.startsWith('更新日期：') ? MobileTheme.secondary : MobileTheme.ink).width('100%').copyOption(CopyOptions.LocalDevice)
      })
    }.width('100%')
  }
  @Builder
  ConsentSheet() {
    Column() {
      Text(tr("隐私政策")).fontSize(17).fontWeight(600).fontColor(MobileTheme.ink).height(56).width('100%').textAlign(TextAlign.Center)
      this.Rule()
      Scroll() {
        Column({ space: 12 }) {
          if (!this.state.privacyAvailable) { Text(tr("隐私政策无法读取，请重新安装应用")).fontSize(14).fontColor(MobileTheme.red) }
          this.PolicyText(this.privacyText)
        }.padding(20).width('100%')
      }.layoutWeight(1).width('100%').align(Alignment.Top).scrollBar(BarState.Off)
      this.Rule()
      Column({ space: 12 }) {
        Row({ space: 8 }) {
          Checkbox({ name: 'consent' }).width(20).height(20).margin(0).select(this.accepted).selectedColor(MobileTheme.blue)
            .onChange((value: boolean) => this.accepted = value).accessibilityText(tr("我已阅读并同意隐私政策"))
          Text(tr("我已阅读并同意《隐私政策》")).fontSize(14).fontColor(MobileTheme.ink).layoutWeight(1).onClick(() => this.accepted = !this.accepted)
        }.width('100%').constraintSize({ minHeight: 44 })
        MobilePrimaryButton({ label: tr("同意并继续"), available: this.accepted && this.state.privacyAvailable, action: () => this.controller?.consent() })
        Button(tr("暂不同意"), { type: ButtonType.Normal }).fontSize(16).fontColor(MobileTheme.secondary).height(44).width('100%')
          .backgroundColor(Color.Transparent).onClick(() => this.dismissModal())
      }.padding({ left: 20, right: 20, top: 12, bottom: 12 }).width('100%')
    }.width(this.viewWidth < 600 ? '100%' : Math.min(640, this.viewWidth - 48)).height(Math.min(760, this.viewHeight * 0.9))
      .backgroundColor(Color.White).borderRadius(20).clip(true)
  }
  @Builder
  DialogBody() {
    if (this.state.modal === 'connecting') {
      Column({ space: 14 }) {
        LoadingProgress().width(34).height(34).color(MobileTheme.blue)
        Text(tr("正在连接")).fontSize(20).fontWeight(600).fontColor(MobileTheme.ink)
        Text(this.state.signal === 1 ? tr("正在建立安全连接…") : this.state.signalText).fontSize(14).fontColor(MobileTheme.secondary).textAlign(TextAlign.Center).constraintSize({ minHeight: 36 })
      }.padding({ left: 26, right: 26, top: 24, bottom: 20 }).width('100%')
      this.Rule()
      this.Action(tr("取消连接"), () => this.controller?.end(''), true)
    } else if (this.state.modal === 'privacyUnlock') {
      Column({ space: 12 }) {
        Text(tr("隐私屏已被解锁")).fontSize(20).fontWeight(600).fontColor(MobileTheme.ink)
        Text(tr("被控端已通过组合键解除隐私屏。点击“锁定”将重新开启隐私屏，并禁止本机使用组合键解锁。"))
          .fontSize(14).lineHeight(21).fontColor(MobileTheme.secondary).width('100%')
        if (this.state.privacyFeedback) { Text(this.state.privacyFeedback).fontSize(12).fontColor(MobileTheme.secondary) }
      }.padding(24).width('100%')
      this.Rule()
      Row() {
        Button(tr("确认"), { type: ButtonType.Normal }).fontSize(16).fontColor(MobileTheme.blue).height(50).layoutWeight(1)
          .backgroundColor(Color.White).onClick(() => this.dismissModal())
        Divider().vertical(true).height(50).strokeWidth(0.5).color(MobileTheme.line)
        Button(tr("锁定"), { type: ButtonType.Normal }).fontSize(16).fontWeight(600).fontColor(MobileTheme.blue).height(50).layoutWeight(1)
          .backgroundColor(Color.White).enabled(this.state.privacyCanLock).onClick(() => this.controller?.lockPrivacy())
      }.width('100%')
    } else if (this.state.modal === 'clipboard') {
      Column({ space: 16 }) {
        this.SheetHeader(tr("剪贴板"))
        Text(tr("发送文本后，点击“粘贴到远端”使用。")).fontSize(13).fontColor(MobileTheme.secondary).width('100%')
        TextArea({ text: this.state.clipboardText, placeholder: tr("要发送到远端的文本") }).fontSize(15).height(120).borderRadius(12).barState(BarState.Off)
          .backgroundColor(MobileTheme.background).onChange((value: string) => this.state.clipboardText = value)
        MobilePrimaryButton({ label: tr("发送文本"), action: () => this.controller?.clipboard() })
        Row({ space: 8 }) {
          Column() { this.Action(tr("粘贴到远端"), () => this.controller?.shortcut(86)) }.layoutWeight(1)
          Column() { this.Action(tr("复制远端文本"), () => this.controller?.copyRemote()) }.layoutWeight(1)
        }.width('100%')
      }.padding(20).width('100%')
    } else if (this.state.modal === 'displays') {
      Column({ space: 12 }) {
        this.SheetHeader(tr("显示器"))
        if (!this.state.displays.length) { this.Footer(tr("等待远端屏幕信息")) }
        ForEach(this.state.displays, (screen: RemoteDisplay, index: number) => {
          Button({ type: ButtonType.Normal }) {
            Row({ space: 12 }) {
              MobileIcon({ name: 'display', iconExtent: 22, color: index === this.state.selectedDisplay ? MobileTheme.blue : MobileTheme.secondary })
              Text(`${index + 1} · ${screen.name || '显示器'}`).fontSize(16).fontColor(MobileTheme.ink).layoutWeight(1).textAlign(TextAlign.Start)
              if (index === this.state.selectedDisplay) { MobileIcon({ name: 'check', iconExtent: 20, color: MobileTheme.blue }) }
            }.padding(14).width('100%')
          }.width('100%').padding(0).backgroundColor(index === this.state.selectedDisplay ? '#EDF4FC' : MobileTheme.background).borderRadius(12)
            .onClick(() => this.controller?.selectDisplay(index))
        })
      }.padding(20).width('100%')
    } else if (this.state.modal === 'recentActions') {
      Text(this.state.recent.find((d: RecentDevice) => d.id === this.deleteId)?.name || this.formatId(this.deleteId))
        .fontSize(17).fontWeight(600).fontColor(MobileTheme.ink).padding(20).width('100%').textAlign(TextAlign.Center)
      this.Rule()
      this.Action(tr("连接"), () => { this.dismissModal(); this.controller?.requestConnection(this.deleteId); })
      this.Rule()
      this.Action(tr("删除记录"), () => this.state.modal = 'delete', true)
      this.Rule()
      this.Action(tr("取消"), () => this.dismissModal())
    } else {
      Column({ space: 12 }) {
        Text(this.state.modal === 'notice' ? tr("连接提示") : this.state.modal === 'upgrade' ? tr("升级提示") : this.state.modal === 'disconnect' ? tr("断开远程连接？") :
          this.state.modal === 'revoke' ? tr("撤回联网授权？") : this.state.modal === 'previews' ? tr("保存远程画面预览？") : this.state.modal === 'clearPreviews' ? tr("清除预览图？") : this.state.modal === 'deleteAnnouncement' ? tr("删除公告？") : tr("删除记录？"))
          .fontSize(20).fontWeight(600).fontColor(MobileTheme.ink).textAlign(TextAlign.Center).width('100%')
        Text(this.state.modal === 'notice' || this.state.modal === 'upgrade' ? this.state.notice : this.state.modal === 'disconnect' ? tr("断开后将返回首页。") :
          this.state.modal === 'previews' ? tr("预览可能包含远程设备上的敏感内容。仅保存在本机，从下一次连接起生效，可随时关闭并清除。") :
          this.state.modal === 'clearPreviews' ? tr("将清除此设备保存的所有远程画面预览，保留连接记录和访问密码。") :
          this.state.modal === 'revoke' ? tr("将停止远程连接、关闭画面预览保存并清除已有预览。连接记录和密码会保留，重新连接前需再次授权。") :
          this.state.modal === 'delete' ? tr("同时删除该设备保存的密码和预览。") : tr("此公告将从列表中移除。"))
          .fontSize(14).lineHeight(21).fontColor(MobileTheme.secondary).textAlign(TextAlign.Center).width('100%')
      }.padding(24).width('100%')
      this.Rule()
      if (this.state.modal === 'notice' || this.state.modal === 'upgrade') { this.Action(tr("知道了"), () => this.dismissModal()) }
      else {
        Row() {
          Button(tr("取消"), { type: ButtonType.Normal }).fontSize(16).fontColor(MobileTheme.blue).height(50).layoutWeight(1)
            .backgroundColor(Color.White).onClick(() => this.dismissModal())
          Divider().vertical(true).height(50).strokeWidth(0.5).color(MobileTheme.line)
          Button(this.state.modal === 'previews' ? tr("开启") : this.state.modal === 'disconnect' ? tr("断开连接") : this.state.modal === 'revoke' ? tr("撤回授权") : this.state.modal === 'clearPreviews' ? tr("清除") : tr("删除"), { type: ButtonType.Normal })
            .fontSize(16).fontWeight(600).fontColor(this.state.modal === 'previews' ? MobileTheme.blue : MobileTheme.red).height(50).layoutWeight(1)
            .backgroundColor(Color.White).onClick(() => this.confirmModal())
        }.width('100%')
      }
    }
  }
  @Builder
  Modal() {
    Stack({ alignContent: this.viewWidth < 600 && (this.state.modal === 'password' || this.state.modal === 'consent') ? Alignment.Bottom : Alignment.Center }) {
      Column() {}.width('100%').height('100%').backgroundColor(this.state.modal === 'connecting' ? '#14000000' : '#38000000')
        .onClick(() => { if (this.state.modal !== 'consent' && this.state.modal !== 'connecting') this.dismissModal(); })
      if (this.state.modal === 'password') { this.PasswordSheet() }
      else if (this.state.modal === 'consent') { this.ConsentSheet() }
      else {
        Scroll() { Column() { this.DialogBody() }.width('100%') }
          .width(Math.min(this.state.modal === 'connecting' ? 292 : 420, this.viewWidth - 40)).constraintSize({ maxHeight: this.viewHeight * 0.9 })
          .backgroundColor(Color.White).borderRadius(24).clip(true).scrollBar(BarState.Off).shadow({ radius: 28, color: '#18000000', offsetY: 8 })
      }
    }.width('100%').height('100%').transition(TransitionEffect.OPACITY.animation({ duration: 160 }))
  }
  build() {
    Stack({ alignContent: Alignment.Center }) {
      if (this.state.page === 'session' && this.state.ready) {
        MobileRemoteView({ state: this.state, controller: this.controller }).transition(TransitionEffect.OPACITY.animation({ duration: 200 }))
      } else if (this.state.page === 'home') { this.Home() }
      else {
        Column() {
          Row({ space: 0 }) {
            MobileIconButton({ icon: 'back', label: tr("返回"), iconExtent: 44, iconSize: 22, action: () => this.back() })
            Text(this.title()).fontSize(17).fontWeight(600).fontColor(MobileTheme.ink).layoutWeight(1).textAlign(TextAlign.Center)
              .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
            if (this.state.page === 'announcements') {
              MobileIconButton({ icon: 'refresh', label: tr("刷新公告"), iconExtent: 44, iconSize: 22, action: () => this.controller?.inbox.refresh() })
            } else { Blank().width(44) }
          }.width('100%').constraintSize({ maxWidth: 720 }).height(56).padding({ left: 12, right: 12 })
          if (this.state.page === 'announcements') {
            Refresh({ refreshing: this.refreshing }) {
              Scroll() { Column() { this.Content() }.width('100%').constraintSize({ maxWidth: 720 }).padding({ left: 20, right: 20, top: 16, bottom: 28 }) }
                .width('100%').height('100%').align(Alignment.Top).scrollBar(BarState.Off)
            }.layoutWeight(1).width('100%').onRefreshing(() => { this.controller?.inbox.refresh(); this.refreshing = false; })
          } else {
            Scroll() { Column() { this.Content() }.width('100%').constraintSize({ maxWidth: 720 }).padding({ left: 20, right: 20, top: 16, bottom: 28 }) }
              .layoutWeight(1).width('100%').align(Alignment.Top).scrollBar(BarState.Off)
          }
        }.width('100%').height('100%').transition(TransitionEffect.asymmetric(TransitionEffect.move(TransitionEdge.END), TransitionEffect.move(TransitionEdge.END)).animation({ duration: 200 }))
      }
      if (this.state.modal) { this.Modal() }
    }.width('100%').height('100%').backgroundColor(MobileTheme.background)
      .onAreaChange((_old: Area, next: Area) => { this.viewWidth = Number(next.width); this.viewHeight = Number(next.height); })
  }
}
