'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const { setTimeout: sleep } = require('node:timers/promises');
const project = require('../package.json');
const platforms = require('../npm/platforms.json');
const registry = 'https://registry.npmjs.org/';
const repository = 'kunkundi/crossdesk';

function versionParts(version) {
  const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([\da-zA-Z-]+(?:\.[\da-zA-Z-]+)*))?$/.exec(version);
  assert(match && !(match[4] || '').split('.').some((part) => /^0\d+$/.test(part)),
    `Invalid release version: ${version}`);
  return [match.slice(1, 4).map(BigInt), match[4]?.split('.')];
}

function compareVersions(left, right) {
  const [a, preA] = versionParts(left);
  const [b, preB] = versionParts(right);
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] > b[i] ? 1 : -1;
  if (!preA || !preB) return preA ? -1 : preB ? 1 : 0;
  for (let i = 0; i < Math.max(preA.length, preB.length); i++) {
    if (preA[i] === preB[i]) continue;
    if (preA[i] === undefined || preB[i] === undefined) return preA[i] === undefined ? -1 : 1;
    const numericA = /^\d+$/.test(preA[i]);
    const numericB = /^\d+$/.test(preB[i]);
    if (numericA !== numericB) return numericA ? -1 : 1;
    return (numericA ? BigInt(preA[i]) > BigInt(preB[i]) : preA[i] > preB[i]) ? 1 : -1;
  }
  return 0;
}

function releasePlan() {
  assert(['true', 'false'].includes(process.env.RELEASE_DRY_RUN), 'Set RELEASE_DRY_RUN=true or false explicitly.');
  const dryRun = process.env.RELEASE_DRY_RUN === 'true';
  const version = project.version;
  versionParts(version);
  assert.equal(project.name, '@chunkdash/crossdesk-cli', 'Unexpected entry package scope.');
  const nativeVersion = fs.readFileSync(path.join(__dirname, '../xmake.lua'), 'utf8')
    .match(/set_version\("([^"]+)"\)/)?.[1];
  assert.equal(version, nativeVersion, 'package.json and xmake.lua versions must match.');
  const ref = process.env.GITHUB_REF || '';
  if (!dryRun) {
    assert.equal(process.env.GITHUB_ACTIONS, 'true', 'Real publishing must run in GitHub Actions.');
    assert.equal(process.env.GITHUB_REPOSITORY, repository, 'Unexpected publishing repository.');
    assert(['push', 'workflow_dispatch'].includes(process.env.GITHUB_EVENT_NAME), 'Unsupported publishing event.');
    assert.equal(ref, `refs/tags/cli-v${version}`, 'Publish only from the matching cli-vVERSION tag.');
  } else if (ref.startsWith('refs/tags/')) {
    assert.equal(ref, `refs/tags/cli-v${version}`, 'Git tag and package version must match, including in a dry run.');
  }
  return { version, tag: version.includes('-') ? 'next' : 'latest', dryRun };
}

function run(command, args, options = {}) {
  return execFileSync(command, args, { encoding: 'utf8', maxBuffer: 128 * 1024 * 1024, ...options });
}

function npm(args, options) {
  return process.env.npm_execpath
    ? run(process.execPath, [process.env.npm_execpath, ...args], options)
    : run('npm', args, options);
}

function archiveRead(file, member, encoding = 'utf8') {
  return run('tar', ['-xOf', file, `package/${member}`], { encoding });
}

function filename(name, version) {
  return `${name.replace(/^@/, '').replace('/', '-')}-${version}.tgz`;
}

function inspect(file, name, version) {
  const manifest = JSON.parse(archiveRead(file, 'package.json'));
  assert.equal(manifest.name, name, `Unexpected package in ${file}`);
  assert.equal(manifest.version, version, `Unexpected version in ${file}`);
  assert(!manifest.private && !manifest.scripts, `${name}: publish only generated packages without lifecycle scripts.`);
  assert.equal(manifest.repository?.url, project.repository.url, `${name}: repository mismatch.`);
  assert.deepEqual(manifest.publishConfig, { access: 'public', registry }, `${name}: publishConfig mismatch.`);
  const result = JSON.parse(npm(['pack', file, '--dry-run', '--json', '--ignore-scripts']));
  // npm 12 returns a name-keyed object; npm 11 and earlier return an array.
  const entries = Array.isArray(result) ? result : Object.values(result);
  assert.equal(entries.length, 1, `Expected one package in ${file}`);
  const [packed] = entries;
  const integrity = `sha512-${crypto.createHash('sha512').update(fs.readFileSync(file)).digest('base64')}`;
  assert.equal(packed.integrity, integrity, `${name}: tarball integrity mismatch.`);
  return { file, name, version, integrity, manifest, files: packed.files };
}

function collectPackages(directory, version) {
  const targets = Object.keys(platforms);
  assert.deepEqual(fs.readdirSync(directory).sort(), targets.map((target) => `npm-${target}`).sort(),
    'Expected exactly five npm platform artifact directories from the same workflow run.');
  const natives = [];
  const launchers = [];
  const dependencies = Object.fromEntries(Object.values(platforms).map(({ packageName }) => [packageName, version]));
  for (const [target, platform] of Object.entries(platforms)) {
    const folder = path.join(directory, `npm-${target}`);
    const names = [filename(platform.packageName, version), filename(project.name, version)];
    assert.deepEqual(fs.readdirSync(folder).sort(), [...names].sort(), `${target}: expected native and entry tarballs.`);
    const native = inspect(path.join(folder, names[0]), platform.packageName, version);
    assert.deepEqual(native.manifest.os, [platform.os]);
    assert.deepEqual(native.manifest.cpu, [platform.cpu]);
    assert.deepEqual(native.manifest.libc, platform.libc ? [platform.libc] : undefined);
    assert(!native.manifest.bin, `${target}: native packages must not register commands.`);
    const binary = `bin/crossdesk-cli${platform.os === 'win32' ? '.exe' : ''}`;
    assert(native.files.some((file) => file.path === binary && (platform.os === 'win32' || (file.mode & 0o111))),
      `${target}: missing executable binary.`);
    const info = JSON.parse(archiveRead(native.file, 'build-info.json'));
    assert.equal(info.version, version);
    assert.equal(info.target, target);
    assert.equal(info.sha256, crypto.createHash('sha256').update(archiveRead(native.file, binary, null)).digest('hex'),
      `${target}: native binary checksum mismatch.`);
    natives.push(native);

    const launcher = inspect(path.join(folder, names[1]), project.name, version);
    assert.deepEqual(launcher.manifest.optionalDependencies, dependencies, `${target}: platform dependencies mismatch.`);
    assert.deepEqual(launcher.manifest.bin, { 'crossdesk-cli': 'npm/cli.cjs' });
    assert.deepEqual(JSON.parse(archiveRead(launcher.file, 'npm/platforms.json')), platforms);
    launchers.push({ target, ...launcher });
  }
  // Pick a Unix entry so the shebang and executable mode work on Unix as well.
  const entry = launchers.find(({ target }) => target === 'linux-x64');
  assert(entry.files.some((file) => file.path === 'npm/cli.cjs' && (file.mode & 0o111)), 'Entry launcher is not executable.');
  const contents = Object.fromEntries(entry.files.map(({ path: member }) =>
    [member, archiveRead(entry.file, member).replace(/\r\n/g, '\n')]));
  for (const launcher of launchers) {
    assert.deepEqual(launcher.files.map((file) => file.path).sort(), Object.keys(contents).sort(),
      `${launcher.target}: entry package contents differ.`);
    for (const [member, content] of Object.entries(contents)) {
      assert.equal(archiveRead(launcher.file, member).replace(/\r\n/g, '\n'), content,
        `${launcher.target}: entry file differs: ${member}`);
    }
  }
  return [...natives, entry];
}

async function readRegistry(name) {
  const response = await fetch(`${registry}${encodeURIComponent(name)}`, {
    headers: { accept: 'application/json', 'cache-control': 'no-cache' },
    signal: AbortSignal.timeout(30000),
  });
  if (response.status === 404) return null;
  assert(response.ok, `${name}: registry returned HTTP ${response.status}; refusing to treat it as a missing package.`);
  return response.json();
}

function checkPublished(pkg, document) {
  const published = document?.versions?.[pkg.version];
  if (!published) return false;
  assert.equal(published.dist?.integrity, pkg.integrity,
    `${pkg.name}@${pkg.version} already exists with different content. Bump the version; do not overwrite it.`);
  return true;
}

async function waitForPackages(packages, tag) {
  const deadline = Date.now() + 15 * 60 * 1000;
  let pending = [...packages];
  while (pending.length) {
    const remaining = [];
    for (const pkg of pending) {
      const document = await readRegistry(pkg.name);
      if (!checkPublished(pkg, document) || document['dist-tags']?.[tag] !== pkg.version) remaining.push(pkg);
    }
    if (!remaining.length) return;
    assert(Date.now() < deadline, `npm is still processing: ${remaining.map((pkg) => pkg.name).join(', ')}. Wait, then re-run the failed job with the same artifacts.`);
    console.log(`Waiting for npm versions and ${tag} tags: ${remaining.map((pkg) => pkg.name).join(', ')}`);
    pending = remaining;
    await sleep(15000);
  }
}

async function publishPackages(packages, { tag, dryRun }) {
  const existing = new Set();
  // Preflight every package before making any registry changes.
  for (const pkg of packages) {
    if (dryRun) continue;
    const document = await readRegistry(pkg.name);
    const current = document?.['dist-tags']?.[tag];
    assert(!current || compareVersions(current, pkg.version) <= 0,
      `${pkg.name}: ${tag} already points to newer ${current}; refusing to roll it back.`);
    if (checkPublished(pkg, document)) {
      assert.equal(current, pkg.version, `${pkg.name}: existing version matches but ${tag} does not. Review dist-tags manually before retrying.`);
      existing.add(pkg.name);
    } else {
      const versions = Object.keys(document?.versions || {});
      assert(!(versions.length === 1 && versions[0] === '0.0.0-stage'),
        `${pkg.name}: npm still has a staging placeholder. Wait for processing before retrying; do not upload it again.`);
    }
  }
  const publish = (pkg) => {
    if (dryRun) {
      // collectPackages already ran npm pack --dry-run and inspected the archive.
      // npm publish --dry-run still exchanges OIDC credentials and rejects
      // existing versions, so it cannot be used before trust is configured.
      console.log(`Validated tarball without publishing: ${pkg.name}@${pkg.version} (${tag})`);
      return;
    }
    if (existing.has(pkg.name)) {
      console.log(`Already published with matching integrity: ${pkg.name}@${pkg.version}`);
      return;
    }
    console.log(`Publishing: ${pkg.name}@${pkg.version} (${tag})`);
    npm(['publish', pkg.file, '--access', 'public', '--tag', tag, '--ignore-scripts',
      `--registry=${registry}`, '--fetch-retries=0', '--fetch-timeout=120000',
      '--provenance'], { stdio: 'inherit' });
  };
  const native = packages.slice(0, -1);
  for (const pkg of native) publish(pkg);
  if (!dryRun) await waitForPackages(native, tag);
  const entry = packages.at(-1);
  publish(entry);
  if (!dryRun) await waitForPackages([entry], tag);
  const report = `${dryRun ? 'Validated (no packages published)' : 'Published and verified'} CLI ${entry.version} with npm tag ${tag}.\n\n` +
    '| Package | Integrity (SHA-512) |\n| --- | --- |\n' + packages.map((pkg) => `| ${pkg.name} | ${pkg.integrity} |`).join('\n') + '\n';
  console.log(report);
  if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, report);
}

async function main() {
  const [command, directory, ...extra] = process.argv.slice(2);
  assert((command === 'plan' && !directory) || (command === 'publish' && directory && !extra.length),
    'Usage: release-npm.cjs plan | publish ARTIFACT_DIRECTORY (set RELEASE_DRY_RUN=true or false)');
  const plan = releasePlan();
  console.log(`CLI ${plan.version}, npm tag ${plan.tag}, dry run: ${plan.dryRun}`);
  if (command === 'plan') {
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT,
      `version=${plan.version}\ntag=${plan.tag}\ndry_run=${plan.dryRun}\n`);
    return;
  }
  await publishPackages(collectPackages(path.resolve(directory), plan.version), plan);
}

if (require.main === module) main().catch((error) => {
  console.error(`release:npm: ${error.message}`);
  process.exitCode = 1;
});

module.exports = { releasePlan, compareVersions, collectPackages, publishPackages };
