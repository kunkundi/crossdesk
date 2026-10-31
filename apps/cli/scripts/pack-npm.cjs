'use strict';

const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const cliRoot = path.resolve(__dirname, '..');
const root = path.resolve(cliRoot, '../..');
const project = require('../package.json');
const platforms = require('../npm/platforms.json');

function run(command, args, options = {}) {
  return execFileSync(command, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...options });
}
function copy(from, to) {
  fs.cpSync(path.join(root, from), to, {
    recursive: true,
    filter: (entry) => !['.git', '.xmake', '.vscode', 'build', 'out', 'node_modules', 'tests', 'test'].includes(path.basename(entry)),
  });
}
function json(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`);
}
function tarballName(name, version) {
  // npm pack writes @scope/name as scope-name-version.tgz.
  return `${name.replace(/^@/, '').replace('/', '-')}-${version}.tgz`;
}
function inspect(binary, target) {
  const data = fs.readFileSync(binary);
  const info = { target, sha256: crypto.createHash('sha256').update(data).digest('hex') };
  if (process.platform === 'darwin') {
    const cpu = process.arch === 'arm64' ? 0x100000c : 0x1000007;
    if (data.length < 32 || data.readUInt32LE(0) !== 0xfeedfacf ||
        data.readUInt32LE(4) !== cpu || data.readUInt32LE(12) !== 2) {
      throw new Error('Expected a Mach-O executable matching the current CPU.');
    }
    info.dependencies = run('otool', ['-L', binary]).split('\n').slice(1)
      .map((line) => line.trim().split(' (')[0]).filter(Boolean);
    for (const dependency of info.dependencies) {
      if (!dependency.startsWith('/usr/lib/') && !dependency.startsWith('/System/Library/'))
        throw new Error(`Non-system macOS library must be bundled before packaging: ${dependency}`);
    }
    const commands = run('otool', ['-l', binary]);
    info.minimumOS = commands.match(/\bminos\s+([\d.]+)/)?.[1] ||
      commands.match(/LC_VERSION_MIN_MACOSX[\s\S]*?\bversion\s+([\d.]+)/)?.[1];
    if (!info.minimumOS) throw new Error('Cannot determine macOS deployment target.');
    run('codesign', ['--verify', binary]);
  } else if (process.platform === 'linux') {
    const machine = process.arch === 'arm64' ? 183 : 62;
    if (data.length < 64 || data.subarray(0, 4).toString('hex') !== '7f454c46' ||
        data[4] !== 2 || data[5] !== 1 || data.readUInt16LE(18) !== machine)
      throw new Error('Expected a 64-bit ELF executable matching the current CPU.');
    if (!process.report.getReport().header.glibcVersionRuntime)
      throw new Error('The Linux npm packages currently target glibc, not musl.');
    info.dependencies = run('ldd', [binary]).trim().split('\n').map((line) => line.trim());
    const system = /^(linux-vdso|ld-linux|lib(c|m|dl|rt|pthread|gcc_s|stdc\+\+|z|resolv)\.)/;
    for (const dependency of info.dependencies) {
      const name = path.basename(dependency.split(/\s/)[0]);
      if (dependency.includes('not found') || !system.test(name))
        throw new Error(`Unbundled Linux dependency: ${dependency}`);
    }
    const symbols = run('objdump', ['-T', binary]);
    info.glibc = [...symbols.matchAll(/\bGLIBC_([\d.]+)/g)].map((match) => match[1])
      .sort((a, b) => a.localeCompare(b, undefined, { numeric: true })).at(-1);
    info.glibcxx = [...symbols.matchAll(/\bGLIBCXX_([\d.]+)/g)].map((match) => match[1])
      .sort((a, b) => a.localeCompare(b, undefined, { numeric: true })).at(-1);
  } else {
    const offset = data.length >= 64 ? data.readUInt32LE(60) : data.length;
    const machine = 0x8664;
    if (data.toString('ascii', 0, 2) !== 'MZ' || offset + 6 > data.length ||
        data.readUInt32LE(offset) !== 0x4550 || data.readUInt16LE(offset + 4) !== machine)
      throw new Error('Expected a Windows PE executable matching the current CPU.');
    info.dependencies = [...run('dumpbin.exe', ['/DEPENDENTS', binary])
      .matchAll(/^\s+([\w.-]+\.dll)\s*$/gim)].map((match) => match[1]);
    if (!info.dependencies.length) throw new Error('Cannot inspect Windows DLL dependencies.');
    for (const dependency of info.dependencies) {
      if (/^(msvcp|vcruntime|concrt)/i.test(dependency) ||
          (!/^(api-ms-|ext-ms-)/i.test(dependency) &&
           !fs.existsSync(path.join(process.env.SystemRoot, 'System32', dependency))))
        throw new Error(`Unbundled Windows runtime: ${dependency}. Build with static runtimes (MT).`);
    }
    info.minimumOS = 'Windows 10 (VT console)';
  }
  const doctor = JSON.parse(run(binary, ['doctor', '--json'], { timeout: 15000 }));
  if (doctor.bridge_version !== 1) throw new Error('Native binary does not report the expected bridge version.');
  return info;
}
function pack(directory, output) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'package.json'), 'utf8'));
  const expected = tarballName(manifest.name, manifest.version);
  if (fs.existsSync(path.join(output, expected))) throw new Error(`Output already exists: ${expected}`);
  const args = ['pack', '--json', '--ignore-scripts', '--pack-destination', output];
  if (process.platform === 'win32' && !process.env.npm_execpath)
    throw new Error('On Windows, run this script through npm run pack:npm.');
  const result = process.env.npm_execpath
    ? run(process.execPath, [process.env.npm_execpath, ...args], { cwd: directory })
    : run('npm', args, { cwd: directory });
  // npm 12 uses a name-keyed object instead of the earlier array format.
  const parsed = JSON.parse(result);
  const entries = Array.isArray(parsed) ? parsed : Object.values(parsed);
  if (entries.length !== 1) throw new Error('npm pack must produce exactly one package.');
  const [{ filename, size }] = entries;
  if (filename !== expected) throw new Error(`Unexpected npm tarball name: ${filename}; expected ${expected}`);
  return { filename, size };
}

let staging;
const created = [];
try {
  const args = process.argv.slice(2);
  let output = path.join(root, 'build', 'npm');
  let binary;
  for (let i = 0; i < args.length; i += 2) {
    if (!args[i + 1]) throw new Error(`Missing value for ${args[i]}`);
    if (args[i] === '--out-dir') output = path.resolve(args[i + 1]);
    else if (args[i] === '--binary') binary = path.resolve(args[i + 1]);
    else throw new Error(`Unknown option: ${args[i]}`);
  }
  const target = `${process.platform}-${process.arch}`;
  const platform = platforms[target];
  if (!platform) throw new Error(`No packaging target for ${target}`);
  const packageNames = Object.values(platforms).map(({ packageName }) => packageName);
  if (packageNames.some((name) => typeof name !== 'string' || name.length > 214 ||
      !/^@[a-z0-9][a-z0-9._-]*\/[a-z0-9][a-z0-9._-]*$/.test(name)))
    throw new Error('Every npm platform must declare a valid scoped packageName.');
  if (new Set(packageNames).size !== packageNames.length)
    throw new Error('Each npm platform must have a distinct packageName.');
  const version = fs.readFileSync(path.join(cliRoot, 'xmake.lua'), 'utf8').match(/set_version\("([^"]+)"\)/)?.[1];
  if (version !== project.version) throw new Error('apps/cli/package.json and apps/cli/xmake.lua versions must match.');
  for (const name of [project.name, platform.packageName]) {
    const filename = tarballName(name, version);
    if (fs.existsSync(path.join(output, filename))) throw new Error(`Output already exists: ${filename}`);
  }
  const executable = process.platform === 'win32' ? 'crossdesk-cli.exe' : 'crossdesk-cli';
  binary ||= path.join(root, 'build', platform.xmakePlatform, platform.xmakeArch, 'release', executable);
  const info = inspect(binary, target);
  staging = fs.mkdtempSync(path.join(os.tmpdir(), 'crossdesk-npm-'));
  fs.mkdirSync(output, { recursive: true });
  const native = path.join(staging, 'native');
  fs.mkdirSync(path.join(native, 'bin'), { recursive: true });
  fs.copyFileSync(binary, path.join(native, 'bin', executable));
  fs.chmodSync(path.join(native, 'bin', executable), 0o755);
  copy('LICENSE', path.join(native, 'LICENSE'));
  // Include the controller and MiniRTC sources used by this checkout.
  // External Xmake dependency source/licenses still belong to the release's
  // dependency bundle; see source/apps/cli/docs/source-provenance.md.
  // Keep desktop build definitions for Xmake's initial option discovery;
  // desktop application sources and resources are intentionally not bundled.
  for (const entry of ['apps/cli', 'apps/desktop/xmake',
                       'libs/controller/include',
                       'libs/controller/src', 'libs/terminal', 'libs/wire/include',
                       'libs/wire/src', 'deps/submodules/minirtc',
                       'deps/submodules/xmake.lua', 'deps/thirdparty', 'xmake.lua',
                       'LICENSE', '.gitmodules'])
    copy(entry, path.join(native, 'source', entry));
  json(path.join(native, 'build-info.json'), { version, ...info });
  fs.writeFileSync(path.join(native, 'README.md'), `# ${platform.packageName}\n\nNative ${target} dependency for [${project.name}](https://www.npmjs.com/package/${project.name}) ${version}, published under the ${platform.packageName.split('/')[0]} scope.\n\nInstall the cross-platform entry package with \`npm install -g ${project.name}\` or run \`npx ${project.name} doctor --json\`. This platform package contains the native binary and does not register a shell command.\n\nSource repository: [CrossDesk](${project.repository.url.replace(/^git\+/, '').replace(/\.git$/, '')}). Report problems at ${project.bugs.url}.\n\nRuntime requirements and linked libraries: [build-info.json](build-info.json).\n\nController and MiniRTC sources are included in source/. From source/, configure with xmake f -y --CROSSDESK_BUILD_DESKTOP=n --CROSSDESK_BUILD_CLI=y --USE_CUDA=n -m release, then run xmake build crossdesk-cli. External dependency notices and corresponding sources must accompany the product release; see source/apps/cli/docs/source-provenance.md.\n`);
  const metadata = {
    license: project.license, author: project.author, repository: project.repository,
    homepage: project.homepage, bugs: project.bugs,
    publishConfig: { access: 'public', registry: 'https://registry.npmjs.org/' },
  };
  json(path.join(native, 'package.json'), {
    ...metadata, name: platform.packageName, version, description: `${project.description} (${target})`,
    os: [platform.os], cpu: [platform.cpu],
    ...(platform.libc ? { libc: [platform.libc] } : {}),
    files: ['bin', 'build-info.json', 'source', 'LICENSE', 'README.md'],
  });
  const launcher = path.join(staging, 'launcher');
  fs.mkdirSync(launcher);
  for (const entry of ['npm', 'docs', 'README.md']) copy(`apps/cli/${entry}`, path.join(launcher, entry));
  copy('LICENSE', path.join(launcher, 'LICENSE'));
  fs.chmodSync(path.join(launcher, 'npm', 'cli.cjs'), 0o755);
  json(path.join(launcher, 'package.json'), {
    ...metadata, name: project.name, version, description: project.description, engines: project.engines,
    bin: { 'crossdesk-cli': 'npm/cli.cjs' },
    os: ['darwin', 'linux', 'win32'], cpu: ['arm64', 'x64'],
    files: ['npm', 'docs', 'README.md', 'LICENSE'],
    optionalDependencies: Object.fromEntries(packageNames.map((name) => [name, version])),
  });
  // Finish both archives before publishing either one to the output directory.
  const archives = path.join(staging, 'archives');
  fs.mkdirSync(archives);
  const packages = [pack(native, archives), pack(launcher, archives)];
  for (const { filename } of packages) {
    const destination = path.join(output, filename);
    fs.copyFileSync(path.join(archives, filename), destination, fs.constants.COPYFILE_EXCL);
    created.push(destination);
  }
  for (const { filename, size } of packages)
    console.log(`${path.join(output, filename)} (${(size / 1024 / 1024).toFixed(2)} MiB)`);
  console.log(`Native requirements: ${JSON.stringify(info)}`);
} catch (error) {
  for (const file of created) fs.rmSync(file, { force: true });
  console.error(`pack:npm: ${error.message}`);
  process.exitCode = 1;
} finally {
  if (staging) fs.rmSync(staging, { recursive: true, force: true });
}
