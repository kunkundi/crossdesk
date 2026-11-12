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
const { values } = parseArgs({ options: {
  "out-dir": { type: "string" },
  "native-package-dir": { type: "string" },
} });
const output = resolve(values["out-dir"] ?? join(repository, "build/npm-mcp"));
const project = JSON.parse(await readFile(join(root, "package.json"), "utf8"));
const cli = JSON.parse(await readFile(join(root, "../cli/package.json"), "utf8"));
const harness = JSON.parse(await readFile(join(root, "integrations/deepseek-harness/package.json"), "utf8"));
const platforms = JSON.parse(
  await readFile(join(root, "../cli/npm/platforms.json"), "utf8"),
);
const native = await import(new URL("../dist/native.js", import.meta.url));
const nativeVersion = (await readFile(join(root, "src/native.ts"), "utf8"))
  .match(/export const NATIVE_VERSION = "([^"]+)";/)?.[1];
if (project.version !== cli.version || project.version !== harness.version || project.version !== nativeVersion)
  throw new Error("CLI, MCP, DSH and NATIVE_VERSION must match. Run python3 scripts/version.py set mcp VERSION from the repository root.");
if (!nativeVersion || native.NATIVE_VERSION !== nativeVersion)
  throw new Error("Built MCP native version differs from source. Run npm run build first.");
for (const file of ["main.js", "viewer-browser.html", "viewer-apps.html", "harness.js", "harness-client.js"])
  await access(join(root, "dist", file));
const staging = await mkdtemp(join(tmpdir(), "crossdesk-mcp-pack-"));
try {
  for (const entry of [
    "dist",
    "src",
    "ui",
    "scripts",
    "integrations",
    "cordis.patch.yml",
    "tsconfig.json",
    "tsconfig.ui.json",
    "package-lock.json",
    "README.md",
    "DEVELOPMENT.md",
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
      "cordis.patch.yml",
      "tsconfig.json",
      "tsconfig.ui.json",
      "build-package.json",
      "package-lock.json",
      "README.md",
      "DEVELOPMENT.md",
      "LICENSE",
    ],
    optionalDependencies: Object.fromEntries(
      Object.values(platforms).map((p) => [p.packageName, nativeVersion]),
    ),
    publishConfig: { access: "public" },
  };
  delete manifest.scripts;
  delete manifest.devDependencies;
  // Platform bundles are installable before the native version reaches npm.
  // Preserve the native package's source, notices and build provenance too.
  if (values["native-package-dir"]) {
    const directory = resolve(values["native-package-dir"]);
    const bundled = JSON.parse(await readFile(join(directory, "package.json"), "utf8"));
    const platform = Object.values(platforms).find(p => p.packageName === bundled.name);
    if (!platform || bundled.version !== native.NATIVE_VERSION ||
        JSON.stringify(bundled.os) !== JSON.stringify([platform.os]) ||
        JSON.stringify(bundled.cpu) !== JSON.stringify([platform.cpu]) ||
        (platform.libc && JSON.stringify(bundled.libc) !== JSON.stringify([platform.libc])))
      throw new Error("Expected a compatible CrossDesk native platform package.");
    for (const entry of ["bin", "source", "LICENSE", "build-info.json"])
      await access(join(directory, entry));
    const binary = join(directory, "bin", platform.os === "win32" ? "crossdesk-cli.exe" : "crossdesk-cli");
    await access(binary);
    if (platform.os === process.platform && platform.cpu === process.arch)
      await native.nativeBinary(binary);
    const target = `${platform.os === "win32" ? "windows" : platform.os}-${platform.cpu}`;
    await cp(directory, join(staging, "native", target), { recursive: true });
    manifest.files.push("native");
    manifest.os = [platform.os];
    manifest.cpu = [platform.cpu];
    if (platform.libc) manifest.libc = [platform.libc];
    delete manifest.optionalDependencies;
  }
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
