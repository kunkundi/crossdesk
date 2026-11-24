import { tr } from './Localization';
import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { asset } from '@kit.AssetStoreKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { util } from '@kit.ArkTS';
import { fileIo as fs } from '@kit.CoreFileKit';
import { image } from '@kit.ImageKit';
import bridge from 'libcrossdesk.so';

export interface RecentDevice {
  id: string; name: string; alias: string; groups: string[];
  platform: string; remember: boolean; preview: string; wallpaper?: string;
}
export class MobileStore {
  private static trustLoading?: Promise<string>;
  private data: preferences.Preferences;
  readonly directory: string;
  readonly previewDirectory: string;
  private previewEpoch: number = 0;
  private wallpaperEpoch: number = 0;
  private wallpaperWrites: Promise<void> = Promise.resolve();
  private readonly wallpaperDirectory: string;
  constructor(context: common.UIAbilityContext) {
    this.data = preferences.getPreferencesSync(context, { name: 'crossdesk_mobile' });
    this.directory = `${context.filesDir}/mobile`;
    this.previewDirectory = `${context.cacheDir}/mobile-previews`;
    this.wallpaperDirectory = `${context.cacheDir}/mobile-wallpapers`;
    try { fs.mkdirSync(this.previewDirectory, true); } catch {}
    try { fs.mkdirSync(this.wallpaperDirectory, true); } catch {}
    for (const name of ['', '/outgoing', '/received']) {
      try { fs.mkdirSync(this.directory + name, true); } catch { /* Existing directories are reusable. */ }
    }
  }
  text(key: string, fallback: string = ''): string { const value = this.data.getSync(key, fallback); return typeof value === 'string' ? value : fallback; }
  flag(key: string, fallback: boolean = false): boolean { const value = this.data.getSync(key, fallback); return typeof value === 'boolean' ? value : fallback; }
  number(key: string, fallback: number): number { const value = this.data.getSync(key, fallback); return typeof value === 'number' ? value : fallback; }
  put(key: string, value: string | number | boolean): void { this.data.putSync(key, value); this.data.flushSync(); }
  static scope(server: string, port: number): string { return `${server.trim().toLowerCase().replace(/\.$/, '')}:${port}`; }
  static async hash(value: string): Promise<string> {
    const md = cryptoFramework.createMd('SHA256'); await md.update({ data: new util.TextEncoder().encodeInto(value) });
    const digest = await md.digest(); return Array.from(digest.data).map((b: number) => b.toString(16).padStart(2, '0')).join('');
  }
  private async query(key: string): Promise<asset.AssetMap> {
    const query: asset.AssetMap = new Map();
    query.set(asset.Tag.ALIAS, new util.TextEncoder().encodeInto(await MobileStore.hash(`crossdesk-mobile:${key}`)));
    return query;
  }
  async secret(key: string): Promise<string> {
    const query = await this.query(key); query.set(asset.Tag.RETURN_TYPE, asset.ReturnType.ALL);
    try {
      const found = await asset.query(query); const value = found.length ? found[0].get(asset.Tag.SECRET) : undefined;
      return value instanceof Uint8Array ? util.TextDecoder.create('utf-8').decodeWithStream(value) : '';
    } catch (error) {
      if ((error as ErrorWithCode).code === asset.ErrorCode.NOT_FOUND) return '';
      throw new Error(tr("安全存储不可用，请解锁设备后重试"));
    }
  }
  async saveSecret(key: string, value: string): Promise<void> {
    if (!value) { await this.removeSecret(key); return; }
    const attributes = await this.query(key);
    attributes.set(asset.Tag.SECRET, new util.TextEncoder().encodeInto(value));
    attributes.set(asset.Tag.ACCESSIBILITY, asset.Accessibility.DEVICE_FIRST_UNLOCKED);
    attributes.set(asset.Tag.SYNC_TYPE, asset.SyncType.NEVER);
    attributes.set(asset.Tag.CONFLICT_RESOLUTION, asset.ConflictResolution.OVERWRITE);
    await asset.add(attributes);
  }
  async removeSecret(key: string): Promise<void> {
    try { await asset.remove(await this.query(key)); }
    catch (error) { if ((error as ErrorWithCode).code !== asset.ErrorCode.NOT_FOUND) throw new Error(tr("无法删除已保存的凭据")); }
  }
  async trustKey(): Promise<string> {
    // Serializes pairing export and reconnects, including overlapping page lifetimes.
    if (MobileStore.trustLoading) return MobileStore.trustLoading;
    const task = this.loadTrustKey(); MobileStore.trustLoading = task;
    try { return await task; }
    finally { if (MobileStore.trustLoading === task) MobileStore.trustLoading = undefined; }
  }
  private async loadTrustKey(): Promise<string> {
    let seed = await this.secret('device-trust:v1');
    if (!seed) {
      if (this.flag('deviceTrustInitialized')) throw new Error('Device signing identity missing');
      seed = bridge.mobileGenerateTrustKey();
      await this.saveSecret('device-trust:v1', seed);
    }
    // Validate persisted data without replacing a corrupt/unavailable identity.
    bridge.mobileTrustInfo(seed, '');
    this.put('deviceTrustInitialized', true);
    return seed;
  }
  recent(scope: string): RecentDevice[] {
    try {
      const entries = JSON.parse(this.text(`recent:${scope}`, '[]')) as RecentDevice[];
      return entries.filter((d: RecentDevice) => typeof d.id === 'string' && (/^[0-9]{9}$/.test(d.id) || (d.id.includes(':') && bridge.mobileDirectEndpoint(d.id) === d.id)) && typeof d.name === 'string').slice(0, 20)
        .map((d: RecentDevice) => {
          d.alias = typeof d.alias === 'string' ? d.alias : '';
          d.groups = Array.isArray(d.groups) ? d.groups : [];
          try { if (typeof d.preview !== 'string' || !d.preview.startsWith(`${this.previewDirectory}/`) || !fs.accessSync(d.preview)) d.preview = ''; }
          catch { d.preview = ''; }
          try {
            if (typeof d.wallpaper !== 'string' ||
                !d.wallpaper.startsWith(`${this.wallpaperDirectory}/`) || !fs.accessSync(d.wallpaper))
              d.wallpaper = '';
          } catch { d.wallpaper = ''; }
          return d;
        });
    } catch { return []; }
  }
  async saveRecent(scope: string, device: RecentDevice, password: string): Promise<void> {
    if (device.remember) await this.saveSecret(`password:${scope}:${device.id}`, password);
    else await this.removeSecret(`password:${scope}:${device.id}`);
    const old = this.recent(scope), previous = old.find((d: RecentDevice) => d.id === device.id);
    device.preview = previous?.preview ?? '';
    device.wallpaper = previous?.wallpaper ?? '';
    device.alias = previous?.alias ?? '';
    device.groups = previous?.groups ?? [];
    if (device.name === device.id && previous) device.name = previous.name;
    const next = [device, ...old.filter((d: RecentDevice) => d.id !== device.id)].slice(0, 20);
    this.put(`recent:${scope}`, JSON.stringify(next));
    for (const dropped of old.filter((d: RecentDevice) => !next.some((n: RecentDevice) => n.id === d.id))) {
      await this.removeSecret(`password:${scope}:${dropped.id}`); this.removePreview(dropped.preview);
      this.removeWallpaper(dropped.wallpaper ?? '');
    }
  }
  metadata(scope: string, id: string, name: string, platform: string): void {
    const entries = this.recent(scope);
    const device = entries.find((d: RecentDevice) => d.id === id);
    if (!device) return;
    if (name && name.trim()) device.name = name.trim();
    device.platform = platform;
    this.put(`recent:${scope}`, JSON.stringify(entries));
  }
  editRecent(scope: string, id: string, alias?: string, groups?: string[]): void {
    const entries = this.recent(scope);
    const device = entries.find((d: RecentDevice) => d.id === id);
    if (!device) return;
    if (alias !== undefined) device.alias = alias.trim();
    if (groups !== undefined) device.groups = Array.from(new Set<string>(groups)).sort();
    this.put(`recent:${scope}`, JSON.stringify(entries));
  }
  async delete(scope: string, id: string): Promise<void> {
    await this.removeSecret(`password:${scope}:${id}`);
    const old = this.recent(scope); this.put(`recent:${scope}`, JSON.stringify(old.filter((d: RecentDevice) => d.id !== id)));
    const removed = old.find((d: RecentDevice) => d.id === id);
    this.removePreview(removed?.preview ?? '');
    this.removeWallpaper(removed?.wallpaper ?? '');
  }
  private removePreview(path: string): void {
    if (!path.startsWith(`${this.previewDirectory}/`)) return;
    try { fs.unlinkSync(path); } catch { /* Already removed. */ }
  }
  clearPreviews(includingWallpapers: boolean = true): void {
    this.previewEpoch++;
    let names: string[] = [];
    try { names = fs.listFileSync(this.previewDirectory); } catch { /* The system may already have cleared the cache. */ }
    for (const name of names) this.removePreview(`${this.previewDirectory}/${name}`);
    if (includingWallpapers) {
      this.wallpaperEpoch++;
      try { names = fs.listFileSync(this.wallpaperDirectory); } catch { names = []; }
      for (const name of names) this.removeWallpaper(`${this.wallpaperDirectory}/${name}`);
    }
  }
  private removeWallpaper(path: string): void {
    if (!path.startsWith(`${this.wallpaperDirectory}/`)) return;
    try { fs.unlinkSync(path); } catch { /* Already removed. */ }
  }
  saveWallpaper(scope: string, id: string, encoded: string): Promise<void> {
    if (!this.flag('consent') || !encoded || encoded.length > 65536) return Promise.resolve();
    const epoch = this.wallpaperEpoch;
    const write = this.wallpaperWrites.then(() => this.writeWallpaper(scope, id, encoded, epoch));
    this.wallpaperWrites = write.catch(() => {});
    return write;
  }
  private async writeWallpaper(scope: string, id: string, encoded: string, epoch: number): Promise<void> {
    if (epoch !== this.wallpaperEpoch || !this.flag('consent')) return;
    const decoded = new util.Base64Helper().decodeSync(encoded);
    const bytes = new ArrayBuffer(decoded.length);
    new Uint8Array(bytes).set(decoded);
    const source = image.createImageSource(bytes);
    try {
      const size = (await source.getImageInfo()).size;
      if (size.width <= 0 || size.height <= 0 || size.width > 640 || size.height > 640) return;
    } finally {
      await source.release();
    }
    const key = await MobileStore.hash(JSON.stringify([scope, id]));
    if (epoch !== this.wallpaperEpoch || !this.flag('consent')) return;
    const entries = this.recent(scope), device = entries.find((d: RecentDevice) => d.id === id);
    if (!device) return;
    const destination = `${this.wallpaperDirectory}/${key}-${Date.now()}.jpg`;
    let temporary = `${destination}.tmp`;
    try {
      const output = fs.openSync(temporary, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
      try {
        if (fs.writeSync(output.fd, bytes) !== bytes.byteLength) return;
      } finally { fs.closeSync(output); }
      fs.renameSync(temporary, destination);
      const previous = device.wallpaper;
      device.wallpaper = destination;
      this.put(`recent:${scope}`, JSON.stringify(entries));
      temporary = '';
      if (previous && previous !== destination) this.removeWallpaper(previous);
    } finally {
      if (temporary) {
        this.removeWallpaper(temporary);
        this.removeWallpaper(destination);
      }
    }
  }
  discardRaw(path: string): void {
    if (!path.startsWith(`${this.previewDirectory}/preview-`) || !/^preview-[0-9]+-[0-9]+\.rgba$/.test(path.substring(this.previewDirectory.length + 1))) return;
    try { fs.unlinkSync(path); } catch { /* Already removed. */ }
  }
  discardOutgoing(path: string): void {
    if (!path.startsWith(`${this.directory}/outgoing/`) || path.substring(this.directory.length + 10).includes('/')) return;
    try { fs.unlinkSync(path); } catch { /* Already removed. */ }
  }
  async savePreview(scope: string, id: string, raw: string): Promise<void> {
    if (!raw.startsWith(`${this.previewDirectory}/preview-`) || !/^preview-[0-9]+-[0-9]+\.rgba$/.test(raw.substring(this.previewDirectory.length + 1))) return;
    const epoch = this.previewEpoch;
    let pixel: image.PixelMap | undefined;
    const packer = image.createImagePacker();
    let output: fs.File | undefined;
    let temporary = '';
    try {
      if (!this.flag('previews') || !this.flag('consent') || !this.recent(scope).some((d: RecentDevice) => d.id === id)) return;
      const file = fs.openSync(raw, fs.OpenMode.READ_ONLY); const bytes = new ArrayBuffer(640 * 360 * 4);
      try { if (fs.readSync(file.fd, bytes) !== bytes.byteLength) return; } finally { fs.closeSync(file); }
      pixel = await image.createPixelMap(bytes, { size: { width: 640, height: 360 }, pixelFormat: image.PixelMapFormat.RGBA_8888 });
      const destination = `${this.previewDirectory}/${await MobileStore.hash(`${scope}:${id}`)}.jpg`;
      temporary = `${destination}.tmp`;
      output = fs.openSync(temporary, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
      await packer.packToFile(pixel, output.fd, { format: 'image/jpeg', quality: 82 }); fs.closeSync(output); output = undefined;
      if (epoch !== this.previewEpoch || !this.flag('previews') || !this.flag('consent')) return;
      const entries = this.recent(scope), target = entries.find((d: RecentDevice) => d.id === id); if (!target) return;
      fs.renameSync(temporary, destination); temporary = ''; target.preview = destination;
      this.put(`recent:${scope}`, JSON.stringify(entries));
    } finally {
      if (output) fs.closeSync(output); if (pixel) pixel.release(); packer.release();
      try { fs.unlinkSync(raw); } catch { /* Revocation may already have removed it. */ }
      if (temporary) try { fs.unlinkSync(temporary); } catch { /* Best effort cleanup. */ }
    }
  }
}
interface ErrorWithCode { code: number; }
