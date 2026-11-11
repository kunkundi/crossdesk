import { tr } from './Localization';

export interface ResolutionMode { width: number; height: number; }
export interface ResolutionPreference extends ResolutionMode { display_key: string; }
export interface ResolutionDisplay extends ResolutionPreference {
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
export function validResolutionPreference(value: ResolutionPreference): boolean {
  if (!validMode(value) || typeof value.display_key !== 'string' || !value.display_key.length ||
      value.display_key.length > 1024 || value.display_key.includes('\0')) return false;
  const bytes = Array.from(value.display_key).reduce((n: number, c: string) => {
    const point = c.codePointAt(0) ?? 0;
    return n + (point < 128 ? 1 : point < 2048 ? 2 : point < 65536 ? 3 : 4);
  }, 0);
  return bytes <= 1024;
}
export function readResolutionPreferences(text: string): ResolutionPreference[] {
  if (text.length > 65536) return [];
  try {
    const values = JSON.parse(text) as ResolutionPreference[];
    if (!Array.isArray(values) || values.length > 32) return [];
    const keys: string[] = [];
    const result: ResolutionPreference[] = [];
    for (const value of values) {
      if (!validResolutionPreference(value) || keys.includes(value.display_key)) return [];
      keys.push(value.display_key);
      result.push({ display_key: value.display_key, width: value.width, height: value.height });
    }
    return result;
  } catch { return []; }
}
export class RemoteResolutionStore {
  private read: (key: string) => string;
  private write: (key: string, value: string) => void;
  constructor(read: (key: string) => string, write: (key: string, value: string) => void) {
    this.read = read; this.write = write;
  }
  load(scope: string, id: string): ResolutionPreference[] {
    if (!id) return [];
    try { return readResolutionPreferences(this.read(`resolutions:${JSON.stringify([scope, id])}`)); }
    catch { return []; }
  }
  remember(scope: string, id: string, preference: ResolutionPreference): void {
    if (!id || !validResolutionPreference(preference)) return;
    try {
      const values = this.load(scope, id).filter((value: ResolutionPreference) => value.display_key !== preference.display_key);
      if (values.length >= 32) values.shift();
      values.push({ display_key: preference.display_key, width: preference.width, height: preference.height });
      this.write(`resolutions:${JSON.stringify([scope, id])}`, JSON.stringify(values));
    } catch { console.warn('Could not save remote display resolution'); }
  }
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
          !validResolutionPreference(d) || !Array.isArray(d.modes) || d.modes.length > 256 || !d.modes.every((m: ResolutionMode) => validMode(m))) return false;
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
  receive(response: ResolutionRequest, displays: ResolutionDisplay[]): ResolutionPreference | undefined {
    const request = this.requested;
    if (!request || !response || response.request_id !== request.request_id || response.display_id !== request.display_id ||
        response.display_key !== request.display_key || !sameMode(response, request) || typeof response.accepted !== 'boolean' || !this.valid(displays)) return undefined;
    this.displays = displays; this.pending = false;
    this.failed = !response.accepted || !displays.some((d: ResolutionDisplay) =>
      d.display_id === response.display_id && d.display_key === response.display_key && sameMode(d, response));
    return this.failed ? undefined : { display_key: response.display_key, width: response.width, height: response.height };
  }
  feedback(): string { return this.pending ? tr("等待电脑端确认…") : this.failed ? tr("调整失败，请重试。") : ''; }
}
