import { readFile } from "node:fs/promises";
import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/server";
import type {
  CallToolResult,
  ClientCapabilities,
  Tool,
  ServerContext,
} from "@modelcontextprotocol/server";
import {
  getUiCapability,
  registerAppResource,
  registerAppTool,
  RESOURCE_MIME_TYPE,
} from "@modelcontextprotocol/ext-apps/server";
import { Sessions, type View } from "./sessions.js";
import { BrowserPanel } from "./browser.js";
import { TerminalPanel } from "./terminal.js";
import { uiAction, uiSchemas, type UIAction } from "./ui-actions.js";
import {
  id,
  alias,
  mode,
  action,
  screenshot,
  Failure,
  failure,
  parse,
  token,
  type Data,
} from "./common.js";

const resource = "ui://crossdesk/viewer.html";
const uiNames: Record<UIAction, string> = {
  ready: "crossdesk_ui_ready",
  devices: "crossdesk_ui_list_devices",
  status: "crossdesk_ui_get_session",
  connect: "crossdesk_ui_connect",
  video_open: "crossdesk_video_open",
  control: "crossdesk_viewer_control",
  unlock: "crossdesk_ui_unlock",
  recovery: "crossdesk_ui_recovery",
  input: "crossdesk_ui_input",
};
function output(value: Data, meta?: Data): CallToolResult {
  const data = { ...value };
  const content: any[] = [];
  if (data.mime_type === "image/png" && typeof data.data === "string") {
    content.push({ type: "image", mimeType: data.mime_type, data: data.data });
    delete data.data;
  }
  if (data.observation?.data) {
    content.push({
      type: "image",
      mimeType: data.observation.mime_type,
      data: data.observation.data,
    });
    data.observation = { ...data.observation };
    delete data.observation.data;
  }
  if (Array.isArray(data.observations)) data.observations = data.observations.map((observation: Data) => {
    const { data: bytes, ...metadata } = observation;
    if (bytes) content.push({ type: "image", mimeType: observation.mime_type, data: bytes });
    return metadata;
  });
  content.unshift({ type: "text", text: JSON.stringify(data) });
  return { content, structuredContent: data, ...(meta ? { _meta: meta } : {}) };
}
export function createServer(
  sessions: Sessions,
  browser: BrowserPanel,
  terminal: TerminalPanel,
): McpServer {
  const server = new McpServer(
    { name: "crossdesk-mcp", version: "0.1.0" },
    {
      capabilities: { tools: {}, resources: {} },
      instructions:
        "Remote computer control. Open a panel for the user to select and authorize a device. Never request passwords in chat. When a remote task is pending, immediately follow crossdesk_open with crossdesk_get_session(view_id, wait_ms: 25000) in the same turn. Repeat bounded waits on wait_timed_out until ready or the user cancels; do not ask the user to type a connection confirmation or click Continue. On ready, continue the requested task within current authorization. When state is reconnecting, continue bounded waits for the explicitly enabled recovery policy. Use the returned session_id after recovery and inspect a fresh screenshot. On closed/failed, stop waiting and do not automatically reconnect. Obtain a screenshot before actions and pass its observation_id; coordinates refer to that returned image. If the remote system is locked, focus its visible password field, take a fresh screenshot and call crossdesk_unlock. A saved system password is used automatically; otherwise the local panel asks the user. Wait with crossdesk_get_session(unlock_request_id, wait_ms: 25000), then verify a fresh screenshot before continuing the original task. Submitted actions are not remote execution acknowledgements. Never replay actions after uncertain outcomes. Human pause can only be resumed in the user panel.",
    },
  );
  type Definition = {
    name: string;
    schema: z.ZodType;
    description: string;
    ui?: boolean;
    app?: boolean;
    readOnly?: boolean;
    run: (p: Data, ctx: ServerContext) => Promise<CallToolResult>;
  };
  const definitions = new Map<string, Definition>();
  const supported = (ctx: ServerContext) => {
    const envelope = ctx.mcpReq.envelope as Data | undefined;
    const caps = envelope
      ? envelope["io.modelcontextprotocol/clientCapabilities"]
      : server.server.getClientCapabilities();
    return (
      getUiCapability((caps ?? {}) as ClientCapabilities)?.mimeTypes?.includes(
        RESOURCE_MIME_TYPE,
      ) ?? false
    );
  };
  const apps = (ctx: ServerContext) =>
    sessions.policy === "apps" && supported(ctx);
  const openPanel = async (v: View) => v.panel === "browser" ? browser.open(v)
    : v.panel === "terminal" ? terminal.open(v) : sessions.describe(v);
  const viewResult = (v: View, result: Data) =>
    output(
      result,
      v.panel === "apps"
        ? {
            "crossdesk/ui": {
              view_id: v.id,
              view_generation: v.generation,
              ui_token: v.secret,
            },
          }
        : undefined,
    );
  const add = (
    name: string,
    schema: z.ZodType,
    description: string,
    run: Definition["run"],
    options: Partial<Definition> = {},
  ) => {
    const d = { name, schema, description, run, ...options };
    definitions.set(name, d);
    const config = {
      description,
      inputSchema: schema,
      annotations: {
        readOnlyHint: !!d.readOnly,
        destructiveHint: ["crossdesk_action", "crossdesk_unlock", "crossdesk_clipboard_set",
          "crossdesk_files_transfer", "crossdesk_files_cancel", "crossdesk_system_command", "crossdesk_drag_across"].includes(d.name),
        openWorldHint: true,
      },
      ...(d.ui || d.app
        ? {
            _meta: {
              ui: {
                ...(d.ui ? { resourceUri: resource } : {}),
                visibility: d.app
                  ? ["app" as const]
                  : ["model" as const, "app" as const],
              },
            },
          }
        : {}),
    };
    if (d.ui || d.app)
      registerAppTool(
        server,
        name,
        { ...config, _meta: config._meta! },
        (p, ctx) => run(p as Data, ctx),
      );
    else server.registerTool(name, config, (p, ctx) => run(p as Data, ctx));
  };
  add(
    "crossdesk_open",
    z.strictObject({ device_alias: alias.optional() }),
    "Open CrossDesk so the user can choose a device, enter its password, and authorize this session. GUI environments use a browser; headless terminal environments return terminal_command, which you must show to the user to run in a separate terminal. Never run the terminal viewer in a captured tool-output channel. Returns immediately. To continue a pending remote task, call crossdesk_get_session with the returned view_id and wait_ms: 25000; no chat confirmation is needed.",
    async (p, ctx) => {
      if (p.device_alias) await sessions.store.device(p.device_alias);
      const v = sessions.create(
        sessions.choose(supported(ctx)),
        p.device_alias,
      );
      return viewResult(
        v,
        await openPanel(v),
      );
    },
    { ui: true },
  );
  add(
    "crossdesk_open_browser",
    z.strictObject({ view_id: id.optional() }),
    "Explicitly open the local browser panel after the user requests browser fallback. Does not create another remote connection.",
    async (p) => {
      if (!["auto", "browser"].includes(sessions.policy))
        throw new Failure("PANEL_POLICY_DENIED");
      const v = p.view_id
        ? sessions.view(p.view_id)
        : sessions.create("browser");
      if (v.panel !== "browser") {
        await sessions.revokeVideo(v);
        v.panel = "browser";
        v.generation++;
        v.secret = token();
        v.ready = false;
        v.created = Date.now();
      }
      return output(await browser.open(v));
    },
  );
  add(
    "crossdesk_list_devices",
    z.strictObject({}),
    "List the local saved device book and persistent authorization, without credentials. This is not online discovery.",
    async () => output({ devices: await sessions.store.list() }),
    { readOnly: true },
  );
  add(
    "crossdesk_connect",
    z.strictObject({
      device_alias: alias,
      mode: mode.default("observe"),
      display_index: z.int().min(0).max(63).default(0),
    }),
    "Connect only a saved, previously authorized device with available credentials. AUTH_REQUIRED means the user must authorize through the panel or setup.",
    async (p, ctx) => {
      const selected = sessions.choose(supported(ctx));
      // Reserve panel capacity before starting a connection. Once connected,
      // panel setup errors must not hide the live session from the caller.
      const v = sessions.create(selected, p.device_alias);
      let result: Data;
      try {
        result = await sessions.connectSaved(
          p.device_alias,
          p.mode,
          p.display_index,
          ctx.mcpReq.signal,
        );
      } catch (error) {
        sessions.views.delete(v.id);
        throw error;
      }
      v.session = result.session_id;
      let panelResult: Data;
      try { panelResult = await openPanel(v); }
      catch (error) { panelResult = { ...sessions.describe(v), panel_error: failure(error) }; }
      return viewResult(v, {
        ...result,
        ...panelResult,
        state: result.state,
      });
    },
    { ui: true },
  );
  add(
    "crossdesk_get_session",
    z.strictObject({
      session_id: id.optional(),
      view_id: id.optional(),
      wait_ms: z.int().min(0).max(25000).optional(),
      unlock_request_id: id.optional(),
    }),
    "Get connection, display geometry, frame age and control state. Supply exactly one of session_id or view_id. With unlock_request_id, wait for that unlock request to finish (default 25000 ms). Otherwise view_id waits for connection, and session_id with wait_ms waits for an active recovery. session_id without wait_ms returns immediately. On wait_timed_out, repeat while the task is pending. An unlock state of submitted requires a fresh screenshot to verify success. Waiting never connects or grants control.",
    async (p, ctx) => {
      if (!!p.session_id === !!p.view_id)
        throw new Failure("SELECT_ONE_SESSION");
      if (p.unlock_request_id) return output(await sessions.waitForUnlock(
        p.session_id, p.view_id, p.unlock_request_id, p.wait_ms ?? 25000, ctx.mcpReq.signal,
      ));
      if (p.session_id && p.wait_ms) return output(await sessions.waitForSession(p.session_id, p.wait_ms, ctx.mcpReq.signal));
      return output(p.view_id
        ? await sessions.waitForConnection(p.view_id, p.wait_ms ?? 25000, ctx.mcpReq.signal)
        : await sessions.status(p.session_id, undefined, ctx.mcpReq.signal));
    },
    { readOnly: true },
  );
  add("crossdesk_list_sessions", z.strictObject({}),
    "List the active remote sessions with their device aliases, displays and control state. Each session_id addresses one connection. Up to eight connections can be active concurrently.",
    async () => output(await sessions.listSessions()), { readOnly: true });
  add("crossdesk_set_recovery", z.strictObject({ session_id: id, enabled: z.boolean(),
    max_attempts: z.int().min(1).max(5).default(3) }),
    "Enable or disable bounded background connection recovery for a saved device with persistent authorization and credentials. Recovery uses a new session_id and never resubmits previous input, unlock or file operations. Wait with crossdesk_get_session and use its returned session_id plus a fresh screenshot before continuing. Human pause and explicit disconnect stop recovery.",
    async (p) => output(await sessions.setRecovery(p.session_id, p.enabled, p.max_attempts)));
  add("crossdesk_capture_displays", z.strictObject({ session_id: id,
    display_indices: z.array(z.int().min(0).max(63)).min(1).max(8)
      .refine((values) => new Set(values).size === values.length).optional(),
    max_edge: z.int().min(64).max(2048).default(1024) }),
    "Capture up to eight remote displays in sequence and restore the selected display. Returns all images and a capture_id valid for 30 seconds. Use capture_id for cross-display dragging. For ordinary actions select a display and capture a fresh observation.",
    async (p, ctx) => output(await sessions.captureDisplays(p, ctx.mcpReq.signal)));
  add("crossdesk_drag_across", z.strictObject({ session_id: id, operation_id: id, capture_id: id,
    path: z.array(z.strictObject({ display_index: z.int().min(0).max(63),
      x: z.number().finite().min(0).max(8192), y: z.number().finite().min(0).max(8192) })).min(2).max(32),
    button: z.enum(["left", "right", "middle"]).default("left"),
    duration_ms: z.int().min(100).max(10000).default(500) }),
    "Drag across displays using coordinates in the matching images from crossdesk_capture_displays. Keeps the mouse button held while switching displays. Always releases on interruption. Reuse the same operation_id to query an uncertain result. Display switching and network latency add to duration_ms.",
    async (p, ctx) => output(await sessions.dragAcross(p, ctx.mcpReq.signal)));
  add("crossdesk_video_settings", z.strictObject({ session_id: id,
    fps: z.union([z.literal(1), z.literal(8), z.literal(15), z.literal(30), z.literal(60)]).optional(),
    width: z.int().min(2).max(3840).refine((value) => value % 2 === 0).optional(),
    height: z.int().min(2).max(2160).refine((value) => value % 2 === 0).optional(),
    quality: z.int().min(0).max(2).optional(), max_bitrate_bps: z.int().min(128000).max(100000000).optional() }),
    "Adjust this connection's remote preview frame rate, resolution, quality and bitrate at runtime. Returns the remote preview acceptance status or wait_timed_out. Release held input before changing these settings.",
    async (p, ctx) => output(await sessions.videoSettings(p, ctx.mcpReq.signal)));
  add(
    "crossdesk_screenshot",
    screenshot,
    "Capture the remote display as a PNG image and immutable observation metadata. Coordinates in subsequent actions use this returned image. after_frame_id requires generation.",
    async (p, ctx) => output(await sessions.screenshot(p, ctx.mcpReq.signal)),
    { readOnly: true },
  );
  add(
    "crossdesk_action",
    z.strictObject({
      session_id: id,
      operation_id: id,
      observation_id: id,
      action,
      observe_after: z.boolean().default(true),
    }),
    "Submit one structured action against a recent observation. Reuse operation_id only to retrieve that same operation, never replay uncertain effects with a new ID. Human pause prevents control. A post-action screenshot is not an execution acknowledgement.",
    async (p, ctx) => output(await sessions.action(p, ctx.mcpReq.signal)),
  );
  add(
    "crossdesk_unlock",
    z.strictObject({
      session_id: id,
      operation_id: id,
      observation_id: id,
      use_saved: z.boolean().default(true),
    }),
    "Unlock the remote system after visually identifying its lock screen, focusing the password field and obtaining a fresh screenshot. Uses its separately saved system password, or asks through the local panel. Never supply passwords in chat or tool arguments. Repeat the same operation_id and parameters only to query that request. Wait with crossdesk_get_session and the returned unlock.request_id, then inspect a fresh screenshot. If the password was rejected, use_saved: false asks the user to replace it. Never automatically retry uncertain or rejected input.",
    async (p, ctx) => {
      let v = [...sessions.views.values()].find((view) => view.session === p.session_id);
      const created = !v;
      if (!v) v = sessions.create(sessions.choose(supported(ctx)));
      let result: Data;
      try { result = await sessions.requestUnlock(p, ctx.mcpReq.signal); }
      catch (error) { if (created) sessions.views.delete(v.id); throw error; }
      let panel = sessions.describe(v);
      try {
        if (result.unlock.state === "awaiting_password" && !v.ready) panel = await openPanel(v);
      } catch (error) { panel.panel_error = failure(error); }
      return viewResult(v, { ...panel, ...result, next_step: result.unlock.next_step });
    },
    { ui: true },
  );
  add(
    "crossdesk_wait",
    z.strictObject({ session_id: id, observation_id: id,
      condition: z.enum(["changed", "stable"]),
      stable_ms: z.int().min(100).max(5000).default(500),
      timeout_ms: z.int().min(0).max(25000).default(5000) }),
    "Wait for the observed screenshot region to change or remain stable. Returns matched, wait_timed_out and a fresh screenshot on success. A visual condition can be checked after an action with a separate screenshot observation. Remote input receipts report native input acceptance; verify the application's intended result visually.",
    async (p, ctx) => output(await sessions.waitFor(p, ctx.mcpReq.signal)),
    { readOnly: true },
  );
  add(
    "crossdesk_set_control",
    z.strictObject({ session_id: id, enabled: z.boolean() }),
    "Enable or disable control within the user-authorized scope. Cannot override human pause or upgrade an observe-only connection.",
    async (p) => output(await sessions.control(p.session_id, p.enabled)),
  );
  add("crossdesk_clipboard_get", z.strictObject({ session_id: id }),
    "Read the remote text clipboard within the authorized control session.",
    async (p, ctx) => output(await sessions.clipboard({ ...p, operation: "get" }, ctx.mcpReq.signal)),
    { readOnly: true });
  const filePath = z.string().max(4096).refine((path) => !path.includes("\0"));
  add("crossdesk_files_list", z.strictObject({ session_id: id, path: filePath.default(""),
    request_id: z.int().min(1).max(4294967295).optional(), offset: z.int().min(0).default(0) }),
    "List a remote directory. Empty path selects the remote home and @roots lists filesystem roots. Results contain up to 256 entries. Continue with request_id and next_offset while present. A timed-out listing remains queryable with its request_id.",
    async (p, ctx) => output(await sessions.files({ ...p, operation: "list" }, ctx.mcpReq.signal)),
    { readOnly: true });
  add("crossdesk_files_transfer", z.strictObject({ session_id: id, operation_id: id,
    direction: z.enum(["upload", "download"]), paths: z.array(filePath.min(1)).min(1).max(256),
    destination: filePath.min(1) }),
    "Upload local files/folders or download remote files/folders into an existing absolute destination directory. paths are absolute source paths. Returns a transfer operation to monitor with crossdesk_files_status. Reuse the same operation_id and arguments to query the original transfer. Existing destination files receive a distinct name on conflict.",
    async (p, ctx) => {
      const { direction, ...args } = p;
      return output(await sessions.files({ ...args, operation: direction }, ctx.mcpReq.signal));
    });
  add("crossdesk_files_status", z.strictObject({ session_id: id, operation_id: id,
    offset: z.int().min(0).default(0) }),
    "Get transfer progress, per-file completion and errors. Continue file entries with next_offset. Wait for completed, failed or cancelled before reporting a finished transfer.",
    async (p, ctx) => output(await sessions.files({ ...p, operation: "status" }, ctx.mcpReq.signal)),
    { readOnly: true });
  add("crossdesk_files_cancel", z.strictObject({ session_id: id, operation_id: id }),
    "Cancel a transfer and remove its unfinished staging files. Completed files remain available.",
    async (p, ctx) => output(await sessions.files({ ...p, operation: "cancel" }, ctx.mcpReq.signal)));
  add("crossdesk_system_status", z.strictObject({ session_id: id }),
    "Query the remote operating system's lock state and available secure-desktop service. unknown means the platform could not determine the state.",
    async (p, ctx) => output(await sessions.system({ ...p, command: "status" }, ctx.mcpReq.signal)),
    { readOnly: true });
  add("crossdesk_release_inputs", z.strictObject({ session_id: id }),
    "Release all keys and mouse buttons held by this session and cancel its current input action. Use when finishing a sequence of key_down/mouse_down actions or recovering from interruption. Held input expires after 15 seconds without another action.",
    async (p) => output(await sessions.releaseInputs(p.session_id)));
  add("crossdesk_system_command", z.strictObject({ session_id: id, operation_id: id,
    command: z.enum(["lock", "send_sas"]) }),
    "Lock the remote session or request Windows secure attention through its service. Requires control authorization. Reuse operation_id to query the same command. Check system status and a fresh screenshot after acceptance.",
    async (p, ctx) => output(await sessions.system(p, ctx.mcpReq.signal)));
  add("crossdesk_clipboard_set", z.strictObject({ session_id: id, operation_id: id,
    text: z.string().max(16384).refine((text) => Buffer.byteLength(text) <= 16384 && !text.includes("\0")) }),
    "Set or clear the remote text clipboard. Reuse the same operation_id to query the original result. Returns applied after the remote clipboard update.",
    async (p, ctx) => output(await sessions.clipboard({ ...p, operation: "set" }, ctx.mcpReq.signal)));
  add(
    "crossdesk_select_display",
    z.strictObject({ session_id: id, display_index: z.int().min(0).max(63) }),
    "Select a remote display. Invalidates previous observations; capture a new screenshot before any further input.",
    async (p) => output(await sessions.display(p.session_id, p.display_index)),
  );
  add(
    "crossdesk_disconnect",
    z.strictObject({ session_id: id }),
    "Disconnect the remote session, release input state and stop associated previews.",
    async (p) => output(await sessions.disconnect(p.session_id)),
  );
  for (const [name, schema] of Object.entries(uiSchemas)) {
    add(
      uiNames[name as UIAction],
      schema.extend({
        view_id: id,
        view_generation: z.int().min(1),
        ui_token: id,
      }),
      `CrossDesk panel only: ${name}.`,
      async (p, ctx) => {
        if (!apps(ctx)) throw new Failure("PANEL_UNSUPPORTED");
        const { view_id, view_generation, ui_token, ...args } = p;
        const v = sessions.authenticate(view_id, view_generation, ui_token);
        if (v.panel !== "apps") throw new Failure("VIEW_UNAUTHORIZED");
        // Video grants stay in the authenticated app-only response.
        const result = name === "video_open" ? await browser.video(v) : await uiAction(
          sessions,
          v,
          name,
          args,
          ctx.mcpReq.signal,
        );
        return {
          content: [
            {
              type: "text",
              text: name === "video_open" ? "Video viewer connection" : JSON.stringify(result),
            },
          ],
          structuredContent: result,
        };
      },
      { app: true },
    );
  }
  registerAppResource(
    server,
    "CrossDesk viewer",
    resource,
    { mimeType: RESOURCE_MIME_TYPE },
    async (uri, ctx) => {
      if (!apps(ctx)) throw new Failure("PANEL_UNSUPPORTED");
      return {
        contents: [
          {
            uri: uri.href,
            mimeType: RESOURCE_MIME_TYPE,
            text: await readFile(
              new URL("./viewer-apps.html", import.meta.url),
              "utf8",
            ),
            _meta: {
              ui: {
                prefersBorder: true,
                csp: { connectDomains: await browser.connectDomains(), resourceDomains: [] },
              },
            },
          },
        ],
      };
    },
  );
  // Request-scoped projection avoids global tool mutations when modern clients
  // send concurrent calls with different capability envelopes.
  server.server.setRequestHandler("tools/list", async (_req, ctx) => ({
    tools: [...definitions.values()]
      .filter((d) => !d.app || apps(ctx))
      .map((d) => {
        const inputSchema = z.toJSONSchema(d.schema, {
          io: "input",
        }) as Tool["inputSchema"];
        if (d.name === "crossdesk_get_session")
          inputSchema.oneOf = [
            { required: ["session_id"] },
            { required: ["view_id"] },
          ];
        return {
          name: d.name,
          description: d.description,
          inputSchema,
          annotations: {
            readOnlyHint: !!d.readOnly,
            destructiveHint: ["crossdesk_action", "crossdesk_unlock", "crossdesk_clipboard_set",
              "crossdesk_files_transfer", "crossdesk_files_cancel", "crossdesk_system_command", "crossdesk_drag_across"].includes(d.name),
            openWorldHint: true,
          },
          ...((d.ui || d.app) && apps(ctx)
            ? {
                _meta: {
                  ui: {
                    ...(d.ui ? { resourceUri: resource } : {}),
                    visibility: d.app ? ["app"] : ["model", "app"],
                  },
                  ...(d.ui ? { "ui/resourceUri": resource } : {}),
                },
              }
            : {}),
        };
      }),
  }));
  server.server.setRequestHandler("tools/call", async (req, ctx) => {
    try {
      const d = definitions.get(req.params.name);
      if (!d || (d.app && !apps(ctx))) throw new Failure("UNKNOWN_TOOL");
      if (Buffer.byteLength(JSON.stringify(req.params.arguments ?? {})) > 65536)
        throw new Failure("MESSAGE_TOO_LARGE");
      const p = parse(d.schema, req.params.arguments ?? {}) as Data;
      return await d.run(p, ctx);
    } catch (error) {
      const value = { error: failure(error) };
      return { ...output(value), isError: true };
    }
  });
  server.server.setRequestHandler("resources/list", async (_req, ctx) => ({
    resources: apps(ctx)
      ? [
          {
            name: "CrossDesk viewer",
            uri: resource,
            mimeType: RESOURCE_MIME_TYPE,
          },
        ]
      : [],
  }));
  return server;
}
