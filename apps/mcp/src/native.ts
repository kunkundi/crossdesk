import { spawn, execFile, type ChildProcess } from "node:child_process";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { promisify } from "node:util";
import type { Writable } from "node:stream";
import { Failure, type Data, type VideoOptions } from "./common.js";

export const NATIVE_VERSION = "0.2.0";
const run = promisify(execFile);
const require = createRequire(import.meta.url);
function nativeEnvironment(): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = {};
  for (const key of [
    "PATH",
    "HOME",
    "USERPROFILE",
    "SystemRoot",
    "SYSTEMROOT",
    "WINDIR",
    "TEMP",
    "TMP",
    "TMPDIR",
    "LANG",
    "LC_ALL",
    "SSL_CERT_FILE",
    "SSL_CERT_DIR",
  ])
    if (process.env[key] !== undefined) env[key] = process.env[key];
  return env;
}
export async function nativeBinary(override?: string): Promise<string> {
  let binary: string;
  if (override) binary = resolve(override);
  else {
    const target = `${process.platform === "win32" ? "windows" : process.platform}-${process.arch}`;
    if (
      ![
        "darwin-arm64",
        "darwin-x64",
        "linux-arm64",
        "linux-x64",
        "windows-x64",
      ].includes(target)
    )
      throw new Failure("PLATFORM_UNSUPPORTED");
    if (
      process.platform === "linux" &&
      !(process.report.getReport() as Data).header.glibcVersionRuntime
    )
      throw new Failure("GLIBC_REQUIRED");
    try {
      const manifest = require.resolve(
        `@chunkdash/crossdesk-cli-${target}/package.json`,
      );
      if (require(manifest).version !== NATIVE_VERSION)
        throw new Failure("NATIVE_VERSION_MISMATCH");
      binary = join(
        dirname(manifest),
        "bin",
        process.platform === "win32" ? "crossdesk-cli.exe" : "crossdesk-cli",
      );
    } catch (error) {
      throw error instanceof Failure
        ? error
        : new Failure("NATIVE_PACKAGE_MISSING");
    }
  }
  try {
    const { stdout } = await run(binary, ["doctor", "--json"], {
      timeout: 5000,
      maxBuffer: 65536,
      windowsHide: true,
      env: nativeEnvironment(),
    });
    const doctor = JSON.parse(stdout);
    if (doctor.automation_protocol !== "crossdesk-automation/1" ||
        doctor.automation_video_transport !== "loopback-websocket/1" ||
        !["h264-annexb/1", "nv12/1"].every((p) => doctor.automation_video_protocols?.includes(p)) ||
        !["auto", "software"].every((p) => doctor.automation_video_decoders?.includes(p)))
      throw new Failure("NATIVE_PROTOCOL_MISMATCH");
  } catch (error) {
    throw error instanceof Failure ? error : new Failure("NATIVE_START_FAILED");
  }
  return binary;
}

type Pending = {
  resolve: (value: Data) => void;
  reject: (error: Failure) => void;
  clean: () => void;
  action: boolean;
};
export class NativeClient {
  private child: ChildProcess;
  private pending = new Map<number, Pending>();
  private sequence = 0;
  private buffer = "";
  private closed = false;
  private closing?: Promise<void>;
  constructor(binary: string, server: string, port: number, logs: string,
    readonly videoOptions: VideoOptions = { decode_path: "browser", decoder: "auto" }) {
    this.child = spawn(
      binary,
      [
        "agent",
        "--stdio",
        "--password-fd",
        "3",
        "--video-ws",
        "--server",
        server,
        "--port",
        String(port),
        "--log-dir",
        logs,
      ],
      {
        stdio: ["pipe", "pipe", "ignore", "pipe"],
        shell: false,
        windowsHide: true,
        env: nativeEnvironment(),
      },
    );
    this.child.stdout!.setEncoding("utf8");
    this.child.stdout!.on("data", (part: string) => this.receive(part));
    this.child.stdin!.on("error", () => this.fail());
    this.child.stdio[3]!.on("error", () => this.fail());
    this.child.once("error", () => this.fail());
    this.child.once("exit", () => this.fail());
  }
  async connect(
    device: string,
    password: string,
    mode: string,
    display: number,
    signal?: AbortSignal,
  ): Promise<Data> {
    if (Buffer.byteLength(password) > 256 || /[\0\r\n]/.test(password))
      throw new Failure("INVALID_CREDENTIAL");
    const hello = await this.request(
      "hello",
      { protocol: "crossdesk-automation/1" },
      5000,
      signal,
    );
    if (hello.protocol !== "crossdesk-automation/1" ||
        hello.video_transport !== "loopback-websocket/1" ||
        !hello.video_protocols?.includes(this.videoOptions.decode_path === "minirtc" ? "nv12/1" : "h264-annexb/1") ||
        !hello.video_decoders?.includes(this.videoOptions.decoder))
      throw new Failure("NATIVE_PROTOCOL_MISMATCH");
    const secret = Buffer.from(password + "\n");
    password = "";
    (this.child.stdio[3] as Writable).end(secret, () => secret.fill(0));
    return this.request("connect", { device, mode, display, ...this.videoOptions }, 7000, signal);
  }
  request(
    method: string,
    args: Data = {},
    timeout = 7000,
    signal?: AbortSignal,
  ): Promise<Data> {
    if (this.closed)
      return Promise.reject(new Failure("SESSION_CLOSED", method === "action"));
    if (signal?.aborted) return Promise.reject(new Failure("CANCELLED"));
    if (this.pending.size >= 32 || this.child.stdin!.writableLength > 262144)
      return Promise.reject(new Failure("BUSY", false, true));
    const id = ++this.sequence;
    const line = JSON.stringify({ ...args, id, method }) + "\n";
    if (Buffer.byteLength(line) > 65536)
      return Promise.reject(new Failure("MESSAGE_TOO_LARGE"));
    return new Promise((resolve, reject) => {
      const cancel = (code: string) => {
        const p = this.pending.get(id);
        if (!p) return;
        this.pending.delete(id);
        p.clean();
        // Cancellation is best effort; a timed-out action is never replayed.
        this.child.stdin?.write(
          JSON.stringify({
            id: ++this.sequence,
            method: "cancel",
            request_id: id,
          }) + "\n",
        );
        reject(new Failure(code, method === "action"));
      };
      const abort = () => cancel("CANCELLED");
      const timer = setTimeout(() => cancel("NATIVE_TIMEOUT"), timeout);
      signal?.addEventListener("abort", abort, { once: true });
      this.pending.set(id, {
        resolve,
        reject,
        action: method === "action",
        clean: () => {
          clearTimeout(timer);
          signal?.removeEventListener("abort", abort);
        },
      });
      this.child.stdin!.write(line);
    });
  }
  private receive(part: string) {
    this.buffer += part;
    if (Buffer.byteLength(this.buffer) > 8 * 1024 * 1024) {
      this.fail();
      this.child.kill();
      return;
    }
    for (;;) {
      const end = this.buffer.indexOf("\n");
      if (end < 0) break;
      const line = this.buffer.slice(0, end);
      this.buffer = this.buffer.slice(end + 1);
      try {
        if (Buffer.byteLength(line) > 4 * 1024 * 1024) throw new Error();
        const reply = JSON.parse(line);
        if (!Number.isSafeInteger(reply.id) || typeof reply.ok !== "boolean")
          throw new Error();
        const p = this.pending.get(reply.id);
        if (!p) continue;
        this.pending.delete(reply.id);
        p.clean();
        if (reply.ok) p.resolve(reply.result);
        else
          p.reject(
            new Failure(
              /^[A-Z_]+$/.test(reply.error?.code)
                ? reply.error.code
                : "NATIVE_ERROR",
              !!reply.error?.side_effect_possible,
              !!reply.error?.retryable,
            ),
          );
      } catch {
        this.fail();
        this.child.kill();
        return;
      }
    }
  }
  private fail() {
    this.closed = true;
    for (const p of this.pending.values()) {
      p.clean();
      p.reject(new Failure("SESSION_CLOSED", p.action));
    }
    this.pending.clear();
  }
  close(): Promise<void> {
    if (this.closing) return this.closing;
    this.closing = (async () => {
      if (!this.closed)
        await this.request("disconnect", {}, 1000).catch(() => {});
      this.child.stdin?.end();
      if (this.child.exitCode === null && this.child.signalCode === null)
        await new Promise<void>((resolve) => {
          const kill = setTimeout(() => {
            this.child.kill("SIGKILL");
            resolve();
          }, 2000);
          this.child.once("exit", () => {
            clearTimeout(kill);
            resolve();
          });
        });
      this.fail();
    })();
    return this.closing;
  }
}
