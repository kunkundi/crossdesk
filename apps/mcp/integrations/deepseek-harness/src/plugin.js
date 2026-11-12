import { fileURLToPath } from "node:url";
import { isAbsolute } from "node:path";
import z from "@deepseek-ai/schemastery";
import { Config as McpConfig, apply as applyMcp } from "@deepseek-ai/dsh-mcp-client";
import { nativeBinary } from "../../../dist/native.js";
import { apply as applySidebar } from "./host.js";

export const name = "crossdesk";
export const inject = ["tools", "connection"];
export const Config = z.object({
  serverName: z.string().pattern(/^[a-zA-Z0-9_-]{1,24}$/).default("crossdesk"),
  nativeBinary: z.string(),
  configDir: z.string(),
});

// Built into dist/harness.js beside the ordinary MCP entry. DSH supplies its
// own MCP client and Node/Electron executable; no PATH or profile edits needed.
export async function apply(ctx, config) {
  for (const field of ["nativeBinary", "configDir"])
    if (config[field] !== undefined && !isAbsolute(config[field]))
      throw new Error(`CrossDesk ${field} must be an absolute path`);
  let binary;
  try { binary = await nativeBinary(config.nativeBinary); }
  catch (error) {
    throw new Error("CrossDesk native CLI is unavailable or incompatible. Install the platform bundle with the native CLI included, or set nativeBinary for a source build.", { cause: error });
  }
  const args = [fileURLToPath(new URL("./main.js", import.meta.url)), "serve", "--stdio",
    "--panel=browser", "--browser-launch=host", "--native-binary", binary];
  if (config.configDir) args.push("--config-dir", config.configDir);
  // Reuse the host's client implementation so namespace reservation, tool
  // permissions, images, reconnect and awaited disposal have a single owner.
  // Duplicate legacy MCP entries are rejected before another server is spawned.
  await applyMcp(ctx, McpConfig({
    serverName: config.serverName, transport: "stdio", command: process.execPath, args,
    env: process.versions.electron ? { ELECTRON_RUN_AS_NODE: "1" } : {},
    toolCallTimeoutMs: 90000, failOnStartupError: true,
  }));
  applySidebar(ctx, { serverName: config.serverName });
}
