import { build } from "esbuild";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../integrations/deepseek-harness");
await build({ entryPoints: [resolve(root, "src/host.js")], outfile: resolve(root, "dist/host.js"),
  bundle: true, format: "esm", platform: "node", target: "node22" });
await build({ entryPoints: [resolve(root, "src/client.js")], outfile: resolve(root, "dist/client.js"),
  bundle: true, format: "cjs", platform: "browser", target: "es2022",
  banner: { js: 'window.__ModuleLoader__.load({ id: "@chunkdash/dsh-crossdesk-sidebar", factory: (require) => { var module = { exports: {} }; var exports = module.exports;' },
  footer: { js: "return module.exports; } });" } });
