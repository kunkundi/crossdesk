'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const { setTimeout: sleep } = require('node:timers/promises');
const project = require('../package.json');
const lock = require('../package-lock.json');
const cli = require('../../cli/package.json');
const harness = require('../integrations/deepseek-harness/package.json');
const platforms = require('../../cli/npm/platforms.json');
const { compareVersions } = require('../../cli/scripts/release-npm.cjs');
const registry = 'https://registry.npmjs.org/';
const repository = 'kunkundi/crossdesk';

function releasePlan() {
  assert(['true', 'false'].includes(process.env.RELEASE_DRY_RUN), 'Set RELEASE_DRY_RUN=true or false explicitly.');
  const dryRun = process.env.RELEASE_DRY_RUN === 'true';
  const version = project.version;
  compareVersions(version, version);
  assert.equal(project.name, '@chunkdash/crossdesk-mcp', 'Unexpected MCP package name.');
  for (const manifest of [lock, lock.packages?.['']]) {
    assert.equal(manifest?.name, project.name, 'package-lock.json name must match package.json.');
    assert.equal(manifest?.version, version, 'package-lock.json version must match package.json.');
  }
  const nativeVersion = fs.readFileSync(path.join(__dirname, '../src/native.ts'), 'utf8')
    .match(/export const NATIVE_VERSION = "([^"]+)";/)?.[1];
  assert(nativeVersion, 'MCP source must declare an exact NATIVE_VERSION.');
  compareVersions(nativeVersion, nativeVersion);
  assert.equal(cli.version, version, 'CLI and MCP must share the same version.');
  assert.equal(harness.version, version, 'DSH and MCP must share the same version.');
  assert.equal(nativeVersion, version, 'MCP must depend on the same native CLI version.');
  const ref = process.env.GITHUB_REF || '';
  if (!dryRun) {
    assert.equal(process.env.GITHUB_ACTIONS, 'true', 'Real publishing must run in GitHub Actions.');
    assert.equal(process.env.GITHUB_REPOSITORY, repository, 'Unexpected publishing repository.');
    assert(['push', 'workflow_dispatch'].includes(process.env.GITHUB_EVENT_NAME), 'Unsupported publishing event.');
    assert.equal(ref, `refs/tags/mcp-v${version}`, 'Publish only from the matching mcp-vVERSION tag.');
  } else if (ref.startsWith('refs/tags/')) {
    assert.equal(ref, `refs/tags/mcp-v${version}`, 'Git tag and package version must match, including in a dry run.');
  }
  return { version, nativeVersion, tag: version.includes('-') ? 'next' : 'latest', dryRun };
}

function run(command, args, options = {}) {
  return execFileSync(command, args, { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024, ...options });
}

function npm(args, options) {
  return process.env.npm_execpath
    ? run(process.execPath, [process.env.npm_execpath, ...args], options)
    : run('npm', args, options);
}

function archiveRead(file, member) {
  return run('tar', ['-xOf', file, `package/${member}`]);
}

function collectPackage(directory, { version, nativeVersion }) {
  const filename = `chunkdash-crossdesk-mcp-${version}.tgz`;
  assert.deepEqual(fs.readdirSync(directory), [filename], 'Expected exactly one universal MCP tarball from this workflow run.');
  const file = path.join(directory, filename);
  const manifest = JSON.parse(archiveRead(file, 'package.json'));
  assert.equal(manifest.name, project.name, 'Unexpected MCP package in artifact.');
  assert.equal(manifest.version, version, 'Artifact version does not match the release tag.');
  assert(manifest.private === false && !manifest.scripts && !manifest.devDependencies,
    'Publish only generated packages without lifecycle scripts or development dependencies.');
  assert(!manifest.os && !manifest.cpu && !manifest.libc, 'Publish the universal MCP package, not a platform bundle.');
  assert.deepEqual(manifest.publishConfig, { access: 'public', registry }, 'Unexpected publishConfig.');
  for (const field of ['repository', 'dependencies', 'engines', 'peerDependencies', 'peerDependenciesMeta', 'main', 'exports', 'dsh'])
    assert.deepEqual(manifest[field], project[field], `Artifact ${field} differs from source metadata.`);
  assert.deepEqual(manifest.bin, { 'crossdesk-mcp': 'dist/main.js' });
  assert.deepEqual(manifest.optionalDependencies,
    Object.fromEntries(Object.values(platforms).map(({ packageName }) => [packageName, nativeVersion])),
    'MCP must pin all native platform dependencies to NATIVE_VERSION.');
  const result = JSON.parse(npm(['pack', file, '--dry-run', '--json', '--ignore-scripts']));
  const entries = Array.isArray(result) ? result : Object.values(result);
  assert.equal(entries.length, 1, 'Expected one package in the tarball.');
  const [packed] = entries;
  const integrity = `sha512-${crypto.createHash('sha512').update(fs.readFileSync(file)).digest('base64')}`;
  assert.equal(packed.integrity, integrity, 'Tarball integrity mismatch.');
  const files = new Map(packed.files.map((entry) => [entry.path, entry]));
  for (const member of ['dist/main.js', 'dist/native.js', 'dist/harness.js', 'dist/harness-client.js',
    'dist/viewer-browser.html', 'dist/viewer-apps.html', 'cordis.patch.yml', 'README.md', 'DEVELOPMENT.md', 'LICENSE'])
    assert(files.get(member)?.size > 0, `Missing or empty release file: ${member}`);
  assert(!packed.files.some((entry) => entry.path.startsWith('native/')), 'Universal package must not embed one platform binary.');
  assert(files.get('dist/main.js').mode & 0o111, 'MCP command is not executable.');
  assert(archiveRead(file, 'dist/main.js').startsWith('#!/usr/bin/env node\n'), 'MCP command is missing its Node shebang.');
  assert.equal(archiveRead(file, 'dist/native.js').match(/export const NATIVE_VERSION = "([^"]+)";/)?.[1],
    nativeVersion, 'Built MCP native version differs from source. Rebuild before packaging.');
  assert.equal(archiveRead(file, 'cordis.patch.yml').trim(),
    fs.readFileSync(path.join(__dirname, '../cordis.patch.yml'), 'utf8').trim(), 'DSH bundle patch differs from source.');
  return { file, name: manifest.name, version, integrity, manifest };
}

async function readRegistry(name, version) {
  const response = await fetch(`${registry}${encodeURIComponent(name)}${version ? `/${version}` : ''}`, {
    headers: { accept: 'application/json', 'cache-control': 'no-cache' },
    signal: AbortSignal.timeout(30000),
  });
  if (response.status === 404) return null;
  assert(response.ok, `${name}: registry returned HTTP ${response.status}; refusing to treat it as a missing package.`);
  return response.json();
}

async function checkNativePackages(version) {
  const results = await Promise.allSettled(Object.values(platforms).map(async (platform) => {
    const document = await readRegistry(platform.packageName);
    const published = document?.versions?.[version];
    assert(published, `${platform.packageName}@${version} is not available. Publish CLI ${version} first, then retry the MCP publish job.`);
    assert.equal(published.name, platform.packageName);
    assert.equal(published.version, version);
    assert.deepEqual(published.os, [platform.os], `${platform.packageName}: OS mismatch.`);
    assert.deepEqual(published.cpu, [platform.cpu], `${platform.packageName}: CPU mismatch.`);
    assert.deepEqual(published.libc, platform.libc ? [platform.libc] : undefined, `${platform.packageName}: libc mismatch.`);
    assert(published.dist?.tarball && /^sha512-/.test(published.dist?.integrity), `${platform.packageName}: missing distribution metadata.`);
    return platform.packageName;
  }));
  const failures = results.filter((result) => result.status === 'rejected');
  assert(!failures.length, `Native dependency checks failed:\n${failures.map((result) => result.reason.message).join('\n')}`);
  console.log(`Verified all ${results.length} native packages at ${version}.`);
}

function checkIntegrity(pkg, published) {
  assert.equal(published.dist?.integrity, pkg.integrity,
    `${pkg.name}@${pkg.version} already exists with different content. Bump the version; do not overwrite it.`);
}

async function waitForPackage(pkg, tag) {
  const deadline = Date.now() + 15 * 60 * 1000;
  while (true) {
    const document = await readRegistry(pkg.name);
    const published = document?.versions?.[pkg.version];
    if (published) {
      checkIntegrity(pkg, published);
      if (document['dist-tags']?.[tag] === pkg.version) return;
    }
    assert(Date.now() < deadline, 'npm is still processing. Wait, then re-run the failed job with the same artifact; do not upload again.');
    console.log(`Waiting for npm to expose ${pkg.name}@${pkg.version} and its ${tag} tag...`);
    await sleep(15000);
  }
}

async function publishPackage(pkg, { tag, nativeVersion, dryRun }) {
  if (dryRun) {
    // npm publish --dry-run can exchange OIDC credentials and reject existing
    // versions. The archive has already passed npm pack --dry-run and inspection.
    console.log('Local validation only: npm permissions and native dependency availability were not checked.');
  } else {
    await checkNativePackages(nativeVersion);
    const document = await readRegistry(pkg.name);
    const current = document?.['dist-tags']?.[tag];
    assert(!current || compareVersions(current, pkg.version) <= 0,
      `${tag} already points to newer ${current}; refusing to roll it back.`);
    // The version endpoint can become visible before the package document.
    const published = document?.versions?.[pkg.version] ?? await readRegistry(pkg.name, pkg.version);
    if (published) {
      checkIntegrity(pkg, published);
      assert.equal(current, pkg.version, `The version already exists but ${tag} does not match. Wait for npm processing or review dist-tags before retrying.`);
      console.log(`Already published with matching integrity: ${pkg.name}@${pkg.version}`);
    } else {
      const versions = Object.keys(document?.versions || {});
      assert(!(versions.length === 1 && versions[0] === '0.0.0-stage'),
        'npm still has a staging placeholder. Wait for processing before retrying; do not upload again.');
      console.log(`Publishing ${pkg.name}@${pkg.version} (${tag})`);
      npm(['publish', pkg.file, '--access', 'public', '--tag', tag, '--ignore-scripts',
        `--registry=${registry}`, '--fetch-retries=0', '--fetch-timeout=120000', '--provenance'], { stdio: 'inherit' });
      await waitForPackage(pkg, tag);
    }
  }
  const report = `${dryRun ? 'Validated (no package published)' : 'Published and verified'} MCP ${pkg.version} with npm tag ${tag}.\n\n` +
    `Native CLI dependency: ${nativeVersion}${dryRun ? ' (registry availability not checked)' : ' (all five platforms verified)'}.\n\n` +
    `Tarball integrity: \`${pkg.integrity}\`\n`;
  console.log(report);
  if (process.env.GITHUB_STEP_SUMMARY) fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, report);
}

async function main() {
  const [command, directory, ...extra] = process.argv.slice(2);
  assert((command === 'plan' && !directory) || (command === 'publish' && directory && !extra.length),
    'Usage: release-npm.cjs plan | publish ARTIFACT_DIRECTORY (set RELEASE_DRY_RUN=true or false)');
  const plan = releasePlan();
  console.log(`MCP ${plan.version}, native CLI ${plan.nativeVersion}, npm tag ${plan.tag}, dry run: ${plan.dryRun}`);
  if (command === 'plan') {
    if (process.env.GITHUB_OUTPUT) fs.appendFileSync(process.env.GITHUB_OUTPUT,
      `version=${plan.version}\nnative_version=${plan.nativeVersion}\ntag=${plan.tag}\ndry_run=${plan.dryRun}\n`);
    return;
  }
  await publishPackage(collectPackage(path.resolve(directory), plan), plan);
}

if (require.main === module) main().catch((error) => {
  console.error(`release:mcp: ${error.message}`);
  process.exitCode = 1;
});

module.exports = { releasePlan, collectPackage, checkNativePackages, publishPackage };
