import { build } from "esbuild";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../integrations/deepseek-harness");
const dist = resolve(root, "../../dist");
await build({ entryPoints: [resolve(root, "src/plugin.js")], outfile: resolve(dist, "harness.js"),
  bundle: true, format: "esm", platform: "node", target: "node22",
  packages: "external" });
await build({ entryPoints: [resolve(root, "src/host.js")], outfile: resolve(root, "dist/host.js"),
  bundle: true, format: "esm", platform: "node", target: "node22" });
for (const [id, outfile] of [
  ["@chunkdash/crossdesk-mcp", resolve(dist, "harness-client.js")],
  ["@chunkdash/dsh-crossdesk-sidebar", resolve(root, "dist/client.js")],
]) await build({ entryPoints: [resolve(root, "src/client.js")], outfile,
    bundle: true, format: "cjs", platform: "browser", target: "es2022",
    banner: { js: `window.__ModuleLoader__.load({ id: ${JSON.stringify(id)}, factory: (require) => { var module = { exports: {} }; var exports = module.exports;` },
    footer: { js: "return module.exports; } });" } });
