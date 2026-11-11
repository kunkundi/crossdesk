import {
  createServer,
  type IncomingMessage,
  type ServerResponse,
  type Server,
} from "node:http";
import {
  createServer as createIPC,
  createConnection,
  type Server as IPCServer,
} from "node:net";
import { spawn } from "node:child_process";
import { readFile, writeFile, unlink, chmod, readdir, mkdtemp, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath, pathToFileURL } from "node:url";
import { Failure, token, equal, failure, type BrowserLaunch, type Data } from "./common.js";
import { Sessions, type View } from "./sessions.js";
import { uiAction } from "./ui-actions.js";
import { displayEnvironment } from "./environment.js";
import { browserNextStep, type BrowserHost } from "./browser-host.js";

const uiFile = fileURLToPath(new URL("./viewer-browser.html", import.meta.url));
interface BrowserSession {
  csrf: string;
  views: Set<string>;
  expires: number;
}
interface BrowserGrant {
  view: string;
  generation: number;
  expires: number;
  directory?: string;
  launcher?: string;
  timer?: ReturnType<typeof setTimeout>;
}
export async function openURL(url: string): Promise<boolean> {
  if (!displayEnvironment().gui) return false;
  const command =
    process.platform === "darwin"
      ? "open"
      : process.platform === "win32"
        ? "rundll32.exe"
        : "xdg-open";
  const args =
    process.platform === "win32" ? ["url.dll,FileProtocolHandler", url] : [url];
  return new Promise((resolve) => {
    const child = spawn(command, args, {
      stdio: "ignore",
      shell: false,
      windowsHide: true,
    });
    const timeout = setTimeout(() => {
      child.unref();
      resolve(true);
    }, 1500);
    child.once("error", () => {
      clearTimeout(timeout);
      resolve(false);
    });
    child.once("exit", (code) => {
      clearTimeout(timeout);
      resolve(code === 0);
    });
  });
}
export class BrowserPanel {
  private closed = false;
  private server?: Server;
  private starting?: Promise<void>;
  private ipc?: IPCServer;
  private ipcPath?: string;
  private descriptor?: string;
  private origin = "";
  private cookie = "crossdesk_" + token();
  private grants = new Map<string, BrowserGrant>();
  private launchers = new Map<string, string>();
  private clients = new Map<string, BrowserSession>();
  private html = "";
  constructor(private sessions: Sessions, private launch: BrowserLaunch = "auto") {}
  async connectDomains() {
    await this.start();
    // Native processes own ephemeral video ports, including after reconnect.
    // Restrict the wildcard to ports on the literal loopback address.
    return [this.origin, "ws://127.0.0.1:*"];
  }
  async video(view: View) {
    await this.start();
    if (this.closed) throw new Failure("PANEL_CLOSED");
    return this.sessions.openVideo(view, this.origin);
  }
  async open(view: View, host?: BrowserHost): Promise<Data> {
    if (this.closed) throw new Failure("PANEL_CLOSED");
    await this.start();
    if (this.closed) throw new Failure("PANEL_CLOSED");
    const { key, grant, url } = this.bootstrap(view);
    const launch = this.launch === "auto" ? (host ? "host" : "system") : this.launch;
    let launcher: string | undefined;
    let opened = false;
    try {
      if (launch === "host" && host === "deepseek-harness") {
        // Harness rejects file: navigation. Keep the bootstrap credential out
        // of tool results and exchange a separate single-use loopback launcher.
        grant.launcher = token();
        this.launchers.set(grant.launcher, key);
        launcher = `${this.origin}/launch/${grant.launcher}`;
      } else if (launch === "host") {
        // The host opens a local document, never a model-visible bearer URL.
        // Its browser gets its own cookie through the normal bootstrap flow.
        grant.directory = await mkdtemp(join(tmpdir(), "crossdesk-mcp-launch-"));
        const path = join(grant.directory, "open.html");
        await writeFile(path,
          `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="referrer" content="no-referrer"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; base-uri 'none'"><meta http-equiv="refresh" content="0;url=${url}"><title>CrossDesk MCP</title></head><body><p>正在打开 CrossDesk…</p><a href="${url}" rel="noreferrer">打开画面</a></body></html>`,
          { mode: 0o600, flag: "wx" });
        if (this.closed || this.grants.get(key) !== grant) throw new Failure("PANEL_CLOSED");
        launcher = pathToFileURL(path).href;
      } else opened = await openURL(url);
    } catch (error) {
      await this.removeGrant(key, grant);
      throw error;
    }
    return {
      ...this.sessions.describe(view),
      browser_opened: opened,
      browser_url: this.origin,
      browser_launch: launch,
      ...(host ? { browser_host: host } : {}),
      ...(launcher ? {
        browser_launch_url: launcher,
        browser_launch_expires_in_ms: Math.max(0, grant.expires - Date.now()),
        browser_launch_expires_at_ms: grant.expires,
        next_step: browserNextStep(host),
      } : {}),
      pairing_command: {
        command: "crossdesk-mcp",
        args: [
          "setup",
          "--open-panel",
          "--view",
          view.id,
          "--config-dir",
          this.sessions.store.directory,
        ],
      },
    };
  }
  private async removeGrant(key: string, grant: BrowserGrant) {
    if (this.grants.get(key) === grant) this.grants.delete(key);
    if (grant.launcher) this.launchers.delete(grant.launcher);
    clearTimeout(grant.timer);
    if (grant.directory) await rm(grant.directory, { recursive: true, force: true });
  }
  private bootstrap(view: View) {
    if (this.closed) throw new Failure("PANEL_CLOSED");
    if (view.panel !== "browser" || !["auto", "browser"].includes(this.sessions.policy))
      throw new Failure("PANEL_POLICY_DENIED");
    if (this.grants.size >= 32) throw new Failure("VIEW_LIMIT");
    const key = token();
    const grant: BrowserGrant = { view: view.id, generation: view.generation, expires: Date.now() + 60000 };
    this.grants.set(key, grant);
    grant.timer = setTimeout(() => void this.removeGrant(key, grant).catch(() => {}), 60000);
    grant.timer.unref();
    return { key, grant, url: `${this.origin}/#bootstrap=${key}` };
  }
  private async start() {
    this.starting ??= (async () => {
      this.html = await readFile(uiFile, "utf8");
      if (this.closed) throw new Failure("PANEL_CLOSED");
      this.server = createServer((req, res) => {
        void this.handle(req, res).catch(() => {
          if (!res.headersSent)
            this.respond(res, 500, { error: { code: "INTERNAL_ERROR" } });
          else res.end();
        });
      });
      // No upgrade handler or video bytes in the Node HTTP server.
      this.server.requestTimeout = 45000;
      this.server.headersTimeout = 10000;
      this.server.maxHeadersCount = 32;
      await new Promise<void>((resolve, reject) => {
        this.server!.once("error", reject);
        this.server!.listen(0, "127.0.0.1", resolve);
      });
      this.origin = `http://127.0.0.1:${(this.server.address() as import("node:net").AddressInfo).port}`;
      if (this.closed) throw new Failure("PANEL_CLOSED");
      await this.startIPC();
      if (this.closed) throw new Failure("PANEL_CLOSED");
    })().catch(async (error) => {
      await this.close(false);
      this.starting = undefined;
      throw error;
    });
    return this.starting;
  }
  private respond(res: ServerResponse, code: number, value: Data) {
    res.writeHead(code, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify(value));
  }
  private client(req: IncomingMessage): BrowserSession | undefined {
    const value = req.headers.cookie
      ?.split(";")
      .map((s) => s.trim())
      .find((s) => s.startsWith(this.cookie + "="))
      ?.slice(this.cookie.length + 1);
    const client = value ? this.clients.get(value) : undefined;
    return client && client.expires > Date.now() ? client : undefined;
  }
  private async body(req: IncomingMessage): Promise<Data> {
    if (req.headers["content-type"] !== "application/json")
      throw new Failure("CONTENT_TYPE_REQUIRED");
    let bytes = 0;
    const chunks: Buffer[] = [];
    for await (const chunk of req) {
      bytes += chunk.length;
      if (bytes > 65536) throw new Failure("MESSAGE_TOO_LARGE");
      chunks.push(chunk);
    }
    try {
      const value = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      if (!value || Array.isArray(value) || typeof value !== "object")
        throw new Error();
      return value;
    } catch {
      throw new Failure("INVALID_ARGUMENT");
    }
  }
  private async handle(req: IncomingMessage, res: ServerResponse) {
    res.setHeader("Cache-Control", "no-store");
    res.setHeader("Referrer-Policy", "no-referrer");
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("X-Frame-Options", "DENY");
    const authority = this.origin.slice("http://".length);
    if (
      req.headers.host !== authority ||
      (req.headers.origin && req.headers.origin !== this.origin)
    )
      return this.respond(res, 403, { error: { code: "ORIGIN_DENIED" } });
    const launcher = req.url?.match(/^\/launch\/([a-zA-Z0-9_-]+)$/)?.[1];
    if (launcher && req.method === "GET") {
      const key = this.launchers.get(launcher);
      const grant = key ? this.grants.get(key) : undefined;
      const view = grant ? this.sessions.views.get(grant.view) : undefined;
      if (!key || !grant || grant.expires <= Date.now() || !view ||
          view.panel !== "browser" || view.generation !== grant.generation) {
        if (key && grant) await this.removeGrant(key, grant);
        return this.respond(res, 410, { error: { code: "LAUNCHER_EXPIRED" } });
      }
      this.launchers.delete(launcher);
      grant.launcher = undefined;
      res.writeHead(302, { Location: `${this.origin}/#bootstrap=${key}` });
      res.end();
      return;
    }
    if (req.url === "/" && req.method === "GET") {
      const nonce = token();
      res.setHeader(
        "Content-Security-Policy",
        `default-src 'none'; script-src 'nonce-${nonce}'; style-src 'nonce-${nonce}'; img-src data:; connect-src 'self' ws://127.0.0.1:*; base-uri 'none'; form-action 'none'; frame-ancestors 'none'`,
      );
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      res.end(this.html.replaceAll("__CROSSDESK_NONCE__", nonce));
      return;
    }
    if (req.url === "/api/context" && req.method === "GET") {
      const client = this.client(req);
      if (!client)
        return this.respond(res, 401, { error: { code: "PAIRING_REQUIRED" } });
      for (const id of client.views)
        if (!this.sessions.views.has(id)) client.views.delete(id);
      if (!client.views.size)
        return this.respond(res, 401, { error: { code: "PAIRING_REQUIRED" } });
      return this.respond(res, 200, {
        csrf: client.csrf,
        views: [...client.views].map((v) =>
          this.sessions.describe(this.sessions.view(v)),
        ),
      });
    }
    if (req.method !== "POST" || req.headers.origin !== this.origin)
      return this.respond(res, 403, { error: { code: "ORIGIN_DENIED" } });
    try {
      const body = await this.body(req);
      if (req.url === "/api/bootstrap") {
        const grant =
          typeof body.token === "string"
            ? this.grants.get(body.token)
            : undefined;
        const view = grant ? this.sessions.views.get(grant.view) : undefined;
        if (!grant || grant.expires < Date.now() || !view ||
            view.panel !== "browser" || view.generation !== grant.generation)
          throw new Failure("PAIRING_REQUIRED");
        await this.removeGrant(body.token, grant);
        if (this.closed || this.sessions.views.get(grant.view) !== view ||
            view.panel !== "browser" || view.generation !== grant.generation)
          throw new Failure("PAIRING_REQUIRED");
        for (const [id, client] of this.clients)
          if (client.expires < Date.now()) this.clients.delete(id);
        const existing = this.client(req);
        if (!existing && this.clients.size >= 32)
          throw new Failure("VIEW_LIMIT");
        const session = token(),
          client = existing ?? {
            csrf: token(),
            views: new Set<string>(),
            expires: 0,
          };
        client.views.add(grant.view);
        client.expires = Date.now() + 12 * 60 * 60 * 1000;
        for (const [key, previous] of this.clients)
          if (previous === client) this.clients.delete(key);
        this.clients.set(session, client);
        res.setHeader(
          "Set-Cookie",
          `${this.cookie}=${session}; HttpOnly; SameSite=Strict; Path=/api`,
        );
        return this.respond(res, 200, {
          csrf: client.csrf,
          ...this.sessions.describe(view),
        });
      }
      const client = this.client(req);
      if (
        !client ||
        typeof req.headers["x-crossdesk-csrf"] !== "string" ||
        !equal(client.csrf, req.headers["x-crossdesk-csrf"])
      )
        throw new Failure("PANEL_UNAUTHORIZED");
      if (typeof body.view_id !== "string" || !client.views.has(body.view_id))
        throw new Failure("VIEW_UNAUTHORIZED");
      const name = req.url?.match(
        /^\/api\/(ready|devices|status|connect|video_open|control|unlock|recovery|input|audio)$/,
      )?.[1];
      if (!name) throw new Failure("UNKNOWN_ACTION");
      const { view_id, ...args } = body;
      const controller = new AbortController();
      res.once("close", () => {
        if (!res.writableEnded) controller.abort();
      });
      const result = name === "video_open" ? await this.video(this.sessions.view(view_id)) : await uiAction(
        this.sessions,
        this.sessions.view(view_id),
        name,
        args,
        controller.signal,
      );
      this.respond(res, 200, result);
    } catch (error) {
      this.respond(res, 400, { error: failure(error) });
    }
  }
  private async startIPC() {
    const secret = token();
    const suffix = token().slice(0, 20);
    this.ipcPath =
      process.platform === "win32"
        ? `\\\\.\\pipe\\crossdesk-mcp-${suffix}`
        : join(tmpdir(), `cd-mcp-${suffix}.sock`);
    this.ipc = createIPC((socket) => {
      socket.setTimeout(5000, () => socket.destroy());
      let data = "",
        consumed = false;
      socket.on("error", () => {});
      socket.on("data", (part) => {
        data += part.toString("utf8");
        if (data.length > 2048) {
          socket.destroy();
          return;
        }
        if (consumed || !data.includes("\n")) return;
        consumed = true;
        void (async () => {
          try {
            const p = JSON.parse(data);
            if (
              typeof p.secret !== "string" ||
              !equal(p.secret, secret) ||
              typeof p.view_id !== "string"
            )
              throw new Failure("PAIRING_DENIED");
            const v = this.sessions.view(p.view_id);
            if (
              v.panel !== "browser" ||
              !["auto", "browser"].includes(this.sessions.policy)
            )
              throw new Failure("PANEL_POLICY_DENIED");
            // Only the authenticated local setup process receives this URL.
            // It uses its own desktop environment to open the browser.
            socket.end(
              JSON.stringify({ ok: true, url: this.bootstrap(v).url }) + "\n",
            );
          } catch {
            socket.end('{"ok":false}\n');
          }
        })();
      });
    });
    await new Promise<void>((resolve, reject) => {
      this.ipc!.once("error", reject);
      this.ipc!.listen(this.ipcPath!, resolve);
    });
    if (process.platform !== "win32") await chmod(this.ipcPath, 0o600);
    this.descriptor = join(
      this.sessions.store.directory,
      `run-${process.pid}-${suffix}.json`,
    );
    await writeFile(
      this.descriptor,
      JSON.stringify({ pid: process.pid, endpoint: this.ipcPath, secret }),
      { mode: 0o600, flag: "wx" },
    );
  }
  async close(final = true) {
    if (final) this.closed = true;
    await this.sessions.revokeVideo();
    await Promise.all([...this.grants].map(([key, grant]) => this.removeGrant(key, grant)));
    this.clients.clear();
    this.server?.closeAllConnections();
    if (this.server?.listening) this.server.close();
    if (this.ipc?.listening) this.ipc.close();
    if (this.descriptor) await unlink(this.descriptor).catch(() => {});
    if (this.ipcPath && process.platform !== "win32")
      await unlink(this.ipcPath).catch(() => {});
  }
}
export async function pairBrowser(
  directory: string,
  view: string,
): Promise<string | undefined> {
  for (const file of await readdir(directory)) {
    if (!/^run-\d+-[a-zA-Z0-9_-]+\.json$/.test(file)) continue;
    try {
      const descriptor = JSON.parse(
        await readFile(join(directory, file), "utf8"),
      );
      const url = await new Promise<string | undefined>((resolve) => {
        const socket = createConnection(descriptor.endpoint);
        let data = "";
        socket.setTimeout(5000, () => {
          socket.destroy();
          resolve(undefined);
        });
        socket.once("error", () => resolve(undefined));
        socket.once("close", () => resolve(undefined));
        socket.once("connect", () =>
          socket.write(
            JSON.stringify({ secret: descriptor.secret, view_id: view }) + "\n",
          ),
        );
        socket.on("data", (part) => {
          data += part;
          if (data.length > 2048) socket.destroy();
        });
        socket.once("end", () => {
          try {
            const result = JSON.parse(data);
            resolve(
              result.ok &&
                typeof result.url === "string" &&
                /^http:\/\/127\.0\.0\.1:\d+\/#bootstrap=[a-zA-Z0-9_-]+$/.test(
                  result.url,
                )
                ? result.url
                : undefined,
            );
          } catch {
            resolve(undefined);
          }
        });
      });
      if (url) return url;
    } catch {
      /* Another MCP instance may have exited. */
    }
  }
  return undefined;
}
