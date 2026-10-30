import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { truncateToWidth, parseKey, decodeKittyPrintable, isKeyRelease, getCellDimensions } from "@earendil-works/pi-tui";
import { spawn, type ChildProcess } from "node:child_process";
import type { Writable } from "node:stream";

const clean = (s: unknown) => String(s ?? "").replace(/[^\x20-\x7e]/g, "?").slice(0, 180);
const validAspect = (n: number) => Number.isFinite(n) && n >= 0.2 && n <= 2;
function aspectOf(width: number, height: number): number | undefined {
  const ratio = width / height;
  return width >= 1 && width <= 1024 && height >= 1 && height <= 1024 && validAspect(ratio) ? ratio : undefined;
}

// Normalize enhanced key reports with the host's actual keyboard decoder.
function legacyInput(data: string): string {
  if (isKeyRelease(data)) return "";
  const key = parseKey(data);
  if (key === "ctrl+]") return "\x1d";
  if (key?.match(/^ctrl\+[a-z]$/)) return String.fromCharCode(key.charCodeAt(5) - 96);
  if (key?.match(/^alt\+[a-z]$/)) return "\x1b" + key[4];
  const keys: Record<string, string> = { enter: "\r", escape: "\x1b", backspace: "\x7f", tab: "\t",
    up: "\x1b[A", down: "\x1b[B", right: "\x1b[C", left: "\x1b[D", home: "\x1b[H", end: "\x1b[F",
    delete: "\x1b[3~", pageUp: "\x1b[5~", pageDown: "\x1b[6~" };
  return (key && keys[key]) || decodeKittyPrintable(data) || data;
}

export default function crossdesk(pi: ExtensionAPI) {
  let stop: (() => void) | undefined;
  pi.on("session_shutdown", async () => stop?.());
  pi.registerCommand("crossdesk", {
    description: "Open a human-only remote desktop view (Ctrl+] for commands)",
    handler: async (args, ctx) => {
      if (ctx.mode !== "tui" || !ctx.hasUI || !process.stdin.isTTY || !process.stdout.isTTY) {
        ctx.ui.notify("CrossDesk embedded view requires Pi interactive TUI. Run crossdesk-cli <device-id> in a terminal.", "warning");
        return;
      }
      if (stop) { ctx.ui.notify("A CrossDesk view is already open", "warning"); return; }
      const configuredAspect = process.env.CROSSDESK_CELL_ASPECT?.trim();
      const manualAspect = !configuredAspect || configuredAspect === "auto" ? undefined : Number(configuredAspect);
      if (manualAspect !== undefined && !validAspect(manualAspect)) {
        ctx.ui.notify("CROSSDESK_CELL_ASPECT must be auto or a width/height ratio from 0.2 to 2", "warning");
        return;
      }
      const device = (args.trim() || await ctx.ui.input("CrossDesk device ID", "9 digits"))?.trim();
      if (!device) return;
      if (!/^\d{9}$/.test(device)) { ctx.ui.notify("Expected a 9-digit device ID", "error"); return; }
      // A custom masked prompt keeps credentials out of the editor, history,
      // extension messages, model tools and package configuration.
      let password = await ctx.ui.custom<string | undefined>((tui, _theme, _keys, done) => {
        let secret = "";
        return {
          render: (width: number) => [truncateToWidth("Remote password (Enter connects, Esc cancels): " + "*".repeat([...secret].length), width)],
          invalidate() {},
          handleInput(data: string) {
            data = legacyInput(data);
            if (!data) return;
            if (data === "\x1b" || data === "\x1d") { secret = ""; done(undefined); }
            else if (data === "\r" || data === "\n") { const value = secret; secret = ""; done(value); }
            else if (data === "\x7f" || data === "\b") secret = [...secret].slice(0, -1).join("");
            else {
              const text = data.replace(/\x1b\[200~|\x1b\[201~/g, "");
              if (!/[\x00-\x1f\x7f-\x9f]/.test(text) && Buffer.byteLength(secret + text) <= 256) secret += text;
            }
            tui.requestRender();
          },
        };
      });
      if (password === undefined) return;
      let child: ChildProcess | undefined;
      let killTimer: ReturnType<typeof setTimeout> | undefined;
      let stopGeometry: (() => void) | undefined;
      const cleanup = () => {
        stopGeometry?.(); stopGeometry = undefined;
        if (!child) return;
        const owned = child; child = undefined;
        owned.stdin?.end(); // EOF asks the bridge to disconnect and release input.
        killTimer = setTimeout(() => {
          owned.kill("SIGTERM");
          const force = setTimeout(() => owned.kill("SIGKILL"), 2000); force.unref();
          owned.once("exit", () => clearTimeout(force));
        }, 2000);
        killTimer.unref();
        owned.once("exit", () => { if (killTimer) clearTimeout(killTimer); });
      };
      try {
        await ctx.ui.custom<void>((tui, _theme, _keys, done) => {
          let closed = false, status = "starting", error = "", lines: string[] = [], buffer = "";
          let columns = 0, rows = 0, request = 0, ready = false, generation = 0;
          let frame: { frame_id: number; generation: number } | undefined;
          let helloId = 0, zoom = 1, native = false;
          let control = false, commandMode = false, bridgeStopped = false;
          const hostCell = getCellDimensions();
          let cellAspect = manualAspect ?? aspectOf(hostCell.widthPx, hostCell.heightPx) ?? 0.5;
          const colorHint = process.env.COLORTERM || "";
          const colors = /^(truecolor|24bit)$/.test(colorHint) ? 24 : process.env.TERM?.includes("256color") ? 256 : 8;
          const finish = () => { if (closed) return; closed = true; cleanup(); stop = undefined; lines = []; done(); };
          stop = finish;
          const fail = (reason: string) => { bridgeStopped = true; error = clean(reason); cleanup(); tui.requestRender(); };
          child = spawn(process.env.CROSSDESK_CLI || "crossdesk-cli", ["bridge", "--stdio", "--password-fd", "3"], {
            shell: false, stdio: ["pipe", "pipe", "pipe", "pipe"],
          });
          const processChild = child;
          const send = (method: string, fields: Record<string, unknown> = {}) => {
            if (closed || !child?.stdin || child.stdin.destroyed) return 0;
            if (child.stdin.writableLength > 65536) { fail("Bridge is not consuming requests"); return 0; }
            const id = ++request;
            child.stdin.write(JSON.stringify({ id, method, ...fields }) + "\n"); return id;
          };
          const sendViewport = () => send("viewport", {
            columns: columns || 80, rows: rows || 20, cell_aspect: cellAspect,
            colors, utf8: true, fps: 8, detail: "quadrant", quality: "high",
          });
          child.stdin?.on("error", () => fail("Bridge input closed"));
          const secrets = child.stdio[3] as Writable;
          secrets.on("error", () => fail("Credential channel closed"));
          secrets.end(password + "\n"); password = undefined;
          child.stderr?.resume(); // Never insert native diagnostics in the TUI or chat.
          child.on("error", () => fail("Cannot start crossdesk-cli; check CROSSDESK_CLI or PATH"));
          child.on("exit", (code) => {
            if (child === processChild) child = undefined;
            stopGeometry?.(); stopGeometry = undefined;
            if (!closed) { bridgeStopped = true; error ||= `Bridge exited (${code ?? "signal"})`; tui.requestRender(); }
          });
          child.stdout?.setEncoding("utf8");
          child.stdout?.on("data", (data: string) => {
            if (closed) return;
            buffer += data;
            if (buffer.length > 2 * 1024 * 1024) { fail("Bridge message exceeds 2 MiB"); return; }
            for (;;) {
              const end = buffer.indexOf("\n"); if (end < 0) break;
              const raw = buffer.slice(0, end); buffer = buffer.slice(end + 1);
              try {
                const message = JSON.parse(raw);
                if (message.id === helloId && message.ok && message.version === 1 && !ready) {
                  ready = true;
                  sendViewport();
                  send("connect", { device });
                }
                if (message.ok === false) error = clean(message.error);
                if (message.status) {
                  if (message.status.exit_requested) { clearTimeout(watchdog); finish(); return; }
                  if (Number.isSafeInteger(message.status.generation)) generation = message.status.generation;
                  control = message.status.control === true;
                  commandMode = message.status.command_mode === true;
                  status = clean(message.status.state) + (commandMode
                    ? " LOCAL COMMAND / " + (message.status.resume_control === true ? "CONTROL PAUSED" : "READ ONLY")
                    : control ? " CONTROL" : " READ ONLY");
                  if (message.status.error) error = clean(message.status.error);
                  if (message.status.state !== "connected") lines = [];
                }
                if (message.event === "frame") {
                  if (!Number.isSafeInteger(message.frame_id) || !Number.isSafeInteger(message.generation) ||
                      !Array.isArray(message.lines) || message.lines.length > 100)
                    throw new Error("Invalid frame");
                  if (message.columns !== columns || message.rows !== rows || message.generation < generation ||
                      (message.cell_aspect !== undefined && message.cell_aspect !== cellAspect)) {
                    send("ack", {frame_id: message.frame_id, generation: message.generation});
                    continue; // A frame already in flight may precede the resize response.
                  }
                  for (const line of message.lines) {
                    // Only our SGR colors, spaces and block glyphs may reach Pi.
                    if (typeof line !== "string" || line.length > 20000 || /[^ \u2580-\u259f]/u.test(line.replace(/\x1b\[[0-9;]*m/g, "")))
                      throw new Error("Invalid terminal line");
                  }
                  if (Number.isFinite(message.zoom) && message.zoom >= 1 && message.zoom <= 32) zoom = message.zoom;
                  native = message.native === true;
                  lines = message.lines; frame = { frame_id: message.frame_id, generation: message.generation };
                }
                tui.requestRender();
              } catch { fail("Invalid bridge protocol message"); }
            }
          });
          helloId = send("hello", { version: 1 });
          const watchdog = setTimeout(() => { if (!ready) fail("Bridge handshake timeout"); }, 5000);
          processChild.once("exit", () => clearTimeout(watchdog));
          // Use the host's terminal writer and sequence listener; Pi remains
          // the sole owner of raw input and screen rendering.
          let pixelSize: [number, number] | undefined, cellCount: [number, number] | undefined;
          let directCell = false, queried = false;
          const updateAspect = (width: number, height: number) => {
            const ratio = aspectOf(width, height);
            if (manualAspect !== undefined || ratio === undefined || ratio === cellAspect) return;
            cellAspect = ratio; lines = []; frame = undefined;
            if (ready) sendViewport();
            tui.requestRender();
          };
          const removeGeometryListener = tui.addInputListener(data => {
            // Pi's StdinBuffer assembles split replies before listeners run.
            const match = /^\x1b\[([468]);([0-9]+);([0-9]+)t$/.exec(data);
            if (!queried || !match) return;
            const type = Number(match[1]), height = Number(match[2]), width = Number(match[3]);
            if (![height, width].every(n => Number.isSafeInteger(n) && n > 0 && n <= 1048576))
              return { consume: true };
            if (type === 6) {
              if (aspectOf(width, height) === undefined) return { consume: true };
              directCell = true;
              updateAspect(width, height);
              return; // Let Pi update its own shared cell-size cache as well.
            }
            if (type === 4) pixelSize = [width, height];
            if (type === 8) cellCount = [width, height];
            if (!directCell && pixelSize && cellCount)
              updateAspect(pixelSize[0] / cellCount[0], pixelSize[1] / cellCount[1]);
            return { consume: true };
          });
          let terminalColumns = tui.terminal.columns, terminalRows = tui.terminal.rows;
          const queryGeometry = () => {
            if (closed || bridgeStopped) return;
            queried = true; directCell = false; pixelSize = cellCount = undefined;
            tui.terminal.write("\x1b[16t\x1b[14t\x1b[18t");
          };
          const geometryTimer = setInterval(queryGeometry, 2000);
          geometryTimer.unref();
          stopGeometry = () => { clearInterval(geometryTimer); removeGeometryListener(); };
          queryGeometry();
          return {
            render(width: number) {
              const w = Math.max(1, Math.min(240, width - 1));
              const h = Math.max(1, Math.min(100, tui.terminal.rows - 5));
              if (w !== columns || h !== rows) {
                columns = w; rows = h; lines = []; frame = undefined;
                if (ready) sendViewport();
              }
              if (terminalColumns !== tui.terminal.columns || terminalRows !== tui.terminal.rows) {
                terminalColumns = tui.terminal.columns; terminalRows = tui.terminal.rows;
                queryGeometry();
              }
              const ack = frame; frame = undefined;
              if (ack) queueMicrotask(() => send("ack", ack)); // Host consumed the frame; not a physical display timestamp.
              const hint = bridgeStopped ? "q / Esc / Ctrl+C close"
                : commandMode ? "Esc/Ctrl+C return | c toggle q quit | arrows/ijkl pan +/- zoom f fit d detail"
                : control ? "Keys -> remote | Ctrl+] then c release, q quit"
                : "c control q quit ? help | i up j left k down l right | +/- zoom f fit d detail";
              return [truncateToWidth(`CrossDesk ${device} | ${status} ${native ? "detail" : "fit"} x${zoom} | Ctrl+] commands`, width),
                ...lines.slice(0, h).map(line => truncateToWidth(line, width)),
                truncateToWidth(`${hint}${error ? " | " + error : lines.length ? "" : " | Waiting for remote video…"}`, width)];
            },
            handleInput(data: string) {
              data = legacyInput(data);
              if (!data) return;
              if ((bridgeStopped || !ready) && (data === "q" || data === "\x1b" || data === "\x03")) { clearTimeout(watchdog); finish(); }
              else if (ready && Buffer.byteLength(data) <= 16384) send("input", { data });
            },
            invalidate() {},
            dispose() { clearTimeout(watchdog); cleanup(); },
          };
        });
      } finally { password = undefined; cleanup(); stop = undefined; }
    },
  });
}
