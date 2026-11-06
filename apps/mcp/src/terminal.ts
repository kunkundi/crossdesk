import { createServer, createConnection, type Server, type Socket } from "node:net";
import { chmod, readFile, readdir, unlink, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { z } from "zod";
import { Failure, equal, failure, id, parse, token, type Data } from "./common.js";
import { Sessions, type View } from "./sessions.js";
import { uiAction } from "./ui-actions.js";

const frameSchema = z.strictObject({
  columns: z.int().min(1).max(160),
  rows: z.int().min(1).max(60),
  colors: z.union([z.literal(8), z.literal(24), z.literal(256)]),
  cell_aspect: z.number().finite().min(0.2).max(2),
});

// A private local channel, separate from MCP stdio and browser HTTP. Only the
// user's terminal receives rendered frames and connection credentials.
export class TerminalPanel {
  private server?: Server;
  private starting?: Promise<void>;
  private endpoint?: string;
  private descriptor?: string;
  private closed = false;
  private sockets = new Set<Socket>();
  private frames = new Set<string>();
  private frameTimes = new Map<string, number>();
  constructor(private sessions: Sessions) {}

  async open(view: View): Promise<Data> {
    await this.start();
    for (const id of this.frameTimes.keys())
      if (!this.sessions.views.has(id)) this.frameTimes.delete(id);
    return {
      ...this.sessions.describe(view),
      terminal_command: {
        command: "crossdesk-mcp",
        args: ["view", "--view", view.id, "--config-dir", this.sessions.store.directory],
      },
      next_step: "Show terminal_command to the user to run in a separate interactive terminal on this host. It displays the existing MCP session and accepts credentials locally. Never run it inside MCP stdio or capture its video into chat. Then wait with crossdesk_get_session(view_id, wait_ms: 25000).",
    };
  }

  private async start() {
    if (this.closed) throw new Failure("PANEL_CLOSED");
    this.starting ??= (async () => {
      const secret = token(), suffix = token().slice(0, 20);
      this.endpoint = process.platform === "win32"
        ? `\\\\.\\pipe\\crossdesk-terminal-${suffix}`
        : join(tmpdir(), `cd-term-${suffix}.sock`);
      this.server = createServer((socket) => {
        this.sockets.add(socket);
        const controller = new AbortController();
        socket.on("error", () => {});
        socket.once("close", () => {
          this.sockets.delete(socket);
          controller.abort();
        });
        socket.setTimeout(45000, () => socket.destroy());
        let buffer = Buffer.alloc(0), consumed = false;
        socket.on("data", (chunk: Buffer) => {
          if (consumed) return socket.destroy();
          buffer = Buffer.concat([buffer, chunk]);
          if (buffer.length > 65536) return socket.destroy();
          if (!buffer.includes(10)) return;
          consumed = true;
          void (async () => {
            try {
              const p = JSON.parse(buffer.toString("utf8"));
              buffer = Buffer.alloc(0);
              if (typeof p.secret !== "string" || !equal(secret, p.secret))
                throw new Failure("PANEL_UNAUTHORIZED");
              const view = this.sessions.view(parse(id, p.view_id));
              if (view.panel !== "terminal" || !["auto", "terminal"].includes(this.sessions.policy))
                throw new Failure("PANEL_POLICY_DENIED");
              view.lastSeen = Date.now();
              let result: Data;
              if (p.method === "frame") {
                const args = parse(frameSchema, p.args);
                if (this.frames.has(view.id) || Date.now() - (this.frameTimes.get(view.id) ?? 0) < 100)
                  throw new Failure("BUSY", false, true);
                this.frames.add(view.id);
                this.frameTimes.set(view.id, Date.now());
                try { result = await this.sessions.terminalFrame(view, args, controller.signal); }
                finally { this.frames.delete(view.id); }
              } else if (p.method === "detach") {
                view.ready = false;
                result = this.sessions.describe(view);
              } else {
                result = await uiAction(this.sessions, view, p.method, p.args, controller.signal);
              }
              socket.end(JSON.stringify({ result }) + "\n");
            } catch (error) {
              socket.end(JSON.stringify({ error: failure(error) }) + "\n");
            }
          })();
        });
      });
      await new Promise<void>((resolve, reject) => {
        this.server!.once("error", reject);
        this.server!.listen(this.endpoint!, resolve);
      });
      if (process.platform !== "win32") await chmod(this.endpoint, 0o600);
      this.descriptor = join(this.sessions.store.directory, `terminal-${process.pid}-${suffix}.json`);
      await writeFile(this.descriptor, JSON.stringify({ endpoint: this.endpoint, secret }), { mode: 0o600, flag: "wx" });
      if (this.closed) throw new Failure("PANEL_CLOSED");
    })().catch(async (error) => {
      await this.cleanup();
      this.starting = undefined;
      throw error;
    });
    return this.starting;
  }

  private async cleanup() {
    for (const socket of this.sockets) socket.destroy();
    if (this.server?.listening) this.server.close();
    if (this.descriptor) await unlink(this.descriptor).catch(() => {});
    if (this.endpoint && process.platform !== "win32") await unlink(this.endpoint).catch(() => {});
  }
  async close() {
    this.closed = true;
    await this.starting?.catch(() => {});
    await this.cleanup();
  }
}

export class TerminalClient {
  private constructor(private endpoint: string, private secret: string, private view: string) {}
  static async attach(directory: string, view: string) {
    parse(id, view);
    for (const file of await readdir(directory)) {
      if (!/^terminal-\d+-[a-zA-Z0-9_-]+\.json$/.test(file)) continue;
      try {
        const d = JSON.parse(await readFile(join(directory, file), "utf8"));
        const client = new TerminalClient(d.endpoint, d.secret, view);
        await client.call("ready", {}, 2000);
        return client;
      } catch { /* An unrelated or exited MCP instance. */ }
    }
    throw new Failure("PANEL_NOT_FOUND");
  }
  call(method: string, args: Data = {}, timeout = 45000): Promise<Data> {
    return new Promise((resolve, reject) => {
      const socket = createConnection(this.endpoint);
      let buffer = Buffer.alloc(0), settled = false;
      const finish = (error?: Failure, result?: Data) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        socket.destroy();
        if (error) reject(error); else resolve(result!);
      };
      const timer = setTimeout(() => finish(new Failure("PANEL_TIMEOUT")), timeout);
      socket.on("error", () => finish(new Failure("PANEL_CLOSED")));
      socket.once("close", () => finish(new Failure("PANEL_CLOSED")));
      socket.once("connect", () => socket.write(JSON.stringify({ secret: this.secret, view_id: this.view, method, args }) + "\n"));
      socket.on("data", (chunk: Buffer) => {
        buffer = Buffer.concat([buffer, chunk]);
        if (buffer.length > 2 * 1024 * 1024) return finish(new Failure("MESSAGE_TOO_LARGE"));
        if (!buffer.includes(10)) return;
        try {
          const reply = JSON.parse(buffer.toString("utf8"));
          if (reply.error) finish(new Failure(reply.error.code,
            !!reply.error.side_effect_possible, !!reply.error.retryable, reply.error.reason));
          else if (reply.result && typeof reply.result === "object") finish(undefined, reply.result);
          else finish(new Failure("INVALID_RESPONSE"));
        } catch { finish(new Failure("INVALID_RESPONSE")); }
      });
    });
  }
}
