#!/usr/bin/env node
import { parseArgs } from "node:util";
import { createInterface } from "node:readline/promises";
import { Writable } from "node:stream";
import {
  serveStdio,
  StdioServerTransport,
} from "@modelcontextprotocol/server/stdio";
import { DeviceStore, type Device } from "./store.js";
import { Sessions } from "./sessions.js";
import { BrowserPanel, pairBrowser, openURL } from "./browser.js";
import { createServer } from "./server.js";
import { TerminalPanel } from "./terminal.js";
import { terminalView } from "./terminal-view.js";
import {
  panel,
  parse,
  Failure,
  failure,
  uiConnect,
  videoOptions,
  type Panel,
} from "./common.js";

async function setup(store: DeviceStore, view?: string) {
  if (!process.stdin.isTTY || !process.stdout.isTTY)
    throw new Failure("SETUP_REQUIRES_TERMINAL");
  if (view) {
    const url = await pairBrowser(store.directory, view);
    if (!url) throw new Failure("PANEL_NOT_FOUND");
    if (await openURL(url)) process.stdout.write("浏览器配对入口已打开。\n");
    else
      process.stdout.write(
        `请在本机浏览器打开下方一次性链接（60 秒有效，不要发送到聊天）：\n${url}\n`,
      );
    return;
  }
  let hidden = false;
  const output = new Writable({
    write(chunk, _encoding, done) {
      if (!hidden) process.stdout.write(chunk);
      done();
    },
  });
  const rl = createInterface({
    input: process.stdin,
    output,
    terminal: true,
    historySize: 0,
  });
  const question = async (label: string) => (await rl.question(label)).trim();
  try {
    const config = await store.config();
    process.stdout.write(
      "CrossDesk MCP · 本地设备配置\n密码只保存到系统凭据库，不写入设备配置。\n",
    );
    const name = await question("设备别名：");
    const previous = config.devices.find((d) => d.alias === name);
    const device = previous?.device_id ?? (await question("9 位设备 ID："));
    const server =
      previous?.server ??
      ((await question(`服务器 [${config.server}]：`)) || config.server);
    const port =
      previous?.port ??
      Number((await question(`端口 [${config.port}]：`)) || config.port);
    const permission = await question(
      "允许未来自动连接？[n / observe / control]：",
    );
    const environment = await question(
      "使用现有密码环境变量（留空使用系统凭据库）：",
    );
    let password: string | undefined;
    if (!environment) {
      hidden = true;
      process.stdout.write("访问密码（隐藏输入，可为空）：");
      try {
        password = await rl.question("");
      } finally {
        hidden = false;
        process.stdout.write("\n");
      }
    }
    parse(uiConnect, {
      device_id: device,
      label: name,
      server,
      port,
      password,
    });
    const d: Device = {
      alias: name,
      device_id: device,
      server,
      port,
      ...(permission === "observe" || permission === "control"
        ? { authorization: permission }
        : {}),
      ...(environment
        ? { credential: "environment", environment_variable: environment }
        : {}),
    };
    await store.save(d, password);
    password = undefined;
    process.stdout.write(
      "已保存。Agent 可调用 crossdesk_list_devices；自动连接仍需有效授权和凭据。\n",
    );
  } finally {
    hidden = false;
    rl.close();
  }
}
async function main() {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    strict: true,
    options: {
      stdio: { type: "boolean" },
      panel: { type: "string" },
      "video-path": { type: "string" },
      "video-decoder": { type: "string" },
      "config-dir": { type: "string" },
      "native-binary": { type: "string" },
      "open-panel": { type: "boolean" },
      view: { type: "string" },
      "cell-aspect": { type: "string" },
      help: { type: "boolean" },
      version: { type: "boolean" },
    },
  });
  if (values.version) {
    process.stdout.write("0.1.0\n");
    return;
  }
  if (values.help || !positionals.length) {
    process.stdout.write(
      "crossdesk-mcp serve --stdio [--panel auto|apps|browser|terminal|off] [--video-path browser|minirtc] [--video-decoder auto|software] [--config-dir directory]\ncrossdesk-mcp view --view VIEW_ID [--config-dir directory] [--cell-aspect 0.5]\ncrossdesk-mcp setup [--config-dir directory]\ncrossdesk-mcp setup --open-panel --view VIEW_ID [--config-dir directory]\nDevelopment: --native-binary /absolute/path/to/crossdesk-cli\n",
    );
    return;
  }
  if (positionals.length !== 1 || !["serve", "setup", "view"].includes(positionals[0]!))
    throw new Failure("INVALID_COMMAND");
  const store = new DeviceStore(
    values["config-dir"] ?? process.env.CROSSDESK_MCP_CONFIG_DIR,
  );
  await store.initialize();
  if (positionals[0] === "view") {
    if (!values.view) throw new Failure("VIEW_REQUIRED");
    await terminalView(store.directory, values.view, Number(values["cell-aspect"] ?? 0.5));
    return;
  }
  if (positionals[0] === "setup") {
    if (values["open-panel"] && !values.view)
      throw new Failure("VIEW_REQUIRED");
    await setup(store, values["open-panel"] ? values.view : undefined);
    return;
  }
  if (!values.stdio) throw new Failure("STDIO_REQUIRED");
  const config = await store.config();
  const policy: Panel = parse(
    panel,
    values.panel ?? config.panel,
  );
  const sessions = new Sessions(
    store,
    policy,
    values["native-binary"] ?? process.env.CROSSDESK_NATIVE_BINARY,
    parse(videoOptions, {
      decode_path: values["video-path"] ?? config.video.decode_path,
      decoder: values["video-decoder"] ?? config.video.decoder,
    }),
  );
  const browser = new BrowserPanel(sessions);
  const terminal = new TerminalPanel(sessions);
  const handle = serveStdio(() => createServer(sessions, browser, terminal), {
    transport: new StdioServerTransport(),
    onerror: () => process.stderr.write("crossdesk-mcp: protocol error\n"),
  });
  let closing = false;
  const close = async () => {
    if (closing) return;
    closing = true;
    await terminal.close();
    await browser.close();
    await sessions.close();
    await handle.close();
  };
  process.stdin.once("end", () => {
    void close();
  });
  for (const signal of ["SIGINT", "SIGTERM", "SIGHUP"] as const)
    process.once(signal, () => {
      void close().finally(() => {
        process.exitCode = 0;
      });
    });
  process.once("uncaughtException", () => {
    process.stderr.write("crossdesk-mcp: INTERNAL_ERROR\n");
    void close().finally(() => {
      process.exitCode = 1;
    });
  });
}
main().catch((error) => {
  const result = failure(error);
  process.stderr.write(`crossdesk-mcp: ${result.code}${result.reason ? ` (${result.reason})` : ""}\n`);
  process.exitCode = 1;
});
