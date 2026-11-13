import { MobileStore } from './MobileStore';
import { tr } from './Localization';

export class RemoteShortcut {
  id: string; name: string; platform: string; group: string; key: number; modifiers: number[];
  service: boolean; custom: boolean;
  constructor(id: string, name: string, platform: string, group: string, key: number, modifiers: number[] = [], service: boolean = false, custom: boolean = false) {
    this.id = id; this.name = name; this.platform = platform; this.group = group; this.key = key;
    this.modifiers = modifiers.slice(); this.service = service; this.custom = custom;
  }
  title(): string { return this.custom ? this.name : tr(this.name); }
  chord(): string { return this.modifiers.map((key: number) => RemoteShortcuts.modifierName(key, this.platform)).concat([RemoteShortcuts.keyName(this.key, this.platform)]).join(' + '); }
}

export class RemoteShortcuts {
  static readonly platforms: string[] = ['windows', 'macos', 'linux'];
  static readonly modifiers: number[] = [17, 16, 18, 91];
  private store: MobileStore;
  private prefix: string = 'remote_shortcuts_v1:';
  constructor(store: MobileStore) { this.store = store; }
  static platform(value: string): string { return RemoteShortcuts.platforms.includes(value) ? value : 'unknown'; }
  static platformName(value: string): string { return value === 'windows' ? 'Windows' : value === 'macos' ? 'macOS' : value === 'linux' ? 'Linux' : tr('未知'); }
  static modifierName(key: number, platform: string): string {
    return key === 17 ? 'Ctrl' : key === 16 ? 'Shift' : key === 18 ? (platform === 'macos' ? 'Option' : 'Alt') : key === 91 ? (platform === 'macos' ? 'Cmd' : platform === 'linux' ? 'Super' : 'Win') : '';
  }
  static keyName(key: number, platform: string): string {
    if (key >= 65 && key <= 90 || key >= 48 && key <= 57) return String.fromCharCode(key);
    if (key >= 112 && key <= 123) return `F${key - 111}`;
    switch (key) {
      case 8: return platform === 'macos' ? 'Delete ⌫' : 'Backspace'; case 9: return 'Tab'; case 13: return 'Enter';
      case 27: return 'Esc'; case 32: return 'Space'; case 33: return 'PageUp'; case 34: return 'PageDown';
      case 35: return 'End'; case 36: return 'Home'; case 37: return '←'; case 38: return '↑'; case 39: return '→';
      case 40: return '↓'; case 45: return 'Insert'; case 46: return platform === 'macos' ? 'Delete ⌦' : 'Delete'; default: return '';
    }
  }
  static keys(): number[] {
    const keys: number[] = []; for (let i = 65; i <= 90; i++) keys.push(i); for (let i = 48; i <= 57; i++) keys.push(i);
    for (let i = 112; i <= 123; i++) keys.push(i);
    return keys.concat([27, 9, 13, 32, 8, 46, 45, 36, 35, 33, 34, 37, 38, 39, 40]);
  }
  static normalize(values: number[]): number[] { return RemoteShortcuts.modifiers.filter((key: number) => values.includes(key)); }
  desktop(): string { const value = this.store.text(this.prefix + 'linux_desktop'); return ['gnome', 'kde'].includes(value) ? value : ''; }
  setDesktop(value: string): void { this.store.put(this.prefix + 'linux_desktop', ['gnome', 'kde'].includes(value) ? value : ''); }
  private preset(id: string, name: string, platform: string, group: string, key: number, modifiers: number[] = []): RemoteShortcut { return new RemoteShortcut(id, name, platform, group, key, modifiers); }
  items(remote: string): RemoteShortcut[] {
    const os = RemoteShortcuts.platform(remote); let result: RemoteShortcut[] = [];
    if (os !== 'unknown') {
      const ids = ['copy', 'paste', 'cut', 'all', 'undo', 'save'], names = ['复制', '粘贴', '剪切', '全选', '撤销', '保存'], keys = [67, 86, 88, 65, 90, 83];
      for (let i = 0; i < ids.length; i++) result.push(this.preset(ids[i], names[i], os, 'common', keys[i], [os === 'macos' ? 91 : 17]));
      if (os === 'linux') result = result.concat([this.preset('term-copy', '终端复制', os, 'common', 67, [17, 16]), this.preset('term-paste', '终端粘贴', os, 'common', 86, [17, 16])]);
    }
    const keys = [27, 9, 13, 8, 46, 36, 35, 33, 34, 37, 38, 39, 40]; for (let i = 112; i <= 123; i++) keys.push(i);
    keys.forEach((key: number) => result.push(this.preset(key === 27 ? 'esc' : key === 9 ? 'tab' : `key-${key}`, RemoteShortcuts.keyName(key, os), os, 'keys', key)));
    if (os === 'windows') {
      result = result.concat([this.preset('switch', '切换窗口', os, 'system', 9, [18]), this.preset('desktop', '显示桌面', os, 'system', 68, [91]), this.preset('task', '任务管理器', os, 'system', 27, [17, 16]), new RemoteShortcut('sas', '安全选项', os, 'system', 46, [17, 18], true)]);
    } else if (os === 'macos') {
      result = result.concat([this.preset('switch', '切换应用', os, 'system', 9, [91]), this.preset('search', '聚焦搜索', os, 'system', 32, [91]), this.preset('forcequit', '强制退出面板', os, 'system', 27, [18, 91])]);
    } else if (os === 'linux' && this.desktop()) {
      result.push(this.preset('switch', this.desktop() === 'gnome' ? '切换应用' : '切换窗口', os, 'system', 9, [this.desktop() === 'gnome' ? 91 : 18]));
      result.push(this.preset('run', '运行窗口', os, 'system', 113, [18]));
      result.push(this.desktop() === 'gnome' ? this.preset('apps', '应用列表', os, 'system', 65, [91]) : this.preset('window-menu', '窗口菜单', os, 'system', 114, [18]));
    }
    return result.concat(this.custom().filter((item: RemoteShortcut) => item.platform === os));
  }
  custom(): RemoteShortcut[] {
    try {
      const raw = JSON.parse(this.store.text(this.prefix + 'custom', '[]')) as RemoteShortcut[];
      if (!Array.isArray(raw)) return [];
      const seen: string[] = [], result: RemoteShortcut[] = [];
      raw.forEach((item: RemoteShortcut) => {
        if (!item || typeof item.id !== 'string' || !item.id.startsWith('custom-') || seen.includes(item.id) || typeof item.name !== 'string' || !item.name.trim() || item.name.length > 64 || !RemoteShortcuts.platforms.includes(item.platform) || !RemoteShortcuts.keys().includes(item.key) || !Array.isArray(item.modifiers) || item.modifiers.length > 4 || !item.modifiers.every((key: number) => RemoteShortcuts.modifiers.includes(key))) return;
        if (item.platform === 'windows' && item.key === 46 && item.modifiers.includes(17) && item.modifiers.includes(18)) return;
        seen.push(item.id); result.push(new RemoteShortcut(item.id, item.name, item.platform, 'mine', item.key, RemoteShortcuts.normalize(item.modifiers), false, true));
      });
      return result.slice(0, 16);
    } catch { return []; }
  }
  pins(platform: string): string[] {
    const raw = this.store.text(this.prefix + 'pins_' + RemoteShortcuts.platform(platform), '');
    const available = this.items(platform).map((item: RemoteShortcut) => item.id);
    let values: string[] = ['esc', 'tab', 'copy', 'paste'];
    try { const parsed = JSON.parse(raw) as string[]; if (Array.isArray(parsed)) values = parsed; } catch {}
    return values.filter((id: string, index: number) => typeof id === 'string' && available.includes(id) && values.indexOf(id) === index).slice(0, 8);
  }
  setPins(platform: string, values: string[]): void { this.store.put(this.prefix + 'pins_' + RemoteShortcuts.platform(platform), JSON.stringify(values.slice(0, 8))); }
  pinned(platform: string): RemoteShortcut[] {
    const items = this.items(platform), result: RemoteShortcut[] = [];
    this.pins(platform).forEach((id: string) => { const item = items.find((value: RemoteShortcut) => value.id === id); if (item) result.push(item); });
    return result;
  }
  remove(id: string): void {
    this.store.put(this.prefix + 'custom', JSON.stringify(this.custom().filter((item: RemoteShortcut) => item.id !== id)));
    RemoteShortcuts.platforms.forEach((platform: string) => this.setPins(platform, this.pins(platform).filter((value: string) => value !== id)));
  }
  save(id: string, name: string, platform: string, key: number, modifiers: number[], pin: boolean): string {
    name = name.trim(); if (!name || name.length > 64) return tr('请输入有效的快捷键名称');
    if (!RemoteShortcuts.platforms.includes(platform) || !RemoteShortcuts.keys().includes(key)) return tr('请选择有效的按键');
    const normalized = RemoteShortcuts.normalize(modifiers);
    if (platform === 'windows' && key === 46 && normalized.includes(17) && normalized.includes(18)) return tr('请使用系统预设中的安全选项');
    const old = this.custom();
    if (old.some((item: RemoteShortcut) => item.id !== id && item.platform === platform && item.key === key && item.modifiers.join(',') === normalized.join(','))) return tr('已有相同组合的快捷键');
    if (!old.some((item: RemoteShortcut) => item.id === id) && old.length >= 16) return tr('最多保存 16 个自定义快捷键');
    if (pin && !this.pins(platform).includes(id) && this.pins(platform).length >= 8) return tr('最多固定 8 个快捷键');
    const itemId = id || `custom-${Date.now()}-${Math.floor(Math.random() * 1000000000)}`;
    const values = old.filter((item: RemoteShortcut) => item.id !== itemId); values.push(new RemoteShortcut(itemId, name, platform, 'mine', key, normalized, false, true));
    this.store.put(this.prefix + 'custom', JSON.stringify(values));
    RemoteShortcuts.platforms.forEach((target: string) => { const list = this.pins(target), index = list.indexOf(itemId), selected = list.filter((value: string) => value !== itemId); if (target === platform && pin) selected.splice(index >= 0 ? Math.min(index, selected.length) : selected.length, 0, itemId); this.setPins(target, selected); });
    return '';
  }
}
