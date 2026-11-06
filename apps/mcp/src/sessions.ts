import { join } from "node:path";
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
  state: "preparing" | "awaiting_password" | "submitting" | "submitted" | "failed" | "cancelled";
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
  private connecting?: AbortController;
  private connectionDone?: Promise<void>;
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
    const view: View = {
      id: token(),
      generation: 1,
      secret: token(),
      panel,
      created: Date.now(),
      lastSeen: Date.now(),
      ready: false,
      alias: alias ?? this.current?.device.alias,
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
          return (s.status = { state: "closed", control: false });
        })
        .finally(() => { s.statusRequest = undefined; });
      await this.waitStatus(s.statusRequest, signal);
    }
    // The user may have disconnected or replaced the session during the read.
    if (this.current !== s || (view && view.session !== id))
      return view ? this.describe(view) : { session_id: id, state: "closed", control: false };
    if (["closed", "failed"].includes(s.status.state)) {
      await this.dispose(s);
    }
    this.checkUnlock(s);
    return {
      ...s.status,
      session_id: s.id,
      device_alias: s.device.alias,
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
  private async waitStatus(request: Promise<Data>, signal?: AbortSignal) {
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
    return s.closing ??= s.native.close().finally(() => s.release());
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
  ): Promise<Data> {
    if (this.closing) throw new Failure("SESSION_CLOSED");
    if (this.connecting) throw new Failure("BUSY");
    // Reserve the single connection slot before the first await.
    const controller = this.connecting = new AbortController();
    signal = signal ? AbortSignal.any([signal, controller.signal]) : controller.signal;
    let complete!: () => void;
    this.connectionDone = new Promise<void>((resolve) => { complete = resolve; });
    let release: (() => Promise<void>) | undefined;
    let native: NativeClient | undefined;
    let session: Session | undefined;
    try {
      if (this.current && !["closed", "failed", "disconnected"].includes(
        (await this.status(this.current.id, undefined, signal)).state,
      )) throw new Failure("SESSION_LIMIT");
      if (this.current) await this.disconnect(this.current.id);
      if (view) {
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
      native = new NativeClient(
        binary,
        d.server,
        d.port,
        join(this.store.directory, "logs"),
        this.videoOptions ?? (await this.store.config()).video,
      );
      const s: Session = session = {
        id: token(),
        device: d,
        native,
        status: {},
        release,
        connecting: true,
        unlocks: new Map(),
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
        if (this.current !== s || signal.aborted) throw new Failure("CANCELLED");
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
      if (session) await this.dispose(session);
      else {
        await native?.close();
        await release?.();
      }
      if (this.current?.native === native) this.current = undefined;
      const reason = signal.aborted ? new Failure("CANCELLED") : error;
      if (view && (!view.session || view.session === session?.id)) {
        view.session = undefined;
        if (!view.disconnected) view.connectionError = failure(reason);
      }
      throw reason;
    } finally {
      password = "";
      this.connecting = undefined;
      this.connectionDone = undefined;
      complete();
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
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
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
  async waitFor(p: Data, signal?: AbortSignal): Promise<Data> {
    const { session_id, ...args } = p;
    const result = await this.session(session_id).native.request(
      "wait_frame", args, (args.timeout_ms ?? 5000) + 3000, signal);
    if (result.matched) result.observation = await this.screenshot({ session_id }, signal);
    return result;
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
  async control(id: string, enabled: boolean) {
    if (!enabled) this.cancelUnlock(this.session(id));
    return this.session(id).native.request("control", { enabled });
  }
  async display(id: string, index: number) {
    const s = this.session(id);
    this.checkUnlock(s);
    if (s.unlock && this.unlockPending(s.unlock)) throw new Failure("UNLOCK_PENDING");
    return s.native.request("display", { index });
  }
  async disconnect(id: string) {
    const s = this.session(id);
    if (s.connecting) this.connecting?.abort();
    for (const v of this.views.values())
      if (v.session === id) {
        v.session = undefined;
        v.disconnected = true;
        v.connectionError = undefined;
      }
    await this.dispose(s);
    if (this.current === s) this.current = undefined;
    return { session_id: id, state: "closed" };
  }
  async viewerControl(v: View, operation: string) {
    if (!v.session) throw new Failure("NOT_READY");
    if (operation === "disconnect") return this.disconnect(v.session);
    if (operation === "pause") this.cancelUnlock(this.session(v.session));
    return this.session(v.session).native.request("viewer_control", {
      operation,
    });
  }
  private unlockPending(request: UnlockRequest) {
    return ["preparing", "awaiting_password", "submitting"].includes(request.state);
  }
  private unlockStatus(request: UnlockRequest): Data {
    return {
      request_id: request.id,
      state: request.state,
      expires_at: request.expires,
      ...(request.error ? { error: request.error } : {}),
      ...(request.result ? { result: request.result } : {}),
      ...(request.persistenceError ? { persistence_error: request.persistenceError } : {}),
      ...(request.saved ? { credential_saved: true } : {}),
      next_step: this.unlockPending(request)
        ? "The user enters the system lock-screen password in the local panel. Never request it in chat. Wait with crossdesk_get_session and unlock_request_id."
        : request.state === "submitted"
          ? "Take a fresh screenshot to verify that the desktop is unlocked, then continue the pending task. If the password was rejected, request user entry with use_saved: false; never automatically replay an unlock."
          : "Unlock stopped. Inspect the error and current screen before starting another request. Never automatically replay an uncertain submission.",
    };
  }
  private cancelUnlock(s: Session, code = "CANCELLED") {
    const request = s.unlock;
    if (!request || !this.unlockPending(request)) return;
    request.error = failure(new Failure(code, request.state === "submitting"));
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
      const prepared = await s.native.request("unlock_prepare", { observation_id: p.observation_id }, 5000, signal);
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
      // User input may take longer than an observation's lifetime. Re-observe the
      // same display generation before sending the secret to its focused field.
      const observation = await this.screenshot({ session_id: s.id }, combined);
      const metadata = observation.metadata;
      if (metadata.generation !== request.generation || metadata.geometry_epoch !== request.geometry)
        throw new Failure("STALE_OBSERVATION");
      const result = await s.native.request("unlock", {
        operation_id: request.operation,
        observation_id: metadata.observation_id,
        password,
      }, 5000, combined);
      request.result = result;
      if (request.controller.signal.aborted) return;
      request.state = result.status === "submitted" ? "submitted" : "failed";
      if (remember && request.state === "submitted") {
        try { await this.store.saveUnlock(s.device, password); request.saved = true; }
        catch (error) { request.persistenceError = failure(error).code; }
      }
    } catch (error) {
      if (!request.controller.signal.aborted) {
        request.state = "failed";
        request.error = failure(error);
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
      const s = result.session_id && this.current?.id === result.session_id ? this.current : undefined;
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
    if (v.panel !== "terminal" || !v.session || this.connecting)
      throw new Failure("NOT_READY");
    const s = this.session(v.session);
    return s.native.request("terminal_frame", args, 5000, signal);
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
    this.connecting?.abort();
    const pending = this.connectionDone;
    if (this.current) await this.disconnect(this.current.id);
    await pending;
    this.views.clear();
  }
}
