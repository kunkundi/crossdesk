import { join } from "node:path";
import { mkdir } from "node:fs/promises";
import { setTimeout as delay } from "node:timers/promises";
import {
  Failure,
  failure,
  token,
  equal,
  parse,
  uiConnect,
  unlockPassword,
  type Data,
  type Panel,
  type VideoOptions,
} from "./common.js";
import { DeviceStore, type Device } from "./store.js";
import { nativeBinary, NativeClient } from "./native.js";
import { displayEnvironment } from "./environment.js";

// Keep the public error categories while exposing only known native reasons.
const connectionErrors = new Map<string, string>([
  ["incorrect_password", "AUTH_FAILED"],
  ["signaling_timeout", "CONNECTION_TIMEOUT"],
  ["connection_timeout", "CONNECTION_TIMEOUT"],
  ["signaling_login_failed", "CONNECTION_FAILED"],
  ["signaling_disconnected", "CONNECTION_FAILED"],
  ["tls_certificate_error", "CONNECTION_FAILED"],
  ["cli_identity_not_supported", "CONNECTION_FAILED"],
  ["join_request_failed", "CONNECTION_FAILED"],
  ["device_not_found", "CONNECTION_FAILED"],
  ["device_offline_or_unavailable", "CONNECTION_FAILED"],
  ["connection_lost", "CONNECTION_FAILED"],
  ["connection_failed", "CONNECTION_FAILED"],
  ["remote_disconnected", "CONNECTION_FAILED"],
  ["display_not_available", "CONNECTION_FAILED"],
  ["media_initialization_failed", "CONNECTION_FAILED"],
  ["control_event_overflow", "CONNECTION_FAILED"],
]);

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
  connectionError?: Data;
}
interface Session {
  id: string;
  device: Device;
  native: NativeClient;
  status: Data;
  release: () => Promise<void>;
  connecting: boolean;
  connection: AbortController;
  displayBatch?: boolean;
  mode: string;
  desiredControl: boolean;
  audioEnabled?: boolean;
  recoveryEnabled: boolean;
  recoveryAttempts: number;
  recoveryAttempt: number;
  recovering?: Promise<void>;
  recoveryAbort?: AbortController;
  monitor?: ReturnType<typeof setInterval>;
  recoveredFrom?: string;
  statusRequest?: Promise<Data>;
  closing?: Promise<void>;
  unlock?: UnlockRequest;
  unlocks: Map<string, UnlockRequest>;
}
interface UnlockRequest {
  id: string;
  operation: string;
  observation: string;
  useSaved: boolean;
  state: "preparing" | "awaiting_password" | "submitting" | "verifying" | "verified" | "submitted" | "failed" | "cancelled";
  verification?: "locked" | "unlocked" | "unknown";
  expires: number;
  generation?: number;
  geometry?: number;
  error?: Data;
  result?: Data;
  persistenceError?: string;
  saved?: boolean;
  controller: AbortController;
}
export class Sessions {
  readonly views = new Map<string, View>();
  private current?: Session;
  private active = new Map<string, Session>();
  private replacements = new Map<string, string>();
  private captures = new Map<string, { session: string; native: NativeClient; created: number; displays: Data[] }>();
  private drags = new Map<string, { arguments: string; result: Data }>();
  private connecting = new Set<AbortController>();
  private connectionDone = new Set<Promise<void>>();
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
    if (this.policy !== "auto") return this.policy;
    const environment = displayEnvironment();
    return environment.gui ? "browser" : environment.terminal ? "terminal" : "off";
  }
  create(panel: View["panel"], alias?: string): View {
    for (const [id, view] of this.views)
      if (Date.now() - view.lastSeen > 30 * 60 * 1000) {
        void this.revokeVideo(view);
        this.views.delete(id);
      }
    if (this.views.size >= 32) throw new Failure("VIEW_LIMIT");
    const selected = alias ? [...this.active.values()].find((s) => s.device.alias === alias && !s.closing)
      : this.current && !this.current.closing ? this.current : [...this.active.values()].find((s) => !s.closing);
    const view: View = {
      id: token(),
      generation: 1,
      secret: token(),
      panel,
      created: Date.now(),
      lastSeen: Date.now(),
      ready: false,
      alias: alias ?? selected?.device.alias,
      session: selected?.id,
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
      state: v.session ? "session_available" : v.disconnected ? "closed" : v.connectionError ? "failed" : "awaiting_user",
      ...(v.connectionError ? { error: v.connectionError } : {}),
      device_alias: v.alias ?? null,
      next_step: v.session
        ? "Call crossdesk_get_session with view_id."
        : v.disconnected || v.connectionError
          ? "The session was disconnected. Do not automatically reconnect."
          : v.panel === "off"
            ? "Use setup to configure credentials and authorization, then crossdesk_connect with the saved device alias. Never send passwords in chat."
            : "The user selects and authorizes a device in the panel. To continue a pending remote task, immediately call crossdesk_get_session with this view_id and wait_ms: 25000; repeat on wait_timed_out. Do not ask the user to send a connection confirmation message.",
    };
  }
  private session(id: string): Session {
    if (this.replacements.has(id)) throw new Failure("SESSION_REPLACED");
    const session = this.active.get(id);
    if (!session) throw new Failure("SESSION_NOT_FOUND");
    return session;
  }
  async status(sessionId?: string, viewId?: string, signal?: AbortSignal): Promise<Data> {
    const view = viewId ? this.view(viewId) : undefined;
    const id = sessionId ?? view?.session;
    if (!id && view) return this.describe(view);
    const replacement = this.replacements.get(id!);
    if (replacement) return { ...await this.status(replacement, undefined, signal), previous_session_id: id };
    const s = this.session(id!);
    if (s.recovering) return {
      ...s.status, error: "", frame_age_ms: null,
      ...(view ? this.describe(view) : {}), session_id: s.id, device_alias: s.device.alias,
      state: "reconnecting", control: false, reconnect_enabled: s.recoveryEnabled,
      recovery_attempt: s.recoveryAttempt,
      next_step: "Authorized connection recovery is running. Wait with crossdesk_get_session. Use the returned session_id and a fresh screenshot after recovery. Query uncertain task outcomes without replaying input.",
    };
    // A panel connection may still be performing the native hello handshake.
    // Reading native status here would race initialization and could close it.
    if (s.connecting) return {
      ...(view ? this.describe(view) : {}),
      session_id: s.id,
      device_alias: s.device.alias,
      state: "connecting",
      control: false,
    };
    // Concurrent panel/model polls share one native read. A cancelled waiter
    // must not cancel another caller's status request or disconnect the session.
    if (signal?.aborted) throw new Failure("CANCELLED");
    if (!["closed", "failed"].includes(s.status.state)) {
      s.statusRequest ??= s.native.request("status", {}, 5000)
        .then((status) => (s.status = status))
        .catch((error) => {
          if (!(error instanceof Failure) || error.code !== "SESSION_CLOSED") throw error;
          return (s.status = { ...s.status, state: "closed", control: false, error: "native_process_closed" });
        })
        .finally(() => { s.statusRequest = undefined; });
      await this.waitStatus(s.statusRequest, signal);
    }
    if (s.recovering) return this.status(sessionId, viewId, signal);
    // The user may have disconnected or replaced the session during the read.
    if (this.active.get(s.id) !== s || (view && view.session !== id))
      return view ? this.describe(view) : { session_id: id, state: "closed", control: false };
    if (["closed", "failed"].includes(s.status.state)) {
      if (this.startRecovery(s)) return this.status(sessionId, viewId, signal);
      await this.dispose(s);
    }
    this.checkUnlock(s);
    return {
      ...s.status,
      session_id: s.id,
      device_alias: s.device.alias,
      reconnect_enabled: s.recoveryEnabled,
      recovery_attempt: s.recoveryAttempt,
      ...(s.recoveredFrom ? { recovered_from: s.recoveredFrom } : {}),
      ...(s.unlock ? { unlock: this.unlockStatus(s.unlock) } : {}),
      ...(view ? this.describe(view) : {}),
      state: s.status.state,
      ...(s.status.state === "ready" ? {
        next_step: "Connection ready. Continue the user's pending task within the current authorization. Obtain a screenshot before input; respect human_paused and control. No user confirmation message is needed.",
      } : ["closed", "failed", "disconnected"].includes(s.status.state) ? {
        next_step: "The connection ended. Stop waiting and do not automatically reconnect or replay actions.",
      } : {}),
      ...(s.unlock && this.unlockPending(s.unlock) ? { next_step: this.unlockStatus(s.unlock).next_step } : {}),
    };
  }
  private async waitStatus<T>(request: Promise<T>, signal?: AbortSignal) {
    if (!signal) return request;
    let abort: () => void = () => {};
    try {
      return await Promise.race([request, new Promise<never>((_, reject) => {
        abort = () => reject(new Failure("CANCELLED"));
        signal.addEventListener("abort", abort, { once: true });
        if (signal.aborted) abort();
      })]);
    } finally {
      signal.removeEventListener("abort", abort);
    }
  }
  private dispose(s: Session): Promise<void> {
    this.cancelUnlock(s);
    clearInterval(s.monitor);
    s.monitor = undefined;
    return s.closing ??= s.native.close().finally(() => s.release());
  }
  private monitor(s: Session) {
    clearInterval(s.monitor);
    if (!s.recoveryEnabled || s.closing) return;
    s.monitor = setInterval(() => {
      if (s.recovering || s.connecting || this.closing) return;
      void this.status(s.id).catch((error) => {
        if (this.active.get(s.id) !== s || s.closing || s.recovering) return;
        if (failure(error).code === "NATIVE_TIMEOUT") {
          s.status = { ...s.status, state: "failed", error: "native_process_unresponsive" };
          this.startRecovery(s);
        }
      });
    }, 1000);
    s.monitor.unref();
  }
  private startRecovery(s: Session): boolean {
    if (!s.recoveryEnabled || s.closing || this.closing || s.status.human_paused ||
        !["connection_lost", "signaling_disconnected", "native_process_closed", "native_process_unresponsive"].includes(s.status.error))
      return false;
    if (s.recovering) return true;
    clearInterval(s.monitor);
    const controller = s.recoveryAbort = new AbortController();
    s.recovering = this.recover(s, controller.signal).finally(() => { s.recovering = undefined; });
    return true;
  }
  private async recover(s: Session, signal: AbortSignal) {
    const views = [...this.views.values()].filter((view) => view.session === s.id);
    const display = s.status.display ?? 0;
    try { await this.dispose(s); }
    catch (error) {
      s.recoveryEnabled = false;
      s.status = { ...s.status, state: "failed", recovery_error: failure(error) };
      return;
    }
    const deadline = Date.now() + 60000;
    for (let attempt = 1; attempt <= s.recoveryAttempts && Date.now() < deadline; attempt++) {
      s.recoveryAttempt = attempt;
      try {
        await delay(Math.min(500 * 3 ** (attempt - 1), 5000), undefined, { signal });
        if (!s.recoveryEnabled || this.closing || signal.aborted) throw new Failure("CANCELLED");
        const device = await this.store.device(s.device.alias);
        if (device.device_id !== s.device.device_id || device.server !== s.device.server || device.port !== s.device.port)
          throw new Failure("DEVICE_ALIAS_CONFLICT");
        const timeout = AbortSignal.timeout(Math.max(1, deadline - Date.now()));
        const result = await this.connectSaved(s.device.alias, s.mode, display,
          AbortSignal.any([signal, timeout]), s.desiredControl);
        const next = this.session(result.session_id);
        if (signal.aborted || this.closing || !s.recoveryEnabled) {
          await this.disconnect(next.id);
          throw new Failure("CANCELLED");
        }
        if (!s.desiredControl) await next.native.request("control", { enabled: false });
        next.desiredControl = s.desiredControl;
        if (next.status.preview_supported && s.status.preview_supported) {
          const settings: Data = { fps: s.status.preview_fps, quality: s.status.preview_quality };
          if (next.status.preview_resolution_supported) Object.assign(settings, {
            width: s.status.preview_width, height: s.status.preview_height });
          if (next.status.preview_bitrate_supported) settings.max_bitrate_bps = s.status.preview_bitrate_bps;
          await next.native.request("video_settings", settings).catch(() => {});
        }
        if (s.audioEnabled) {
          await next.native.request("audio", { enabled: true });
          next.audioEnabled = true;
        }
        next.recoveryEnabled = true;
        next.recoveryAttempts = s.recoveryAttempts;
        next.recoveryAttempt = attempt;
        next.recoveredFrom = s.id;
        for (const view of views) if (this.views.get(view.id) === view && view.session === s.id) {
          view.session = next.id;
          view.disconnected = false;
          view.connectionError = undefined;
        }
        for (const [old, replacement] of this.replacements) if (replacement === s.id) this.replacements.set(old, next.id);
        if (this.replacements.size >= 128) this.replacements.delete(this.replacements.keys().next().value!);
        this.replacements.set(s.id, next.id);
        this.active.delete(s.id);
        this.monitor(next);
        return;
      } catch (error) {
        s.status = { ...s.status, state: "failed", control: false, recovery_error: failure(error) };
        if (signal.aborted || this.closing || !s.recoveryEnabled ||
            ["AUTH_REQUIRED", "AUTH_FAILED", "DEVICE_ALIAS_CONFLICT", "CANCELLED"].includes(failure(error).code) ||
            failure(error).reason === "tls_certificate_error") break;
      }
    }
    s.recoveryEnabled = false;
    s.status = { ...s.status, state: signal.aborted ? "closed" : "failed", control: false, recovery_exhausted: !signal.aborted };
  }
  async setRecovery(sessionId: string, enabled: boolean, attempts = 3): Promise<Data> {
    const s = this.session(sessionId);
    if (enabled) {
      if (s.closing) throw new Failure("SESSION_CLOSED");
      const device = await this.store.device(s.device.alias);
      if (device.device_id !== s.device.device_id || device.server !== s.device.server || device.port !== s.device.port)
        throw new Failure("DEVICE_ALIAS_CONFLICT");
      if (!device.authorization || (s.mode === "control" && device.authorization !== "control"))
        throw new Failure("AUTH_REQUIRED");
      await this.store.password(device);
      if (s.closing || this.active.get(s.id) !== s) throw new Failure("SESSION_CLOSED");
    }
    s.recoveryEnabled = enabled;
    s.recoveryAttempts = attempts;
    if (!enabled) s.recoveryAbort?.abort();
    this.monitor(s);
    return { session_id: s.id, reconnect_enabled: enabled, max_attempts: attempts };
  }
  async waitForSession(sessionId: string, waitMs: number, signal?: AbortSignal): Promise<Data> {
    const deadline = Date.now() + waitMs;
    while (true) {
      const result = await this.status(sessionId, undefined, signal);
      if (!["connecting", "reconnecting"].includes(result.state)) return result;
      if (Date.now() >= deadline) return { ...result, wait_timed_out: true };
      await delay(Math.min(200, deadline - Date.now()), undefined, { signal });
    }
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
    initialControl = mode === "control",
  ): Promise<Data> {
    const d = await this.store.device(name);
    if (
      !d.authorization ||
      (mode === "control" && d.authorization !== "control")
    )
      throw new Failure("AUTH_REQUIRED");
    return this.connect(
      d,
      await this.waitStatus(this.store.password(d), signal),
      mode,
      display,
      undefined,
      signal,
      initialControl,
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
    if ((p.remember_password || p.remember_unlock_password || p.persistent_authorization) && !p.save_device)
      throw new Failure("SAVE_DEVICE_REQUIRED");
    if (p.remember_unlock_password && !p.unlock_password)
      throw new Failure("UNLOCK_CREDENTIAL_REQUIRED");
    if (p.unlock_password && !p.remember_unlock_password)
      throw new Failure("SAVE_UNLOCK_REQUIRED");
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
      delete d.unlock_credential;
      delete d.unlock_environment_variable;
      try {
        await this.store.save(d, p.remember_password ? password : undefined, p.unlock_password);
      } catch (error) {
        result.persistence_error =
          error instanceof Failure ? error.code : "CONFIG_WRITE_FAILED";
      }
    }
    password = "";
    p.unlock_password = undefined;
    return result;
  }
  private async connect(
    d: Device,
    password: string,
    mode: string,
    display: number,
    view?: View,
    signal?: AbortSignal,
    initialControl = mode === "control",
  ): Promise<Data> {
    if (this.closing) throw new Failure("SESSION_CLOSED");
    const established = [...this.active.values()].filter((s) => !s.connecting && !s.closing).length;
    if (established + this.connecting.size >= 8) throw new Failure("SESSION_LIMIT");
    const controller = new AbortController();
    this.connecting.add(controller);
    signal = signal ? AbortSignal.any([signal, controller.signal]) : controller.signal;
    let complete!: () => void;
    const done = new Promise<void>((resolve) => { complete = resolve; });
    this.connectionDone.add(done);
    let release: (() => Promise<void>) | undefined;
    let native: NativeClient | undefined;
    let session: Session | undefined;
    try {
      for (const s of this.active.values()) {
        if (s.closing && !s.recovering) {
          this.active.delete(s.id);
          for (const v of this.views.values()) if (v.session === s.id) {
            v.session = undefined;
            v.disconnected = true;
          }
        }
      }
      if (view) {
        await this.revokeVideo(view);
        view.disconnected = false;
        view.connectionError = undefined;
      }
      release = await this.store.lock(
        `device:${d.server}:${d.port}:${d.device_id}`,
      );
      this.binary ??= nativeBinary(this.nativePath).catch((e) => {
        this.binary = undefined;
        throw e;
      });
      const binary = await this.binary;
      if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
      const sessionId = token();
      const logs = join(this.store.directory, "logs", sessionId);
      await mkdir(logs, { recursive: true, mode: 0o700 });
      native = new NativeClient(
        binary,
        d.server,
        d.port,
        logs,
        this.videoOptions ?? (await this.store.config()).video,
      );
      const s: Session = session = {
        id: sessionId,
        device: d,
        native,
        status: {},
        release,
        connecting: true,
        connection: controller,
        mode,
        desiredControl: initialControl,
        recoveryEnabled: false,
        recoveryAttempts: 3,
        recoveryAttempt: 0,
        unlocks: new Map(),
      };
      this.current = s;
      this.active.set(s.id, s);
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
      if (mode === "control" && !initialControl)
        await native.request("control", { enabled: false }, 5000, signal);
      const deadline = Date.now() + 35000;
      while (Date.now() < deadline) {
        if (this.closing || signal?.aborted) throw new Failure("CANCELLED");
        s.status = await native.request("status", {}, 5000, signal);
        if (this.active.get(s.id) !== s || signal.aborted) throw new Failure("CANCELLED");
        if (s.status.state === "ready" && typeof s.status.frame_age_ms === "number" && s.status.frame_age_ms <= 2000) {
          s.connecting = false;
          return { ...s.status, session_id: s.id, device_alias: s.device.alias };
        }
        if (["failed", "closed", "disconnected"].includes(s.status.state)) {
          const code = connectionErrors.get(s.status.error);
          throw new Failure(code ?? "CONNECTION_FAILED", false, false,
            code ? s.status.error : undefined);
        }
        await delay(100, undefined, { signal });
      }
      throw new Failure("CONNECTION_TIMEOUT");
    } catch (error) {
      if (session) { await this.dispose(session); this.active.delete(session.id); }
      else {
        await native?.close();
        await release?.();
      }
      if (this.current?.native === native) this.current = [...this.active.values()].find((s) => !s.closing);
      const reason = signal.aborted ? new Failure("CANCELLED") : error;
      if (view && (!view.session || view.session === session?.id)) {
        view.session = undefined;
        if (!view.disconnected) view.connectionError = failure(reason);
      }
      throw reason;
    } finally {
      password = "";
      this.connecting.delete(controller);
      this.connectionDone.delete(done);
      complete();
    }
  }
  async screenshot(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    if (s.displayBatch) throw new Failure("BUSY");
    if (args.after_frame_id !== undefined && args.generation === undefined)
      throw new Failure("GENERATION_REQUIRED");
    const result = await s.native.request(
      "screenshot",
      args,
      (args.timeout_ms ?? 2000) + 3000,
      signal,
    );
    return this.publicObservation(s, result);
  }
  private publicObservation(s: Session, observation: Data): Data {
    return { ...observation, metadata: { ...observation.metadata,
      observation_id: `${s.id}_${observation.metadata.observation_id}` } };
  }
  private nativeObservation(s: Session, observation: string): string {
    const prefix = `${s.id}_`;
    if (!observation.startsWith(prefix)) throw new Failure("STALE_OBSERVATION");
    return observation.slice(prefix.length);
  }
  async action(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, observe_after = true, ...args } = p;
    const s = this.session(session_id);
    if (s.displayBatch) throw new Failure("BUSY");
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    args.observation_id = this.nativeObservation(s, args.observation_id);
    const result = await s.native.request("action", args, (args.action.duration_ms ?? 0) + 5000, signal);
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
  async waitFor(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    args.observation_id = this.nativeObservation(s, args.observation_id);
    const result = await s.native.request(
      "wait_frame", args, (args.timeout_ms ?? 5000) + 3000, signal);
    if (result.matched) result.observation = await this.screenshot({ session_id }, signal);
    return result;
  }
  async captureDisplays(p: Data, signal?: AbortSignal): Promise<Data> {
    const s = this.session(p.session_id);
    if (s.displayBatch) throw new Failure("BUSY");
    const status = await this.status(s.id, undefined, signal);
    if (s.displayBatch) throw new Failure("BUSY");
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    if (status.held_key_codes?.length || status.held_mouse_flags?.length) throw new Failure("INPUT_HELD");
    const indices: number[] = p.display_indices ?? status.displays.map((display: Data) => display.index);
    if (!indices.length || indices.length > 8) throw new Failure("DISPLAY_LIMIT");
    if (indices.some((index) => !status.displays.some((display: Data) => display.index === index)))
      throw new Failure("DISPLAY_UNAVAILABLE");
    const native = s.native;
    const created = Date.now();
    if (!["ready", "waiting_frame"].includes(status.state)) throw new Failure("NOT_READY");
    s.displayBatch = true;
    const observations: Data[] = [];
    let error: unknown;
    try {
      for (const index of indices) {
        await native.request("display", { index }, 5000, signal);
        const observation = await native.request("screenshot", { max_edge: p.max_edge ?? 1024 }, 5000, signal);
        observations.push({ ...this.publicObservation(s, observation), actionable: false });
      }
    } catch (failure) { error = failure; }
    finally {
      try {
        await native.request("display", { index: status.display });
        await native.request("screenshot", { max_edge: 64 });
      } catch (restoreError) { error ??= restoreError; }
      s.displayBatch = false;
    }
    if (error) throw error;
    for (const [key, capture] of this.captures) if (Date.now() - capture.created > 30000) this.captures.delete(key);
    if (this.captures.size >= 32) this.captures.delete(this.captures.keys().next().value!);
    const capture_id = token();
    this.captures.set(capture_id, { session: s.id, native, created, displays: observations.map((o) => o.metadata) });
    return { session_id: s.id, capture_id, observations, restored_display: status.display };
  }
  async dragAcross(p: Data, signal?: AbortSignal): Promise<Data> {
    const key = `${p.session_id}:${p.operation_id}`;
    const args = JSON.stringify({ capture_id: p.capture_id, path: p.path, button: p.button ?? "left", duration_ms: p.duration_ms ?? 500 });
    const previous = this.drags.get(key);
    if (previous) {
      if (previous.arguments !== args) throw new Failure("OPERATION_ID_CONFLICT");
      return previous.result;
    }
    const s = this.session(p.session_id);
    const capture = this.captures.get(p.capture_id);
    if (!capture || capture.session !== s.id || capture.native !== s.native || Date.now() - capture.created > 30000)
      throw new Failure("STALE_OBSERVATION");
    const sources = new Map(capture.displays.map((display) => [display.display_index, display]));
    for (const point of p.path) {
      const source = sources.get(point.display_index);
      if (!source || point.x < 0 || point.y < 0 || point.x >= source.image_size.width || point.y >= source.image_size.height)
        throw new Failure("INVALID_COORDINATE");
    }
    if (s.displayBatch) throw new Failure("BUSY");
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    s.displayBatch = true;
    const native = s.native;
    const record = { arguments: args, result: { operation_id: p.operation_id, status: "in_progress" } as Data };
    if (this.drags.size >= 128) this.drags.delete(this.drags.keys().next().value!);
    this.drags.set(key, record);
    let submitted = false;
    const deadline = Date.now() + 30000;
    try {
      const status = await native.request("status", {}, 5000, signal);
      if (!status.control || status.human_paused) throw new Failure("CONTROL_DENIED");
      if (status.held_key_codes?.length || status.held_mouse_flags?.length) throw new Failure("INPUT_HELD");
      let display = -1, observation: Data = {};
      const submit = async (type: string, point: Data) => {
        if (Date.now() > deadline) throw new Failure("ACTION_TIMEOUT", submitted);
        if (point.display_index !== display) {
          await native.request("display", { index: point.display_index, preserve_input: submitted }, 5000, signal);
          observation = await native.request("screenshot", { max_edge: 1024 }, 5000, signal);
          display = point.display_index;
        }
        const source = sources.get(display)!;
        const image = observation.metadata.image_size;
        const action = { type, x: point.x * image.width / source.image_size.width,
          y: point.y * image.height / source.image_size.height, ...(type === "move" ? {} : { button: p.button ?? "left" }) };
        submitted = true;
        const result = await native.request("action", { operation_id: token(),
          observation_id: observation.metadata.observation_id, action }, 5000, signal);
        if (result.status !== "submitted") throw new Failure("ACTION_OUTCOME_UNKNOWN", true);
      };
      await submit("mouse_down", p.path[0]);
      for (const point of p.path.slice(1)) {
        await delay((p.duration_ms ?? 500) / (p.path.length - 1), undefined, { signal });
        await submit("move", point);
      }
      await submit("mouse_up", p.path.at(-1));
      record.result = { operation_id: p.operation_id, status: "submitted", side_effect_possible: true,
        observation: this.publicObservation(s, await native.request("screenshot", {}, 5000, signal)) };
    } catch (error) {
      record.result = { operation_id: p.operation_id, status: submitted ? "outcome_unknown" : "failed_before_submit",
        error: failure(error), side_effect_possible: submitted };
    } finally {
      await native.request("release_inputs").catch(() => {});
      s.displayBatch = false;
    }
    return record.result;
  }
  async audio(p: Data, capture: boolean, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    const result = await s.native.request(capture ? "audio_capture" : "audio", args,
      capture ? (args.duration_ms ?? 3000) + 5000 : 5000, signal);
    if (!capture) { s.status = result; s.audioEnabled = args.enabled; }
    return result;
  }
  async videoSettings(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    if (s.displayBatch) throw new Failure("BUSY");
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    const requested = await s.native.request("video_settings", args, 5000, signal);
    const deadline = Date.now() + 2500;
    do {
      const status = await this.status(s.id, undefined, signal);
      if (status.preview_ack === requested.preview_request) return status;
      await delay(100, undefined, { signal });
    } while (Date.now() < deadline);
    return { ...requested, wait_timed_out: true };
  }
  async clipboard(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    return s.native.request("clipboard", args, 7000, signal);
  }
  async files(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock) && !["status", "cancel"].includes(args.operation))
      throw new Failure("UNLOCK_PENDING");
    if (args.operation !== "list") return s.native.request("files", args, 7000, signal);
    const requestId = args.request_id ?? (await s.native.request("files",
      { operation: "list", path: args.path ?? "" }, 7000, signal)).request_id;
    try {
      const deadline = Date.now() + 25000;
      while (true) {
        const result = await s.native.request("files", {
          operation: "list_result", request_id: requestId, offset: args.offset ?? 0,
        }, 7000, signal);
        if (result.state !== "pending") {
          if (result.next_offset == null)
            await s.native.request("files", { operation: "list_cancel", request_id: requestId });
          return result;
        }
        if (Date.now() >= deadline) return { request_id: requestId, state: "pending", wait_timed_out: true };
        await delay(100, undefined, { signal });
      }
    } catch (error) {
      await s.native.request("files", { operation: "list_cancel", request_id: requestId }).catch(() => {});
      throw error;
    }
  }
  async system(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const s = this.session(session_id);
    if (args.command !== "status" && s.unlock && this.unlockPending(s.unlock))
      throw new Failure("UNLOCK_PENDING");
    return s.native.request("system", args, 7000, signal);
  }
  async releaseInputs(sessionId: string) {
    const s = this.session(sessionId);
    this.cancelUnlock(s);
    return s.native.request("release_inputs");
  }
  async control(id: string, enabled: boolean) {
    const s = this.session(id);
    if (!enabled) { this.cancelUnlock(s); s.desiredControl = false; }
    const result = await s.native.request("control", { enabled });
    s.desiredControl = enabled;
    return result;
  }
  async display(id: string, index: number) {
    const s = this.session(id);
    if (s.displayBatch) throw new Failure("BUSY");
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    return s.native.request("display", { index });
  }
  async disconnect(id: string) {
    id = this.replacements.get(id) ?? id;
    const s = this.session(id);
    s.recoveryEnabled = false;
    s.recoveryAbort?.abort();
    clearInterval(s.monitor);
    if (s.connecting) s.connection.abort();
    for (const v of this.views.values())
      if (v.session === id) {
        v.session = undefined;
        v.disconnected = true;
        v.connectionError = undefined;
      }
    await this.dispose(s);
    await s.recovering?.catch(() => {});
    this.active.delete(s.id);
    if (this.current === s) this.current = [...this.active.values()].find((other) => !other.closing);
    return { session_id: id, state: "closed" };
  }
  async viewerControl(v: View, operation: string) {
    if (!v.session) throw new Failure("NOT_READY");
    if (operation === "disconnect") return this.disconnect(v.session);
    if (operation === "take_over" && !this.session(v.session).native.viewerInputSupported)
      throw new Failure("NATIVE_INPUT_UNSUPPORTED");
    if (operation === "pause" || operation === "take_over") {
      const s = this.session(v.session);
      this.cancelUnlock(s);
      s.status.human_paused = true;
      s.desiredControl = false;
      s.recoveryAbort?.abort();
    }
    const s = this.session(v.session);
    const result = await s.native.request("viewer_control", {
      operation, owner: v.id,
    });
    if (operation === "resume") s.desiredControl = true;
    s.status = result;
    return result;
  }
  async viewerInput(view: View, args: Data, signal?: AbortSignal): Promise<Data> {
    if (!view.session) throw new Failure("NOT_READY");
    const s = this.session(view.session);
    if (s.displayBatch) throw new Failure("BUSY");
    return s.native.request("viewer_input", { ...args, owner: view.id }, 5000, signal);
  }
  private unlockPending(request: UnlockRequest) {
    return ["preparing", "awaiting_password", "submitting", "verifying"].includes(request.state);
  }
  private unlockStatus(request: UnlockRequest): Data {
    return {
      request_id: request.id,
      state: request.state,
      ...(request.verification ? { verification: request.verification } : {}),
      expires_at: request.expires,
      ...(request.error ? { error: request.error } : {}),
      ...(request.result ? { result: request.result } : {}),
      ...(request.persistenceError ? { persistence_error: request.persistenceError } : {}),
      ...(request.saved ? { credential_saved: true } : {}),
      next_step: request.state === "verifying"
        ? "The remote system is verifying unlock completion. Wait with crossdesk_get_session and unlock_request_id."
        : this.unlockPending(request)
        ? "The user enters the system lock-screen password in the local panel. Never request it in chat. Wait with crossdesk_get_session and unlock_request_id."
        : ["submitted", "verified"].includes(request.state)
          ? "Take a fresh screenshot to verify that the desktop is unlocked, then continue the pending task. If the password was rejected, request user entry with use_saved: false; never automatically replay an unlock."
          : "Unlock stopped. Inspect the error and current screen before starting another request. Never automatically replay an uncertain submission.",
    };
  }
  private cancelUnlock(s: Session, code = "CANCELLED") {
    const request = s.unlock;
    if (!request || !this.unlockPending(request)) return;
    request.error = failure(new Failure(code, ["submitting", "verifying"].includes(request.state)));
    request.state = "cancelled";
    request.controller.abort();
  }
  private checkUnlock(s: Session) {
    const request = s.unlock;
    if (!request || !this.unlockPending(request)) return;
    if (Date.now() > request.expires) this.cancelUnlock(s, "UNLOCK_EXPIRED");
    else if (s.closing || !s.status.control || s.status.human_paused ||
        (request.generation !== undefined && (request.generation !== s.status.generation ||
         request.geometry !== s.status.geometry_epoch))) this.cancelUnlock(s, "CONTROL_DENIED");
  }
  async requestUnlock(p: Data, signal?: AbortSignal): Promise<Data> {
    const s = this.session(p.session_id);
    if (s.displayBatch) throw new Failure("BUSY");
    if (!s.native.unlockSupported) throw new Failure("NATIVE_UNLOCK_UNSUPPORTED");
    const previous = s.unlocks.get(p.operation_id);
    if (previous) {
      if (previous.observation !== p.observation_id || previous.useSaved !== p.use_saved)
        throw new Failure("OPERATION_ID_CONFLICT");
      this.checkUnlock(s);
      return { session_id: s.id, unlock: this.unlockStatus(previous) };
    }
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    const request: UnlockRequest = {
      id: p.operation_id, operation: token(), observation: p.observation_id, useSaved: p.use_saved,
      state: "preparing", expires: Date.now() + 300000, controller: new AbortController(),
    };
    s.unlock = request;
    s.unlocks.set(request.id, request);
    if (s.unlocks.size > 128) s.unlocks.delete(s.unlocks.keys().next().value!);
    try {
      const prepared = await s.native.request("unlock_prepare", { observation_id: this.nativeObservation(s, p.observation_id) }, 5000, signal);
      request.generation = prepared.generation;
      request.geometry = prepared.geometry_epoch;
      let password: string | undefined;
      if (p.use_saved) {
        try {
          const d = await this.store.device(s.device.alias);
          if (d.device_id !== s.device.device_id || d.server !== s.device.server || d.port !== s.device.port)
            throw new Failure("DEVICE_ALIAS_CONFLICT");
          password = await this.store.password(d, "unlock");
        } catch (error) {
          if (!["DEVICE_NOT_FOUND", "UNLOCK_CREDENTIAL_REQUIRED", "CREDENTIAL_STORE_UNAVAILABLE"].includes(failure(error).code)) throw error;
        }
      }
      if (!this.unlockPending(request) || signal?.aborted) throw new Failure("CANCELLED");
      request.state = "awaiting_password";
      if (password !== undefined) {
        try { await this.submitUnlock(s, request, password, false, signal); }
        finally { password = undefined; }
      } else if (this.policy === "off") throw new Failure("UNLOCK_CREDENTIAL_REQUIRED");
    } catch (error) {
      if (request.state !== "cancelled") {
        request.state = "failed";
        request.error = failure(error);
      }
    }
    return { session_id: s.id, unlock: this.unlockStatus(request) };
  }
  private async submitUnlock(s: Session, request: UnlockRequest, password: string, remember: boolean, signal?: AbortSignal) {
    parse(unlockPassword, password);
    await this.status(s.id, undefined, signal);
    if (s.unlock !== request || request.state !== "awaiting_password") throw new Failure("UNLOCK_EXPIRED");
    request.state = "submitting";
    const combined = signal ? AbortSignal.any([signal, request.controller.signal]) : request.controller.signal;
    try {
      const verify = s.status.capabilities?.system === true;
      if (verify) {
        const before = await this.system({ session_id: s.id, command: "status" }, combined);
        if (combined.aborted) return;
        if (before.lock_state === "unlocked") {
          request.state = "verified";
          request.verification = "unlocked";
          request.result = { status: "already_unlocked" };
          return;
        }
      }
      // User input may take longer than an observation's lifetime. Re-observe the
      // same display generation before sending the secret to its focused field.
      const observation = await this.screenshot({ session_id: s.id }, combined);
      const metadata = observation.metadata;
      if (metadata.generation !== request.generation || metadata.geometry_epoch !== request.geometry)
        throw new Failure("STALE_OBSERVATION");
      const result = await s.native.request("unlock", {
        operation_id: request.operation,
        observation_id: this.nativeObservation(s, metadata.observation_id),
        password,
      }, 5000, combined);
      request.result = result;
      if (request.controller.signal.aborted) return;
      request.state = result.status === "submitted" ? "submitted" : "failed";
      if (verify && request.state === "submitted") {
        request.state = "verifying";
        const deadline = Date.now() + 8000;
        do {
          const state = await this.system({ session_id: s.id, command: "status" }, combined);
          request.verification = ["locked", "unlocked"].includes(state.lock_state) ? state.lock_state : "unknown";
          if (request.verification !== "locked") break;
          await delay(250, undefined, { signal: combined });
        } while (Date.now() < deadline);
        if (request.controller.signal.aborted) return;
        request.state = request.verification === "unlocked" ? "verified" : "submitted";
        if (request.verification === "locked") request.error = failure(new Failure("UNLOCK_NOT_CONFIRMED", true));
      }
      if (remember && (request.state === "verified" || (!verify && request.state === "submitted"))) {
        try { await this.store.saveUnlock(s.device, password); request.saved = true; }
        catch (error) { request.persistenceError = failure(error).code; }
      } else if (remember && request.state === "submitted") {
        request.persistenceError = "UNLOCK_NOT_CONFIRMED";
      }
    } catch (error) {
      if (!request.controller.signal.aborted) {
        request.state = "failed";
        request.error = failure(error);
        if (request.result?.status === "submitted") request.error.side_effect_possible = true;
      }
    } finally { password = ""; }
  }
  async unlockUI(v: View, p: Data, signal?: AbortSignal): Promise<Data> {
    const s = this.session(v.session!);
    const request = s.unlock;
    if (!request || request.id !== p.request_id) throw new Failure("UNLOCK_NOT_FOUND");
    if (p.cancel) this.cancelUnlock(s);
    else if (request.state === "awaiting_password") {
      const password = p.password;
      p.password = undefined;
      await this.submitUnlock(s, request, password, p.remember_password, signal);
    }
    return { unlock: this.unlockStatus(request) };
  }
  async waitForUnlock(sessionId: string | undefined, viewId: string | undefined, requestId: string, waitMs: number, signal?: AbortSignal): Promise<Data> {
    const deadline = Date.now() + waitMs;
    while (true) {
      const result = await this.status(sessionId, viewId, signal);
      const s = this.active.get(result.session_id);
      if (!s || ["closed", "failed"].includes(result.state)) return result;
      const request = s.unlocks.get(requestId);
      if (!request) throw new Failure("UNLOCK_NOT_FOUND");
      result.unlock = this.unlockStatus(request);
      if (!this.unlockPending(request)) return result;
      if (Date.now() >= deadline) return { ...result, wait_timed_out: true };
      await delay(Math.min(200, deadline - Date.now()), undefined, { signal });
    }
  }
  async terminalFrame(v: View, args: Data, signal?: AbortSignal) {
    if (v.panel !== "terminal" || !v.session)
      throw new Failure("NOT_READY");
    const s = this.session(v.session);
    return s.native.request("terminal_frame", args, 5000, signal);
  }
  async openVideo(v: View, origin: string): Promise<Data> {
    if (!v.session || this.views.get(v.id) !== v) throw new Failure("NOT_READY");
    const s = this.session(v.session);
    if (this.closing || s.connecting || ["closed", "failed"].includes(s.status.state))
      throw new Failure("NOT_READY");
    const generation = v.generation, session = v.session;
    const ticket = token();
    const result = await s.native.request("video_open", {
      ticket, view: v.id, view_generation: generation, session, origin, apps: v.panel === "apps",
    });
    if (this.active.get(s.id) !== s || this.views.get(v.id) !== v ||
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
  async listSessions(): Promise<Data> {
    const sessions = await Promise.all([...this.active.keys()].map((id) => this.status(id)));
    return { sessions };
  }
  async revokeVideo(view?: View) {
    const targets = view ? [this.active.get(view.session ?? "")].filter((s): s is Session => !!s)
      : [...this.active.values()];
    await Promise.all(targets.map(async (s) => {
      try { await s.native.request("video_revoke", view ? { view: view.id } : {}); }
      catch { if (this.active.get(s.id) === s) await this.disconnect(s.id); }
    }));
  }
  async close() {
    this.closing = true;
    for (const controller of this.connecting) controller.abort();
    const pending = [...this.connectionDone];
    const results = await Promise.allSettled([...this.active.keys()].map((id) => this.disconnect(id)));
    await Promise.allSettled(pending);
    this.views.clear();
    const error = results.find((result) => result.status === "rejected");
    if (error?.status === "rejected") throw error.reason;
  }
}
