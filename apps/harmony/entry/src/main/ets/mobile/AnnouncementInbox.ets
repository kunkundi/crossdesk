import { announcementByteLength } from './AnnouncementText';

export interface AnnouncementRevision { id: number; revision: number; }
export interface AnnouncementItem extends AnnouncementRevision { title: string; body: string; updated_at: number; read?: boolean; }
export interface AnnouncementResponse {
  type: string; request_id: string; summary_only: boolean; offset: number; total: number;
  catalog_revision: number; items: AnnouncementItem[]; error?: string; pending_catalog?: boolean; local_catalog?: boolean; body_ready?: boolean; reset_pages?: boolean;
}
export interface AnnouncementRequest { type: string; request_id: string; summary_only: boolean; offset: number; detail_id?: number; revision?: number; }
interface LocalRevision { id: number; revision: number; }
interface LocalState { scope: string; reads: LocalRevision[]; dismissed: LocalRevision[]; }
// Revisions, pagination, stale replies and local dismissal follow AnnouncementInbox.java.
export class AnnouncementInbox {
  items: AnnouncementItem[] = [];
  total: number = 0;
  unread: number = 0;
  loaded: boolean = false;
  loading: boolean = false;
  failed: boolean = false;
  saveFailed: boolean = false;
  connected: boolean = false;
  changed: () => void = () => {};
  private scope: string = '';
  private reads: Map<number, number> = new Map();
  private dismissed: Map<number, number> = new Map();
  private catalog: Map<number, number> = new Map();
  private pending: Map<number, number> = new Map();
  private pageIds: Map<number, number[]> = new Map();
  private bodies: Map<number, AnnouncementItem[]> = new Map();
  private revision: number = -1;
  private pendingRevision: number = -1;
  private pendingTotal: number = 0;
  private pageOffset: number = 0;
  private limit: number = 20;
  private summary: boolean = true;
  private localCatalog: boolean = false;
  private requestId: string = '';
  private sequence: number = 0;
  private timeout: number = -1;
  private send: (request: AnnouncementRequest) => void;
  private load: (scope: string) => string;
  private save: (scope: string, body: string) => void;
  constructor(send: (request: AnnouncementRequest) => void, load: (scope: string) => string, save: (scope: string, body: string) => void) {
    this.send = send; this.load = load; this.save = save;
  }
  configure(scope: string): void {
    if (this.scope === scope) return;
    this.reset(); this.scope = scope;
    try {
      const local = JSON.parse(this.load(scope)) as LocalState;
      if (local.scope === scope) {
        local.reads.forEach((v: LocalRevision) => { if (v.id > 0 && v.revision > 0) this.reads.set(v.id, v.revision); });
        local.dismissed.forEach((v: LocalRevision) => { if (v.id > 0 && v.revision > 0) this.dismissed.set(v.id, v.revision); });
      }
    } catch { /* First use or corrupt local metadata. Content always comes from the server. */ }
  }
  reset(): void {
    this.cancel(); this.connected = false; this.scope = ''; this.items = []; this.catalog.clear(); this.pending.clear();
    this.reads.clear(); this.dismissed.clear(); this.pageIds.clear(); this.bodies.clear(); this.revision = this.pendingRevision = -1;
    this.total = this.unread = 0; this.limit = 20; this.loaded = this.failed = this.saveFailed = false; this.changed();
  }
  connect(value: boolean): void {
    if (value === this.connected) return;
    this.connected = value; if (value) this.refresh(); else this.cancel(); this.changed();
  }
  catalogChanged(resetPages: boolean): void {
    if (resetPages) { this.pageIds.clear(); this.bodies.clear(); }
    this.refresh();
  }
  refresh(): void { this.cancel(); this.pending.clear(); this.pendingRevision = -1; this.pendingTotal = 0; this.summary = true; this.request(0); this.changed(); }
  more(): void { if (!this.connected || this.loading || this.failed || this.limit >= this.total) return; this.limit += Math.min(20, this.total - this.limit); this.rebuild(); this.changed(); }
  markRead(item: AnnouncementItem): void {
    if (!this.items.some((v: AnnouncementItem) => v.id === item.id && v.revision === item.revision)) return;
    if (this.connected && !item.body) { this.cancel(); this.request(0, item); }
    const next = new Map(this.reads); next.set(item.id, item.revision);
    this.saveReads(next);
  }
  markAllRead(): void {
    if (this.unread === 0) return;
    const next = new Map(this.reads);
    this.catalog.forEach((revision: number, id: number) => {
      if (this.dismissed.get(id) !== revision) next.set(id, revision);
    });
    this.saveReads(next);
  }
  private saveReads(next: Map<number, number>): void {
    if (this.persist(next, this.dismissed)) { this.reads = next; this.reading(); }
    this.changed();
  }
  dismiss(item: AnnouncementItem): void {
    if (!this.items.some((v: AnnouncementItem) => v.id === item.id && v.revision === item.revision)) return;
    const next = new Map(this.dismissed); next.set(item.id, item.revision);
    if (this.persist(this.reads, next)) { this.dismissed = next; this.cancel(); this.rebuild(); } this.changed();
  }
  private persist(reads: Map<number, number>, dismissed: Map<number, number>): boolean {
    const body: LocalState = { scope: this.scope, reads: [], dismissed: [] };
    reads.forEach((revision: number, id: number) => body.reads.push({ id: id, revision: revision }));
    dismissed.forEach((revision: number, id: number) => body.dismissed.push({ id: id, revision: revision }));
    try { this.save(this.scope, JSON.stringify(body)); this.saveFailed = false; return true; }
    catch { this.saveFailed = true; return false; }
  }
  fail(id: string): void { if (id && id === this.requestId) { this.cancel(); this.failed = true; this.changed(); } }
  receive(message: AnnouncementResponse): void {
    if (!this.connected || !this.requestId || message.request_id !== this.requestId) return;
    if (message.pending_catalog) { this.cancel(); this.loading = true; this.failed = false; this.changed(); return; }
    if (message.body_ready) { this.catalogChanged(true); return; }
    const request = this.requestId;
    try {
      if (message.type !== 'announcements' || message.error || message.summary_only !== this.summary ||
          !Number.isSafeInteger(message.offset) || !Number.isSafeInteger(message.total) || !Number.isSafeInteger(message.catalog_revision) ||
          message.offset < 0 || message.total < message.offset || message.total > 1000000 || message.catalog_revision < 0 ||
          !Array.isArray(message.items) || message.items.length !== Math.min(this.summary ? 200 : 20, message.total - message.offset)) throw new Error('Invalid announcement page');
      const ids: number[] = [];
      for (const item of message.items) {
        if (!Number.isSafeInteger(item.id) || !Number.isSafeInteger(item.revision) || item.id <= 0 || item.revision <= 0 || ids.includes(item.id)) throw new Error('Invalid revision');
        ids.push(item.id);
      }
      const expected = this.summary ? this.pendingRevision : this.revision;
      if (expected >= 0 && expected !== message.catalog_revision) { this.refresh(); return; }
      if (this.summary) {
        if (message.offset !== this.pending.size || (this.pendingRevision >= 0 && this.pendingTotal !== message.total)) throw new Error('Invalid offset');
        for (const item of message.items) { if (this.pending.has(item.id)) throw new Error('Duplicate item'); this.pending.set(item.id, item.revision); }
        this.pendingRevision = message.catalog_revision; this.pendingTotal = message.total; this.cancel();
        if (this.pending.size < this.pendingTotal) { this.request(this.pending.size); this.changed(); return; }
        const local = message.local_catalog === true;
        if (this.revision !== this.pendingRevision || local !== this.localCatalog) { this.pageIds.clear(); this.bodies.clear(); }
        this.localCatalog = local;
        this.catalog = new Map(this.pending); this.revision = this.pendingRevision;
      } else {
        if (message.offset !== this.pageOffset || message.total !== this.catalog.size) throw new Error('Invalid body page');
        for (const item of message.items) {
          if (this.catalog.get(item.id) !== item.revision || typeof item.title !== 'string' || typeof item.body !== 'string' ||
              announcementByteLength(item.title) > 240 || announcementByteLength(item.body) > 8000 || !Number.isSafeInteger(item.updated_at) || item.updated_at <= 0) throw new Error('Invalid body');
          this.pageIds.forEach((entries: number[], offset: number) => { if (offset !== message.offset && entries.includes(item.id)) throw new Error('Duplicate page'); });
        }
        this.cancel(); this.pageIds.set(message.offset, ids); this.bodies.set(message.offset, message.items);
      }
      this.failed = false; this.rebuild(); this.changed();
    } catch { this.fail(request); }
  }
  private reading(): void {
    this.total = this.unread = 0;
    this.catalog.forEach((revision: number, id: number) => {
      if (this.dismissed.get(id) === revision) return;
      this.total++; if (this.reads.get(id) !== revision) this.unread++;
    });
    this.items.forEach((item: AnnouncementItem) => item.read = this.reads.get(item.id) === item.revision);
  }
  private rebuild(): void {
    this.reading(); const wanted = Math.min(this.limit, this.total); const visible: AnnouncementItem[] = [];
    this.summary = false; let next = -1;
    for (let offset = 0; offset < this.catalog.size && visible.length < wanted; offset += 20) {
      const ids = this.pageIds.get(offset); if (!ids) { next = offset; break; }
      const selected = ids.filter((id: number) => this.dismissed.get(id) !== this.catalog.get(id));
      if (!selected.length) { this.bodies.delete(offset); continue; }
      const body = this.bodies.get(offset); if (!body) { next = offset; break; }
      for (const item of body) if (selected.includes(item.id) && visible.length < wanted) visible.push(item);
    }
    this.items = visible; this.loaded = true; this.reading();
    if (next >= 0) { this.pageOffset = next; this.request(next); }
  }
  private request(offset: number, detail?: AnnouncementItem): void {
    if (!this.connected || !this.scope) return;
    this.requestId = `hm-${++this.sequence}-${Date.now()}`; this.loading = true; this.failed = false;
    const id = this.requestId; this.timeout = setTimeout(() => this.fail(id), 12000);
    const request: AnnouncementRequest = { type: 'announcements_list', request_id: id, summary_only: this.summary, offset: offset };
    if (detail) { request.detail_id = detail.id; request.revision = detail.revision; }
    this.send(request);
  }
  private cancel(): void { if (this.timeout >= 0) clearTimeout(this.timeout); this.timeout = -1; this.requestId = ''; this.loading = false; }
}
