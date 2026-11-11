import { tr } from './Localization';

export interface ResolutionMode { width: number; height: number; }
export interface ResolutionDisplay extends ResolutionMode {
  display_id: number; display_key: string; width: number; height: number; modes: ResolutionMode[];
}
export interface ResolutionRequest extends ResolutionMode {
  display_id: number; display_key: string; width: number; height: number; request_id: number; accepted: boolean;
}
export function resolutionLabel(mode: ResolutionMode): string { return `${mode.width} × ${mode.height}`; }
function sameMode(a: ResolutionMode, b: ResolutionMode): boolean { return a.width === b.width && a.height === b.height; }
function validMode(mode: ResolutionMode): boolean {
  return !!mode && Number.isInteger(mode.width) && Number.isInteger(mode.height) &&
    mode.width > 0 && mode.width <= 32768 && mode.height > 0 && mode.height <= 32768;
}
export class RemoteDisplayResolutions {
  displays: ResolutionDisplay[] = [];
  requested?: ResolutionRequest;
  pending: boolean = false;
  failed: boolean = false;
  display(id: number): ResolutionDisplay | undefined { return this.displays.find((d: ResolutionDisplay) => d.display_id === id); }
  begin(id: number, mode: ResolutionMode): ResolutionRequest | undefined {
    const display = this.display(id);
    if (this.pending || !display || !validMode(mode) || sameMode(display, mode) ||
        !display.modes.some((m: ResolutionMode) => sameMode(m, mode))) return undefined;
    const next = ((this.requested?.request_id ?? 0) + 1) >>> 0;
    this.requested = { display_id: id, display_key: display.display_key, width: mode.width,
      height: mode.height, request_id: next || 1, accepted: false };
    this.pending = true; this.failed = false;
    return this.requested;
  }
  expire(id: number): void { if (this.pending && this.requested?.request_id === id) { this.pending = false; this.failed = true; } }
  private valid(displays: ResolutionDisplay[]): boolean {
    if (!Array.isArray(displays) || displays.length > 32) return false;
    const ids: number[] = [];
    for (const d of displays) {
      if (!d || !Number.isInteger(d.display_id) || d.display_id < 0 || d.display_id >= 32 || ids.includes(d.display_id) ||
          typeof d.display_key !== 'string' || !d.display_key.length || d.display_key.length > 1024 || d.display_key.includes('\0') ||
          !validMode(d) || !Array.isArray(d.modes) || d.modes.length > 256 || !d.modes.every((m: ResolutionMode) => validMode(m))) return false;
      const bytes = Array.from(d.display_key).reduce((n: number, c: string) => {
        const point = c.codePointAt(0) ?? 0;
        return n + (point < 128 ? 1 : point < 2048 ? 2 : point < 65536 ? 3 : 4);
      }, 0);
      if (bytes > 1024) return false;
      ids.push(d.display_id);
    }
    return true;
  }
  host(displays: ResolutionDisplay[]): void {
    if (!this.valid(displays)) return;
    this.displays = displays;
    const request = this.requested;
    if (this.pending && request && !displays.some((d: ResolutionDisplay) =>
      d.display_id === request.display_id && d.display_key === request.display_key &&
        d.modes.some((m: ResolutionMode) => sameMode(m, request)))) this.expire(request.request_id);
  }
  receive(response: ResolutionRequest, displays: ResolutionDisplay[]): void {
    const request = this.requested;
    if (!request || !response || response.request_id !== request.request_id || response.display_id !== request.display_id ||
        response.display_key !== request.display_key || !sameMode(response, request) || typeof response.accepted !== 'boolean' || !this.valid(displays)) return;
    this.displays = displays; this.pending = false;
    this.failed = !response.accepted || !displays.some((d: ResolutionDisplay) =>
      d.display_id === response.display_id && d.display_key === response.display_key && sameMode(d, response));
  }
  feedback(): string { return this.pending ? tr("等待电脑端确认…") : this.failed ? tr("调整失败，请重试。") : ''; }
}
