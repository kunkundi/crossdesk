import { tr } from './Localization';
// Platform-independent controller rules ported from the Android client.
// Keep gesture geometry, acknowledgement ordering and expiry behavior identical.
export function clamp(value: number, low: number, high: number): number {
  return Math.max(low, Math.min(high, value));
}
export class RemoteViewport {
  private static readonly POINTER_INSET: number = 18;
  private containerWidth: number = 0;
  private containerHeight: number = 0;
  private followingPointer: boolean = false;
  baseWidth: number = 1;
  baseHeight: number = 1;
  baseLeft: number = 0;
  baseTop: number = 0;
  scale: number = 1;
  offsetX: number = 0;
  offsetY: number = 0;
  fit(width: number, height: number, videoWidth: number, videoHeight: number): void {
    if (Math.min(width, height, videoWidth, videoHeight) <= 0) return;
    this.containerWidth = width; this.containerHeight = height;
    const scale = Math.min(width / videoWidth, height / videoHeight);
    this.baseWidth = Math.max(1, videoWidth * scale); this.baseHeight = Math.max(1, videoHeight * scale);
    this.baseLeft = (width - this.baseWidth) / 2; this.baseTop = (height - this.baseHeight) / 2;
    this.constrain();
  }
  width(): number { return this.baseWidth * this.scale; }
  height(): number { return this.baseHeight * this.scale; }
  left(): number { return this.baseLeft + this.baseWidth * (1 - this.scale) / 2 + this.offsetX; }
  top(): number { return this.baseTop + this.baseHeight * (1 - this.scale) / 2 + this.offsetY; }
  contains(x: number, y: number): boolean { return x >= this.left() && x < this.left() + this.width() && y >= this.top() && y < this.top() + this.height(); }
  x(value: number): number { return clamp((value - this.left()) / this.width(), 0, 1); }
  y(value: number): number { return clamp((value - this.top()) / this.height(), 0, 1); }
  zoom(factor: number, x: number, y: number): void {
    if (!Number.isFinite(factor) || factor <= 0) return;
    const next = clamp(this.scale * factor, 1, 10), ratio = next / this.scale;
    if (next === 1) { this.reset(); return; }
    const fx = x - this.baseLeft - this.baseWidth / 2, fy = y - this.baseTop - this.baseHeight / 2;
    this.offsetX = fx - (fx - this.offsetX) * ratio; this.offsetY = fy - (fy - this.offsetY) * ratio;
    this.scale = next; this.constrain();
  }
  pan(x: number, y: number): void { this.offsetX += x; this.offsetY += y; this.constrain(); }
  followPointer(x: number, y: number): void {
    if (this.scale <= 1.001 || !this.containerWidth || !this.containerHeight) return;
    const dx = this.pointerPan(this.left() + x * this.width(), this.containerWidth);
    const dy = this.pointerPan(this.top() + y * this.height(), this.containerHeight);
    if (dx === 0 && dy === 0) return;
    this.followingPointer = true;
    this.pan(dx, dy);
  }
  private pointerPan(position: number, length: number): number {
    const margin = Math.min(RemoteViewport.POINTER_INSET, length / 2);
    return clamp(position, margin, length - margin) - position;
  }
  reset(): void { this.scale = 1; this.offsetX = this.offsetY = 0; this.followingPointer = false; }
  finish(): void { if (this.scale <= 1.001) this.reset(); }
  private constrain(): void {
    const inset = this.followingPointer ? RemoteViewport.POINTER_INSET : 0;
    const maximumX = this.baseWidth * (this.scale - 1) / 2 + inset;
    const maximumY = this.baseHeight * (this.scale - 1) / 2 + inset;
    this.offsetX = clamp(this.offsetX, -maximumX, maximumX);
    this.offsetY = clamp(this.offsetY, -maximumY, maximumY);
  }
}
export class MouseInput {
  x: number = 0.5;
  y: number = 0.5;
  gesture: boolean = false;
  left: boolean = false;
  right: boolean = false;
  revision: number = 0;
  private sender: (x: number, y: number, flag: number, scroll: number) => void;
  constructor(sender: (x: number, y: number, flag: number, scroll: number) => void) { this.sender = sender; }
  held(): boolean { return this.left || this.right; }
  move(x: number, y: number): void { this.x = clamp(x, 0, 1); this.y = clamp(y, 0, 1); this.send(0); }
  drag(dx: number, dy: number, width: number, height: number): void {
    if (this.held() && width > 0 && height > 0) this.move(this.x + dx / width, this.y + dy / height);
  }
  gestureLeft(down: boolean): void {
    if ((down && this.held()) || this.gesture === down) return;
    this.gesture = down; this.send(down ? 1 : 2);
  }
  button(left: boolean, down: boolean): void {
    if ((left ? this.left : this.right) === down) return;
    if (down) { this.gestureLeft(false); this.revision++; }
    if (left) this.left = down; else this.right = down;
    this.send(left ? (down ? 1 : 2) : (down ? 3 : 4));
  }
  click(right: boolean = false): void { if (!this.held()) { this.send(right ? 3 : 1); this.send(right ? 4 : 2); } }
  scroll(delta: number): void { if (delta !== 0) this.sender(this.x, this.y, 7, delta > 0 ? 1 : -1); }
  release(): void { this.gestureLeft(false); this.button(true, false); this.button(false, false); this.revision++; }
  private send(flag: number): void { this.sender(this.x, this.y, flag, 0); }
}
export interface VideoValues { quality: number; frameRate: number; preference: number; }
export class VideoSettings {
  supported: boolean = false;
  pending: boolean = false;
  failed: boolean = false;
  selection: VideoValues;
  applied: VideoValues;
  requestId: number = 0;
  appliedRequestId: number = 0;
  constructor(preference: number = 1, longEdge: number = 1920) {
    this.selection = { quality: longEdge > 0 && longEdge <= 1280 ? 0 : 1, frameRate: 30, preference: clamp(preference, 0, 2) };
    this.applied = this.selection;
  }
  valid(v: VideoValues): boolean { return Number.isInteger(v.quality) && v.quality >= 0 && v.quality <= 2 &&
    (v.frameRate === 30 || v.frameRate === 60) && Number.isInteger(v.preference) && v.preference >= 0 && v.preference <= 2; }
  support(value: boolean): void { this.supported = value; if (!value) { this.selection = this.applied; this.pending = this.failed = false; } }
  begin(value: VideoValues): number {
    if (!this.supported || !this.valid(value)) return -1;
    this.requestId = (this.requestId + 1) >>> 0; this.selection = value; this.pending = true; this.failed = false; return this.requestId;
  }
  receive(value: VideoValues, id: number, accepted: boolean): void {
    if (!this.supported || !this.valid(value) || !Number.isInteger(id) || id < 0 || id > 0xFFFFFFFF || ((this.requestId - id) | 0) < 0) return;
    if (accepted && ((id - this.appliedRequestId) | 0) >= 0) { this.applied = value; this.appliedRequestId = id; }
    if (id !== this.requestId) return;
    this.pending = false; this.failed = !accepted; this.selection = this.applied;
  }
  expire(id: number): void { if (this.pending && id === this.requestId) { this.pending = false; this.failed = true; this.selection = this.applied; } }
  feedback(): string { return !this.supported ? tr("连接建立后可调整，需被控端支持。") : this.failed ? tr("调整失败，请重试。") : this.pending ? tr("等待电脑端确认…") : ''; }
}
export interface Traffic { inbound: number; outbound: number; loss: number; }
export interface NetworkReport { rtt: number; mode: number; srtp: boolean; decoder?: string; video: Traffic; audio: Traffic; data: Traffic; total: Traffic; }
export interface FrameSample { time: number; count: number; latency: number; latencyCount: number; }
export class NetworkStatistics {
  report?: NetworkReport;
  remoteEncoder: string = '';
  reportTime: number = -Infinity;
  rttTime: number = -Infinity;
  rtt: number = -1;
  width: number = 0;
  height: number = 0;
  lastFrame: number = 0;
  frames: FrameSample[] = [];
  receive(report: NetworkReport, now: number): void {
    this.report = report; this.reportTime = now;
    if (Number.isFinite(report.rtt) && report.rtt >= 0 && report.rtt <= 2000) {
      this.rtt = this.rtt >= 0 && now - this.rttTime < 3000 ? this.rtt + 0.25 * (report.rtt - this.rtt) : report.rtt;
      this.rttTime = now;
    }
  }
  frame(id: number, latency: number, width: number, height: number, now: number, latencyCount: number = -1): void {
    if (id <= this.lastFrame || width <= 0 || height <= 0) return;
    const count = Math.min(240, id - this.lastFrame);
    this.frames.push({ time: now, count: count, latency: latency, latencyCount: latencyCount < 0 ? count : latencyCount });
    this.lastFrame = id; this.width = width; this.height = height; this.prune(now);
  }
  reset(): void { this.frames = []; this.width = this.height = 0; }
  prune(now: number): void { this.frames = this.frames.filter((f: FrameSample) => now - f.time < 1000).slice(-240); }
  fps(now: number): number { this.prune(now); return this.frames.reduce((n: number, f: FrameSample) => n + f.count, 0); }
  latency(now: number): number {
    this.prune(now); const valid = this.frames.filter((f: FrameSample) => Number.isFinite(f.latency) && f.latency >= 0);
    const count = valid.reduce((n: number, f: FrameSample) => n + f.latencyCount, 0);
    return count ? valid.reduce((n: number, f: FrameSample) => n + f.latency * f.latencyCount, 0) / count : -1;
  }
  current(now: number): NetworkReport | undefined { return now - this.reportTime < 3000 ? this.report : undefined; }
  mode(now: number): string { const report = this.current(now); return !report ? '—' : report.mode === 0 ? tr("P2P 直连") : report.mode === 1 ? tr("TURN 中继") : '—'; }
  static rate(n: number): string { return !Number.isFinite(n) || n < 0 ? '—' : n >= 1000000 ? `${(n / 1000000).toFixed(1)} Mbps` : n >= 1000 ? `${(n / 1000).toFixed(0)} Kbps` : `${n} bps`; }
  static loss(n: number): string { return Number.isFinite(n) && n >= 0 ? `${(n * 100).toFixed(1)}%` : '—'; }
  static delay(n: number): string { return !Number.isFinite(n) || n < 0 ? '—' : n < 1 ? '<1 ms' : `${n.toFixed(0)} ms`; }
}
export interface PresenceDevice { id: string; online: boolean; }
export interface PresenceMessage { type: string; devices?: PresenceDevice[]; id?: string; online?: boolean; }
export class RecentPresence {
  watched: string[] = [];
  connected: boolean = false;
  awaiting: boolean = true;
  lastRefresh: number = 0;
  values: Map<string, boolean> = new Map();
  times: Map<string, number> = new Map();
  private sender: (ids: string[]) => void;
  constructor(sender: (ids: string[]) => void) { this.sender = sender; }
  watch(ids: string[], now: number): void {
    if (JSON.stringify(ids) === JSON.stringify(this.watched)) return;
    this.watched = ids; this.values.clear(); this.times.clear(); this.awaiting = true; this.refresh(now);
  }
  connect(value: boolean, now: number): void {
    if (value === this.connected) return;
    this.connected = value; this.values.clear(); this.times.clear(); this.awaiting = true; this.refresh(now);
  }
  online(id: string, now: number): boolean { return this.connected && !this.awaiting && this.values.get(id) === true && now - (this.times.get(id) ?? -Infinity) < 60000; }
  receive(message: PresenceMessage, now: number): void {
    if (!this.connected) return;
    let devices: PresenceDevice[] = [];
    if (message.type === 'presence' && Array.isArray(message.devices)) {
      this.values.clear(); this.times.clear(); this.awaiting = false; devices = message.devices;
    } else if (message.type === 'presence_update' && !this.awaiting && typeof message.id === 'string' && typeof message.online === 'boolean') {
      devices = [{ id: message.id, online: message.online }];
    }
    devices.forEach((d: PresenceDevice) => {
      if (this.watched.includes(d.id) && typeof d.online === 'boolean') { this.values.set(d.id, d.online); this.times.set(d.id, now); }
    });
  }
  tick(now: number): void { if (this.connected && now - this.lastRefresh >= 30000) this.refresh(now); }
  private refresh(now: number): void { if (this.connected) { this.lastRefresh = now; this.sender(this.watched.slice()); } }
}
export function asciiKey(character: string): number {
  if (character.length !== 1) return 0;
  const c = character.charCodeAt(0);
  if (c >= 97 && c <= 122) return c - 32;
  if (c >= 65 && c <= 90) return c | 0x100;
  if (c >= 48 && c <= 57) return c;
  if (c === 32 || c === 9 || c === 13) return c;
  if (c === 10) return 13;
  const plain = ';=,-./`[\\]\'', shifted = ':+<_>?~{|}"';
  const codes: number[] = [186, 187, 188, 189, 190, 191, 192, 219, 220, 221, 222];
  const index = plain.indexOf(character); if (index >= 0) return codes[index];
  const upper = shifted.indexOf(character); if (upper >= 0) return codes[upper] | 0x100;
  const digit = ')!@#$%^&*('.indexOf(character); return digit >= 0 ? (48 + digit) | 0x100 : 0;
}
export interface TouchPoint { x: number; y: number; }
export class TouchInput {
  relative: boolean = true;
  private active: boolean = false;
  private multi: boolean = false;
  private moved: boolean = false;
  private long: boolean = false;
  private pinching: boolean = false;
  private revision: number = 0;
  private startX: number = 0;
  private startY: number = 0;
  private lastX: number = 0;
  private lastY: number = 0;
  private focusX: number = 0;
  private focusY: number = 0;
  private span: number = 0;
  private startSpan: number = 0;
  private scrollY: number = 0;
  readonly viewport: RemoteViewport;
  readonly mouse: MouseInput;
  constructor(viewport: RemoteViewport, mouse: MouseInput) { this.viewport = viewport; this.mouse = mouse; }
  cancel(): void { this.active = false; this.multi = true; this.mouse.release(); this.viewport.finish(); }
  hold(): void { if (this.active && !this.multi && !this.moved && this.revision === this.mouse.revision && !this.mouse.held()) { this.mouse.click(true); this.long = true; } }
  touch(points: TouchPoint[], kind: string): void {
    if (kind === 'cancel') { this.cancel(); return; }
    if (points.length >= 2) {
      const x = (points[0].x + points[1].x) / 2, y = (points[0].y + points[1].y) / 2;
      const span = Math.hypot(points[1].x - points[0].x, points[1].y - points[0].y);
      if (!this.multi) {
        this.mouse.gestureLeft(false); this.multi = true; this.startSpan = this.span = span;
        this.focusX = x; this.scrollY = this.focusY = y; this.pinching = false;
      } else if (kind === 'move' && !this.mouse.held()) {
        if (!this.pinching && Math.abs(span - this.startSpan) > 6 && this.viewport.contains(x, y)) this.pinching = true;
        if (this.pinching && this.span > 0) this.viewport.zoom(span / this.span, x, y);
        if (this.viewport.scale > 1.001 || this.pinching) this.viewport.pan(x - this.focusX, y - this.focusY);
        else if (this.viewport.contains(x, y) && Math.abs(y - this.scrollY) > 16) { this.mouse.scroll(y - this.scrollY); this.scrollY = y; }
        this.span = span; this.focusX = x; this.focusY = y;
      }
      return;
    }
    if (this.multi) {
      if (kind === 'up' && points.length === 0) { this.active = false; this.viewport.finish(); }
      return;
    }
    if (kind === 'down' && points.length === 1) {
      const p = points[0]; this.active = true; this.moved = this.long = false; this.pinching = false;
      this.revision = this.mouse.revision; this.startX = this.lastX = p.x; this.startY = this.lastY = p.y;
      if (!this.relative) { this.active = this.viewport.contains(p.x, p.y); if (this.active) this.mouse.move(this.viewport.x(p.x), this.viewport.y(p.y)); }
    } else if (kind === 'move' && this.active && points.length) {
      const p = points[0];
      if (Math.hypot(p.x - this.startX, p.y - this.startY) > 4) this.moved = true;
      if (this.moved && !this.relative && !this.long && this.revision === this.mouse.revision && !this.mouse.held()) this.mouse.gestureLeft(true);
      if (this.relative) {
        const dx = p.x - this.lastX, dy = p.y - this.lastY;
        this.mouse.move(this.mouse.x + dx / this.viewport.width(), this.mouse.y + dy / this.viewport.height());
        if (dx !== 0 || dy !== 0) this.viewport.followPointer(this.mouse.x, this.mouse.y);
      } else this.mouse.move(this.viewport.x(p.x), this.viewport.y(p.y));
      this.lastX = p.x; this.lastY = p.y;
    } else if (kind === 'up') {
      if (this.active && !this.moved && !this.long && this.revision === this.mouse.revision) this.mouse.click();
      this.mouse.gestureLeft(false); this.active = false;
    }
  }
  begin(): void { if (!this.active) this.multi = false; }
}
