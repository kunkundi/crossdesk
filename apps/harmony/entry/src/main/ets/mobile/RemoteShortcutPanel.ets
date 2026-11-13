import { tr } from './Localization';
import { MobileTheme, MobileSegment } from './MobileWidgets';
import { RemoteShortcut, RemoteShortcuts } from './RemoteShortcuts';

@Component
export struct RemoteShortcutPanel {
  store?: RemoteShortcuts;
  @Prop @Watch('reload') platform: string = 'unknown';
  @Prop @Watch('reload') revision: number = 0;
  send: (item: RemoteShortcut) => void = () => {};
  manage: () => void = () => {};
  changed: () => void = () => {};
  @State private group: number = 0;
  @State private page: number = 0;
  @State private desktop: string = '';
  @State private items: RemoteShortcut[] = [];
  aboutToAppear(): void { this.reload(); }
  private reload(): void { this.desktop = this.store?.desktop() ?? ''; this.items = this.store?.items(this.platform) ?? []; this.page = 0; }
  private filtered(): RemoteShortcut[] { return this.items.filter((item: RemoteShortcut) => item.group === ['common', 'keys', 'system', 'mine'][this.group]); }
  build() {
    Column({ space: 6 }) {
      MobileSegment({ labels: [tr('常用'), tr('按键'), tr('系统'), tr('我的')], selection: this.group,
        changed: (index: number) => { this.group = index; this.page = 0; } })
      if (this.group === 2 && this.platform === 'linux') {
        Select([{ value: tr('选择桌面方案') }, { value: 'GNOME' }, { value: 'KDE Plasma' }])
          .selected(this.desktop === 'gnome' ? 1 : this.desktop === 'kde' ? 2 : 0)
          .value(this.desktop === 'gnome' ? 'GNOME' : this.desktop === 'kde' ? 'KDE Plasma' : tr('选择桌面方案'))
          .font({ size: 12 }).height(32).onSelect((index: number) => { this.store?.setDesktop(['', 'gnome', 'kde'][index]); this.reload(); this.changed(); })
      }
      Scroll() {
        Column({ space: 4 }) {
          if (!this.filtered().length) {
            Text(tr(this.platform === 'unknown' ? '等待远端平台信息' : this.group === 2 && this.platform === 'linux' ? '选择桌面方案以加载系统预设' : '还没有自定义快捷键'))
              .fontSize(12).fontColor(MobileTheme.secondary).padding(12)
          }
          GridRow({ columns: 3, gutter: 4 }) {
            ForEach(this.filtered().slice(this.page * 6, this.page * 6 + 6), (item: RemoteShortcut) => {
              GridCol() {
                Column({ space: 3 }) {
                  Text(item.title()).fontSize(12).maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
                  Text(item.chord()).fontSize(10).fontColor(MobileTheme.secondary).maxLines(1)
                }.width('100%').height(48).justifyContent(FlexAlign.Center).borderRadius(7).backgroundColor(Color.White)
                  .accessibilityLevel('yes').accessibilityText(`${item.title()}, ${item.chord()}`).onClick(() => this.send(item))
              }
            }, (item: RemoteShortcut) => item.id)
          }
        }.width('100%')
      }.width('100%').layoutWeight(1)
      Row() {
        Button('‹').width(40).height(36).enabled(this.page > 0).accessibilityText(tr('上一页')).onClick(() => this.page--)
        Text(`${this.page + 1}/${Math.max(1, Math.ceil(this.filtered().length / 6))}`).fontSize(11).width(38).textAlign(TextAlign.Center)
        Button('›').width(40).height(36).enabled((this.page + 1) * 6 < this.filtered().length).accessibilityText(tr('下一页')).onClick(() => this.page++)
        Blank()
        Button(tr('管理与自定义')).fontSize(12).height(36).enabled(this.platform !== 'unknown').onClick(this.manage)
      }.width('100%')
    }.width('100%').height('100%').padding(3)
  }
}

@Component
export struct RemoteShortcutManager {
  store?: RemoteShortcuts;
  platform: string = 'unknown';
  close: () => void = () => {};
  changed: () => void = () => {};
  @State private items: RemoteShortcut[] = [];
  @State private pinned: RemoteShortcut[] = [];
  @State private editing: boolean = false;
  @State private itemId: string = '';
  @State private name: string = '';
  @State private target: string = 'windows';
  @State private mainKey: number = 67;
  @State private modifiers: number[] = [];
  @State private pin: boolean = true;
  @State private error: string = '';
  aboutToAppear(): void { this.reload(); }
  private reload(): void { this.items = this.store?.items(this.platform) ?? []; this.pinned = this.store?.pinned(this.platform) ?? []; this.changed(); }
  private edit(item?: RemoteShortcut): void {
    this.itemId = item?.id ?? ''; this.name = item?.name ?? ''; this.target = item?.platform ?? this.platform;
    this.mainKey = item?.key ?? 67; this.modifiers = item?.modifiers.slice() ?? [this.target === 'macos' ? 91 : 17];
    this.pin = item ? (this.store?.pins(item.platform).includes(item.id) ?? false) : (this.store?.pins(this.target).length ?? 0) < 8;
    this.error = ''; this.editing = true;
  }
  private setPinned(item: RemoteShortcut, add: boolean): void {
    const values = this.store?.pins(this.platform) ?? [];
    if (add && values.length >= 8) { this.error = tr('最多固定 8 个快捷键'); return; }
    this.store?.setPins(this.platform, add ? values.concat([item.id]) : values.filter((id: string) => id !== item.id)); this.error = ''; this.reload();
  }
  private moveUp(item: RemoteShortcut): void {
    const values = this.store?.pins(this.platform) ?? [], index = values.indexOf(item.id);
    if (index > 0) { values.splice(index, 1); values.splice(index - 1, 0, item.id); this.store?.setPins(this.platform, values); this.reload(); }
  }
  private save(): void {
    this.error = this.store?.save(this.itemId, this.name, this.target, this.mainKey, this.modifiers, this.pin) ?? '';
    if (!this.error) { this.editing = false; this.reload(); }
  }
  @Builder
  Item(item: RemoteShortcut, pinned: boolean) {
    Column({ space: 4 }) {
      Row() {
        Text(item.title()).fontSize(15).fontColor(MobileTheme.ink).layoutWeight(1).maxLines(2)
        Text(item.chord()).fontSize(12).fontColor(MobileTheme.secondary).maxLines(2)
      }.width('100%')
      Row({ space: 6 }) {
        if (pinned) {
          Button(tr('向上移动')).fontSize(12).height(36).enabled(this.pinned[0]?.id !== item.id).onClick(() => this.moveUp(item))
          Button(tr('移除')).fontSize(12).height(36).onClick(() => this.setPinned(item, false))
        } else {
          Button(tr('添加到常用栏')).fontSize(12).height(36).enabled(!this.pinned.some((value: RemoteShortcut) => value.id === item.id)).onClick(() => this.setPinned(item, true))
          if (item.custom) {
            Button(tr('编辑')).fontSize(12).height(36).onClick(() => this.edit(item))
            Button(tr('删除')).fontSize(12).height(36).backgroundColor(MobileTheme.red).onClick(() => { this.store?.remove(item.id); this.reload(); })
          }
        }
      }.width('100%')
    }.padding(12).width('100%').borderRadius(10).backgroundColor(Color.White)
  }
  build() {
    Column({ space: 12 }) {
      Row() {
        Button(tr(this.editing ? '取消' : '完成')).fontSize(14).height(44).onClick(() => { if (this.editing) this.editing = false; else this.close(); })
        Text(tr(this.editing ? this.itemId ? '编辑快捷键' : '新建快捷键' : '管理快捷键')).fontSize(17).fontWeight(600).layoutWeight(1).textAlign(TextAlign.Center)
        Button(tr(this.editing ? '保存' : '新建')).fontSize(14).height(44).onClick(() => { if (this.editing) this.save(); else this.edit(); })
      }.width('100%')
      Text(tr('本地编辑，输入不会发送到远端')).fontSize(12).fontColor(MobileTheme.secondary)
      if (this.error) { Text(this.error).fontSize(13).fontColor(MobileTheme.red) }
      Scroll() {
        Column({ space: 12 }) {
          if (this.editing) {
            TextInput({ placeholder: tr('名称'), text: this.name }).maxLength(64).onChange((value: string) => this.name = value)
            Text(tr('适用远端')).width('100%').fontSize(13)
            Select(RemoteShortcuts.platforms.map((os: string): SelectOption => ({ value: RemoteShortcuts.platformName(os) })))
              .selected(RemoteShortcuts.platforms.indexOf(this.target)).value(RemoteShortcuts.platformName(this.target))
              .width('100%').onSelect((index: number) => this.target = RemoteShortcuts.platforms[index])
            Text(tr('主按键')).width('100%').fontSize(13)
            Select(RemoteShortcuts.keys().map((key: number): SelectOption => ({ value: RemoteShortcuts.keyName(key, this.target) })))
              .selected(RemoteShortcuts.keys().indexOf(this.mainKey)).value(RemoteShortcuts.keyName(this.mainKey, this.target))
              .width('100%').onSelect((index: number) => this.mainKey = RemoteShortcuts.keys()[index])
            Text(tr('修饰键')).width('100%').fontSize(13)
            Row({ space: 6 }) {
              ForEach(RemoteShortcuts.modifiers, (key: number) => {
                Button(RemoteShortcuts.modifierName(key, this.target)).layoutWeight(1).height(44)
                  .backgroundColor(this.modifiers.includes(key) ? MobileTheme.blue : MobileTheme.secondary)
                  .accessibilitySelected(this.modifiers.includes(key)).onClick(() => this.modifiers = this.modifiers.includes(key) ? this.modifiers.filter((value: number) => value !== key) : RemoteShortcuts.normalize(this.modifiers.concat([key])))
              }, (key: number) => `${key}-${this.target}`)
            }.width('100%')
            Text(`${tr('发送组合')}: ${new RemoteShortcut('', '', this.target, 'mine', this.mainKey, this.modifiers).chord()}`).fontSize(16).padding(12)
            Row() {
              Text(tr('固定到键盘上方')).layoutWeight(1).fontSize(14)
              Toggle({ type: ToggleType.Switch, isOn: this.pin }).onChange((value: boolean) => this.pin = value)
            }.width('100%')
          } else {
            Text(`${RemoteShortcuts.platformName(this.platform)} · ${tr('键盘上方常用栏')} (${this.pinned.length}/8)`).fontSize(14).width('100%')
            ForEach(this.pinned, (item: RemoteShortcut) => { this.Item(item, true) }, (item: RemoteShortcut) => item.id)
            Text(tr('快捷键')).fontSize(14).width('100%')
            ForEach(this.items, (item: RemoteShortcut) => { this.Item(item, false) }, (item: RemoteShortcut) => item.id)
          }
        }.width('100%').padding({ bottom: 24 })
      }.layoutWeight(1).width('100%')
    }.padding(16).width('100%').height('100%').backgroundColor(MobileTheme.background)
      .onKeyEvent((event: KeyEvent) => { if (event.keyCode === 2 && event.type === KeyType.Down) { if (this.editing) this.editing = false; else this.close(); return true; } return false; })
  }
}
