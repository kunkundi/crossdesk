import { build } from "esbuild";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
// Unmodified apps/desktop/resources/linux/crossdesk_128x128.png.
// Keep a local copy so the npm source package rebuilds without desktop assets.
const logo = "data:image/png;base64," +
  (await readFile(resolve(root, "ui/assets/crossdesk.png"))).toString("base64");
await mkdir(resolve(root, "dist"), { recursive: true });
for (const mode of ["apps", "browser"]) {
  const result = await build({
    entryPoints: [resolve(root, "ui/viewer.ts")],
    bundle: true,
    minify: true,
    platform: "browser",
    target: "es2022",
    outfile: "viewer.js",
    write: false,
    define: {
      CROSSDESK_APPS: String(mode === "apps"),
      CROSSDESK_LOGO: JSON.stringify(logo),
    },
  });
  const script = result.outputFiles
    .find((f) => f.path.endsWith(".js"))
    .text.replaceAll("</script", "<\\/script");
  const css = result.outputFiles.find((f) => f.path.endsWith(".css")).text;
  const nonce = mode === "browser" ? ' nonce="__CROSSDESK_NONCE__"' : "";
  await writeFile(
    resolve(root, `dist/viewer-${mode}.html`),
    `<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#ffffff"><link rel="icon" type="image/png" href="${logo}"><title>CrossDesk MCP</title><style${nonce}>${css}</style></head><body><div id="app"></div><script${nonce}>${script}</script></body></html>`,
  );
}
