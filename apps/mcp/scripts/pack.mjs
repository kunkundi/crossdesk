import {
  mkdtemp,
  readFile,
  writeFile,
  mkdir,
  cp,
  chmod,
  rm,
  access,
} from "node:fs/promises";
import { dirname, resolve, join } from "node:path";
import { fileURLToPath } from "node:url";
import { tmpdir } from "node:os";
import { execFileSync } from "node:child_process";
import { parseArgs } from "node:util";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repository = resolve(root, "../..");
const { values } = parseArgs({ options: { "out-dir": { type: "string" } } });
const output = resolve(values["out-dir"] ?? join(repository, "build/npm-mcp"));
const project = JSON.parse(await readFile(join(root, "package.json"), "utf8"));
const cli = JSON.parse(
  await readFile(join(root, "../cli/package.json"), "utf8"),
);
const platforms = JSON.parse(
  await readFile(join(root, "../cli/npm/platforms.json"), "utf8"),
);
const native = await import(new URL("../dist/native.js", import.meta.url));
if (native.NATIVE_VERSION !== cli.version)
  throw new Error("MCP and CLI native versions must match.");
for (const file of ["main.js", "viewer-browser.html", "viewer-apps.html"])
  await access(join(root, "dist", file));
const staging = await mkdtemp(join(tmpdir(), "crossdesk-mcp-pack-"));
try {
  for (const entry of [
    "dist",
    "src",
    "ui",
    "scripts",
    "integrations",
    "tsconfig.json",
    "tsconfig.ui.json",
    "package-lock.json",
    "README.md",
  ])
    await cp(join(root, entry), join(staging, entry), { recursive: true });
  await cp(join(repository, "LICENSE"), join(staging, "LICENSE"));
  await writeFile(
    join(staging, "build-package.json"),
    JSON.stringify(project, null, 2) + "\n",
  );
  const manifest = {
    ...project,
    private: false,
    files: [
      "dist",
      "src",
      "ui",
      "scripts/build-ui.mjs",
      "scripts/build-harness.mjs",
      "integrations",
      "tsconfig.json",
      "tsconfig.ui.json",
      "build-package.json",
      "package-lock.json",
      "README.md",
      "LICENSE",
    ],
    optionalDependencies: Object.fromEntries(
      Object.values(platforms).map((p) => [p.packageName, cli.version]),
    ),
    publishConfig: { access: "public" },
  };
  delete manifest.scripts;
  delete manifest.devDependencies;
  await writeFile(
    join(staging, "package.json"),
    JSON.stringify(manifest, null, 2) + "\n",
  );
  await chmod(join(staging, "dist/main.js"), 0o755);
  await mkdir(output, { recursive: true });
  const filename = `${project.name.replace(/^@/, "").replace("/", "-")}-${project.version}.tgz`;
  try {
    await access(join(output, filename));
    throw new Error(`Output already exists: ${filename}`);
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
  }
  const args = [
    "pack",
    "--json",
    "--ignore-scripts",
    "--pack-destination",
    output,
  ];
  if (process.platform === "win32" && !process.env.npm_execpath)
    throw new Error("On Windows use npm run pack:npm.");
  const result = execFileSync(
    process.env.npm_execpath ? process.execPath : "npm",
    process.env.npm_execpath ? [process.env.npm_execpath, ...args] : args,
    { cwd: staging, encoding: "utf8" },
  );
  const parsed = JSON.parse(result);
  const packages = Array.isArray(parsed) ? parsed : Object.values(parsed);
  if (packages.length !== 1 || packages[0].filename !== filename)
    throw new Error("Unexpected package archive.");
  process.stdout.write(join(output, filename) + "\n");
} finally {
  await rm(staging, { recursive: true, force: true });
}
