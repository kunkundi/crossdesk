import { join } from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import {
  Failure,
  token,
  equal,
  parse,
  uiConnect,
  type Data,
  type Panel,
  type VideoOptions,
} from "./common.js";
import { DeviceStore, type Device } from "./store.js";
import { nativeBinary, NativeClient } from "./native.js";

export interface View {
  id: string;
  generation: number;
  secret: string;
  panel: Exclude<Panel, "auto">;
  created: number;
  ready: boolean;
  lastSeen: number;
  alias?: string;
  session?: string;
  disconnected?: boolean;
}
interface Session {
  id: string;
  device: Device;
  native: NativeClient;
  status: Data;
  release: () => Promise<void>;
}
export class Sessions {
  readonly views = new Map<string, View>();
  private current?: Session;
  private connecting = false;
  private closing = false;
  private binary?: Promise<string>;
  constructor(
    readonly store: DeviceStore,
    readonly policy: Panel,
    private nativePath?: string,
    private videoOptions?: VideoOptions,
  ) {}
  choose(apps: boolean): View["panel"] {
    if (this.policy === "apps" && !apps) throw new Failure("PANEL_UNSUPPORTED");
    return this.policy === "auto" ? (apps ? "apps" : "browser") : this.policy;
  }
  create(panel: View["panel"], alias?: string): View {
    for (const [id, view] of this.views)
      if (Date.now() - view.lastSeen > 30 * 60 * 1000) {
        void this.revokeVideo(view);
        this.views.delete(id);
      }
    if (this.views.size >= 32) throw new Failure("VIEW_LIMIT");
    const view: View = {
      id: token(),
      generation: 1,
      secret: token(),
      panel,
      created: Date.now(),
      lastSeen: Date.now(),
      ready: false,
      alias,
      session: this.current?.id,
    };
    this.views.set(view.id, view);
    return view;
  }
  view(id: string): View {
    const v = this.views.get(id);
    if (!v) throw new Failure("VIEW_NOT_FOUND");
    return v;
  }
  authenticate(id: string, generation: number, secret: string): View {
    const v = this.view(id);
    if (v.generation !== generation || !equal(v.secret, secret))
      throw new Failure("VIEW_UNAUTHORIZED");
    return v;
  }
  describe(v: View): Data {
    return {
      view_id: v.id,
      view_generation: v.generation,
      panel: v.panel,
      panel_status: v.ready
        ? "ready"
        : Date.now() - v.created > 10000
          ? "not_confirmed"
          : "waiting",
      session_id: v.session ?? null,
      state: v.session ? "session_available" : v.disconnected ? "closed" : "awaiting_user",
      device_alias: v.alias ?? null,
      next_step: v.session
        ? "Call crossdesk_get_session with view_id."
        : v.disconnected
          ? "The session was disconnected. Do not automatically reconnect."
          : v.panel === "off"
            ? "Use setup to configure credentials and authorization, then crossdesk_connect with the saved device alias. Never send passwords in chat."
            : "The user selects and authorizes a device in the panel. To continue a pending remote task, immediately call crossdesk_get_session with this view_id and wait_ms: 25000; repeat on wait_timed_out. Do not ask the user to send a connection confirmation message.",
    };
  }
  private session(id: string): Session {
    if (!this.current || this.current.id !== id)
      throw new Failure("SESSION_NOT_FOUND");
    return this.current;
  }
  async status(sessionId?: string, viewId?: string, signal?: AbortSignal): Promise<Data> {
    const view = viewId ? this.view(viewId) : undefined;
    const id = sessionId ?? view?.session;
    if (!id && view) return this.describe(view);
    const s = this.session(id!);
    // A panel connection may still be performing the native hello handshake.
    // Reading native status here would race initialization and could close it.
    if (this.connecting) return {
      ...(view ? this.describe(view) : {}),
      session_id: s.id,
      device_alias: s.device.alias,
      state: "connecting",
      control: false,
    };
    try {
      s.status = await s.native.request("status", {}, 5000, signal);
    } catch {
      if (signal?.aborted) throw new Failure("CANCELLED");
      s.status = { state: "closed", control: false };
    }
    // The user may have disconnected or replaced the session during the read.
    if (this.current !== s || (view && view.session !== id))
      return view ? this.describe(view) : { session_id: id, state: "closed", control: false };
    if (["closed", "failed"].includes(s.status.state)) {
      await s.native.close();
      await s.release();
    }
    return {
      ...s.status,
      session_id: s.id,
      device_alias: s.device.alias,
      ...(view ? this.describe(view) : {}),
      state: s.status.state,
      ...(s.status.state === "ready" ? {
        next_step: "Connection ready. Continue the user's pending task within the current authorization. Obtain a screenshot before input; respect human_paused and control. No user confirmation message is needed.",
      } : ["closed", "failed", "disconnected"].includes(s.status.state) ? {
        next_step: "The connection ended. Stop waiting and do not automatically reconnect or replay actions.",
      } : {}),
    };
  }
  async waitForConnection(viewId: string, waitMs: number, signal?: AbortSignal): Promise<Data> {
    const deadline = Date.now() + waitMs;
    let result = this.describe(this.view(viewId));
    const timedOut = () => ({
      ...result,
      wait_timed_out: true,
      next_step: "Still waiting for the user to connect in the panel. Call crossdesk_get_session again with the same view_id and wait_ms: 25000 while the remote task is pending. Do not ask the user to type a connection confirmation.",
    });
    const timeout = new AbortController();
    const timer = waitMs > 0 ? setTimeout(() => timeout.abort(), waitMs) : undefined;
    const waitSignal = signal ? AbortSignal.any([signal, timeout.signal]) : timeout.signal;
    try {
      for (;;) {
        if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
        const view = this.view(viewId);
        view.lastSeen = Date.now();
        result = await this.status(undefined, viewId, waitSignal);
        if (waitMs === 0 || view.panel === "off" ||
            ["ready", "closed", "failed", "disconnected"].includes(result.state))
          return result;
        const remaining = deadline - Date.now();
        if (remaining <= 0) return timedOut();
        await delay(Math.min(250, remaining), undefined, { signal: waitSignal });
      }
    } catch (error) {
      if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
      if (timeout.signal.aborted) return timedOut();
      throw error;
    } finally {
      clearTimeout(timer);
    }
  }
  async connectSaved(
    name: string,
    mode: string,
    display: number,
    signal?: AbortSignal,
  ): Promise<Data> {
    const d = await this.store.device(name);
    if (
      !d.authorization ||
      (mode === "control" && d.authorization !== "control")
    )
      throw new Failure("AUTH_REQUIRED");
    return this.connect(
      d,
      await this.store.password(d),
      mode,
      display,
      undefined,
      signal,
    );
  }
  async connectUI(v: View, args: unknown, signal?: AbortSignal): Promise<Data> {
    const p = parse(uiConnect, args);
    if (!!p.device_alias === !!p.device_id)
      throw new Failure("SELECT_ONE_DEVICE");
    const config = await this.store.config();
    const d: Device = p.device_alias
      ? { ...(await this.store.device(p.device_alias)) }
      : {
          alias: p.label ?? p.device_id!,
          device_id: p.device_id!,
          server: p.server ?? config.server,
          port: p.port ?? config.port,
        };
    if (p.device_alias && (p.server || p.port || p.label))
      throw new Failure("INVALID_ARGUMENT");
    if ((p.remember_password || p.persistent_authorization) && !p.save_device)
      throw new Failure("SAVE_DEVICE_REQUIRED");
    let password = p.password ?? (await this.store.password(d));
    p.password = undefined;
    // Persist only after successful authentication, avoiding poisoned saved passwords.
    const result = await this.connect(
      d,
      password,
      p.mode,
      p.display_index,
      v,
      signal,
    );
    if (p.save_device) {
      d.authorization = p.persistent_authorization ? p.mode : undefined;
      try {
        await this.store.save(d, p.remember_password ? password : undefined);
      } catch (error) {
        result.persistence_error =
          error instanceof Failure ? error.code : "CONFIG_WRITE_FAILED";
      }
    }
    password = "";
    return result;
  }
  private async connect(
    d: Device,
    password: string,
    mode: string,
    display: number,
    view?: View,
    signal?: AbortSignal,
  ): Promise<Data> {
    if (this.closing) throw new Failure("SESSION_CLOSED");
    if (this.connecting) throw new Failure("BUSY");
    if (
      this.current &&
      !["closed", "failed", "disconnected"].includes(
        (await this.status(this.current.id)).state,
      )
    )
      throw new Failure("SESSION_LIMIT");
    this.connecting = true;
    let release: (() => Promise<void>) | undefined;
    let native: NativeClient | undefined;
    try {
      if (this.current) await this.disconnect(this.current.id);
      release = await this.store.lock(
        `device:${d.server}:${d.port}:${d.device_id}`,
      );
      this.binary ??= nativeBinary(this.nativePath).catch((e) => {
        this.binary = undefined;
        throw e;
      });
      const binary = await this.binary;
      if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
      native = new NativeClient(
        binary,
        d.server,
        d.port,
        join(this.store.directory, "logs"),
        this.videoOptions ?? (await this.store.config()).video,
      );
      const s: Session = {
        id: token(),
        device: d,
        native,
        status: {},
        release,
      };
      this.current = s;
      if (view) {
        view.session = s.id;
        view.alias = d.alias;
        view.disconnected = false;
      }
      s.status = await native.connect(
        d.device_id,
        password,
        mode,
        display,
        signal,
      );
      password = "";
      const deadline = Date.now() + 35000;
      while (Date.now() < deadline) {
        if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
        s.status = await native.request("status", {}, 5000, signal);
        if (s.status.state === "ready" && s.status.frame_age_ms <= 2000)
          return { ...s.status, session_id: s.id, device_alias: s.device.alias };
        if (["failed", "closed", "disconnected"].includes(s.status.state))
          throw new Failure(
            s.status.error === "incorrect_password"
              ? "AUTH_FAILED"
              : "CONNECTION_FAILED",
          );
        await delay(100, undefined, { signal });
      }
      throw new Failure("CONNECTION_TIMEOUT");
    } catch (error) {
      await native?.close();
      await release?.();
      if (this.current?.native === native) this.current = undefined;
      if (view) view.session = undefined;
      throw error;
    } finally {
      password = "";
      this.connecting = false;
    }
  }
  async screenshot(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    if (args.after_frame_id !== undefined && args.generation === undefined)
      throw new Failure("GENERATION_REQUIRED");
    return this.session(session_id).native.request(
      "screenshot",
      args,
      (args.timeout_ms ?? 2000) + 3000,
      signal,
    );
  }
  async action(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, observe_after = true, ...args } = p;
    const s = this.session(session_id);
    const result = await s.native.request("action", args, 5000, signal);
    if (observe_after && result.status === "submitted") {
      try {
        result.observation = await this.screenshot(
          {
            session_id,
            generation: result.generation,
            after_frame_id: result.after_frame_id,
            timeout_ms: 2000,
          },
          signal,
        );
      } catch (error) {
        result.observation_error =
          error instanceof Failure ? error.code : "OBSERVATION_FAILED";
      }
    }
    return result;
  }
  async control(id: string, enabled: boolean) {
    return this.session(id).native.request("control", { enabled });
  }
  async display(id: string, index: number) {
    return this.session(id).native.request("display", { index });
  }
  async disconnect(id: string) {
    const s = this.session(id);
    await s.native.close();
    await s.release();
    if (this.current === s) this.current = undefined;
    for (const v of this.views.values())
      if (v.session === id) {
        v.session = undefined;
        v.disconnected = true;
      }
    return { session_id: id, state: "closed" };
  }
  async viewerControl(v: View, operation: string) {
    if (!v.session) throw new Failure("NOT_READY");
    if (operation === "disconnect") return this.disconnect(v.session);
    return this.session(v.session).native.request("viewer_control", {
      operation,
    });
  }
  async openVideo(v: View, origin: string): Promise<Data> {
    if (!v.session || this.views.get(v.id) !== v) throw new Failure("NOT_READY");
    const s = this.session(v.session);
    if (this.closing || this.connecting || ["closed", "failed"].includes(s.status.state))
      throw new Failure("NOT_READY");
    const generation = v.generation, session = v.session;
    const ticket = token();
    const result = await s.native.request("video_open", {
      ticket, view: v.id, view_generation: generation, session, origin, apps: v.panel === "apps",
    });
    if (this.current !== s || this.views.get(v.id) !== v ||
        v.generation !== generation || v.session !== session) {
      await s.native.request("video_revoke", { view: v.id }).catch(() => {});
      throw new Failure("SESSION_CHANGED");
    }
    if (result.transport !== "loopback-websocket/1" ||
        typeof result.url !== "string" || !/^ws:\/\/127\.0\.0\.1:\d+\/api\/video$/.test(result.url) ||
        result.protocol !== (s.native.videoOptions.decode_path === "minirtc" ? "nv12/1" : "h264-annexb/1") ||
        result.decoder !== s.native.videoOptions.decoder)
      throw new Failure("NATIVE_PROTOCOL_MISMATCH");
    return { ...result, ticket };
  }
  async revokeVideo(view?: View) {
    const s = this.current;
    if (!s || (view && view.session !== s.id)) return;
    try {
      await s.native.request("video_revoke", view ? { view: view.id } : {});
    } catch {
      // Losing the control channel must not leave an authorized video socket
      // alive after its view was removed or replaced.
      if (this.current === s) await this.disconnect(s.id);
      else await s.native.close();
    }
  }
  async close() {
    this.closing = true;
    if (this.current) await this.disconnect(this.current.id);
    this.views.clear();
  }
}
