import { createInterface } from "node:readline/promises";
import { emitKeypressEvents } from "node:readline";
import { Writable } from "node:stream";
import { setTimeout as delay } from "node:timers/promises";
import { Failure, failure, type Data } from "./common.js";
import { TerminalClient } from "./terminal.js";

async function connect(client: TerminalClient) {
  let hidden = false;
  const output = new Writable({ write(chunk, _encoding, done) {
    if (!hidden) process.stdout.write(chunk);
    done();
  } });
  const rl = createInterface({ input: process.stdin, output, terminal: true, historySize: 0 });
  const ask = async (label: string) => (await rl.question(label)).trim();
  try {
    const { devices, defaults } = await client.call("devices");
    const status = await client.call("status");
    process.stdout.write("CrossDesk MCP · 终端画面\n");
    for (const d of devices)
      process.stdout.write(`${d.device_alias} (${d.device_id})\n`);
    const selected = (await ask(`设备别名或 9 位 ID${status.device_alias ? ` [${status.device_alias}]` : ""}：`)) || status.device_alias;
    const device = devices.find((d: Data) => d.device_alias === selected);
    const args: Data = device ? { device_alias: selected } : {
      device_id: selected,
      server: (await ask(`服务器 [${defaults.server}]：`)) || defaults.server,
      port: Number((await ask(`端口 [${defaults.port}]：`)) || defaults.port),
    };
    args.mode = (await ask("允许 Agent 操作？[y/N]：")).toLowerCase() === "y" ? "control" : "observe";
    args.display_index = Number((await ask("显示器索引 [0]：")) || "0");
    const saved = device?.credential_provider !== "none" && !!device;
    if (!saved || (await ask("使用已保存凭据？[Y/n]：")).toLowerCase() === "n") {
      process.stdout.write("访问密码（隐藏输入，可为空）：");
      hidden = true;
      try { args.password = await rl.question(""); }
      finally { hidden = false; process.stdout.write("\n"); }
    }
    process.stdout.write("正在连接…\n");
    try { await client.call("connect", args); }
    finally { args.password = undefined; }
  } finally { rl.close(); }
}

async function unlock(client: TerminalClient, requestId: string, device: string) {
  let hidden = false;
  const output = new Writable({ write(chunk, _encoding, done) {
    if (!hidden) process.stdout.write(chunk);
    done();
  } });
  const rl = createInterface({ input: process.stdin, output, terminal: true, historySize: 0 });
  const controller = new AbortController();
  rl.once("SIGINT", () => controller.abort());
  const args: Data = { request_id: requestId };
  try {
    process.stdout.write("Agent 请求解锁远程系统。请确认远端系统密码框已聚焦。\n系统锁屏密码（隐藏输入，留空取消）：");
    hidden = true;
    try { args.password = await rl.question("", { signal: controller.signal }); }
    finally { hidden = false; process.stdout.write("\n"); }
    if (!args.password) args.cancel = true;
    else {
      const { devices } = await client.call("devices");
      if (devices.some((entry: Data) => entry.device_alias === device))
        args.remember_password = (await rl.question("保存到系统凭据库并允许以后自动解锁？[y/N]：", { signal: controller.signal })).trim().toLowerCase() === "y";
    }
    return await client.call("unlock", args);
  } catch (error) {
    if (!controller.signal.aborted) throw error;
    return await client.call("unlock", { request_id: requestId, cancel: true });
  } finally { args.password = undefined; rl.close(); }
}

export async function terminalView(directory: string, view: string, cellAspect = 0.5) {
  if (!process.stdin.isTTY || !process.stdout.isTTY || process.env.TERM === "dumb")
    throw new Failure("VIEW_REQUIRES_TERMINAL");
  if (!Number.isFinite(cellAspect) || cellAspect < 0.2 || cellAspect > 2)
    throw new Failure("INVALID_ARGUMENT");
  const client = await TerminalClient.attach(directory, view);
  let raw = false, stopped = false, controls = false, message = "";
  const previousRaw = process.stdin.isRaw;
  const stop = () => { stopped = true; };
  const keypress = (_text: string, key: { name?: string; ctrl?: boolean; meta?: boolean; sequence?: string }) => {
    if (key.name === "q" || (key.ctrl && key.name === "c")) { stop(); return; }
    if (controls || key.ctrl || key.meta || !["p", "r", "d"].includes(key.sequence ?? "")) return;
    controls = true;
    void client.call("control", { operation: key.sequence === "p" ? "pause" : key.sequence === "r" ? "resume" : "disconnect" })
      .then(() => { message = ""; if (key.sequence === "d") stopped = true; })
      .catch((error) => { message = failure(error).code; })
      .finally(() => { controls = false; });
  };
  const write = (value: string) => new Promise<void>((resolve, reject) => {
    process.stdout.write(value, (error) => error ? reject(error) : resolve());
  });
  try {
    let status = await client.call("status");
    if (!status.session_id) await connect(client);
    process.stdin.setRawMode(true);
    raw = true;
    emitKeypressEvents(process.stdin);
    process.stdin.on("keypress", keypress);
    process.stdin.resume();
    for (const signal of ["SIGINT", "SIGTERM", "SIGHUP"] as const) process.on(signal, stop);
    await write("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
    let statusAt = 0, size = "", promptedUnlock = "";
    const colors = /truecolor|24bit/i.test(process.env.COLORTERM ?? "") ? 24
      : /256color/i.test(process.env.TERM ?? "") ? 256 : 8;
    while (!stopped) {
      const started = Date.now();
      if (started - statusAt >= 500) {
        status = await client.call("status", {}, 7000);
        statusAt = Date.now();
        if (["closed", "failed", "disconnected"].includes(status.state)) break;
      }
      if (status.unlock?.state === "awaiting_password" && status.unlock.request_id !== promptedUnlock && !controls) {
        promptedUnlock = status.unlock.request_id;
        process.stdin.off("keypress", keypress);
        process.stdin.setRawMode(false);
        await write("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
        try {
          const result = await unlock(client, promptedUnlock, status.device_alias);
          message = result.unlock?.error?.code ?? result.unlock?.persistence_error ?? "";
        } catch (error) { message = failure(error).code; }
        finally {
          process.stdin.setRawMode(true);
          process.stdin.on("keypress", keypress);
          process.stdin.resume();
          await write("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
          statusAt = 0;
          size = "";
        }
        continue;
      }
      // Leave a spare column so terminals with unusual wrap behavior stay safe.
      const columns = Math.min(160, Math.max(1, (process.stdout.columns || 80) - 1));
      const rows = Math.min(60, Math.max(1, (process.stdout.rows || 24) - 2));
      const nextSize = `${columns}:${rows}`;
      let screen = nextSize !== size ? "\x1b[2J" : "";
      size = nextSize;
      try {
        const frame = await client.call("frame", { columns, rows, colors, cell_aspect: cellAspect }, 7000);
        if (frame.columns !== columns || frame.rows !== rows || !Array.isArray(frame.lines) || frame.lines.length !== rows)
          throw new Failure("INVALID_FRAME");
        // Only renderer-generated SGR sequences and single-width block glyphs.
        for (const [index, line] of frame.lines.entries()) {
          if (typeof line !== "string" || !/^(?:\x1b\[[0-9;]*m|[ \u2580-\u259f])*$/u.test(line))
            throw new Failure("INVALID_FRAME");
          screen += `\x1b[${index + 1};1H${line}\x1b[0m\x1b[K`;
        }
        const cursor = frame.cursor;
        if (cursor && Number.isInteger(cursor.column) && Number.isInteger(cursor.row) &&
            cursor.column >= 0 && cursor.column < columns && cursor.row >= 0 && cursor.row < rows)
          screen += `\x1b[${cursor.row + 1};${cursor.column + 1}H\x1b[97;44m↖\x1b[0m`;
      } catch (error) {
        if (!["NOT_READY", "BUSY", "FRAME_TIMEOUT", "STALE_OBSERVATION"].includes(failure(error).code)) throw error;
      }
      const label = status.human_paused ? "PAUSED" : status.control ? "AGENT CONTROL" : "OBSERVE";
      const bar = `${label} | p pause / r resume / d disconnect / q close view${message ? ` | ${message}` : ""}`;
      // No remote strings or credentials are interpolated into terminal control output.
      screen += `\x1b[${rows + 1};1H\x1b[0m${bar.replace(/[^\x20-\x7e]/g, "").slice(0, columns)}\x1b[K`;
      await write(screen);
      await delay(Math.max(0, 125 - (Date.now() - started)));
    }
  } finally {
    process.stdin.off("keypress", keypress);
    for (const signal of ["SIGINT", "SIGTERM", "SIGHUP"] as const) process.off(signal, stop);
    if (raw) {
      process.stdin.setRawMode(previousRaw);
      process.stdin.pause();
      process.stdout.write("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
    }
    await client.call("detach", {}, 1000).catch(() => {});
  }
}
