#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const platforms = require('./platforms.json');
const { version } = require('../package.json');

function main() {
  const target = `${process.platform}-${process.arch}`;
  if (!platforms[target]) {
    throw new Error(`No crossdesk-cli build for ${target}. Supported targets: ${Object.keys(platforms).join(', ')}.`);
  }
  if (process.platform === 'linux' && !process.report.getReport().header.glibcVersionRuntime) {
    throw new Error('This release requires glibc Linux. Alpine/musl needs a separate native build.');
  }
  const packageName = `crossdesk-cli-${target}`;
  let manifestPath;
  try {
    manifestPath = require.resolve(`${packageName}/package.json`);
  } catch {
    throw new Error(`Missing ${packageName}@${version}. Install with optional dependencies enabled; this platform's package must be published before crossdesk-cli.`);
  }
  if (require(manifestPath).version !== version) {
    throw new Error(`Expected ${packageName}@${version}; reinstall matching package versions.`);
  }
  const binary = path.join(path.dirname(manifestPath), 'bin', process.platform === 'win32' ? 'crossdesk-cli.exe' : 'crossdesk-cli');
  fs.accessSync(binary, process.platform === 'win32' ? fs.constants.F_OK : fs.constants.X_OK);
  const args = process.argv.slice(2);
  // Hosts such as Pi should spawn the native path directly, retaining descriptor
  // inheritance and control of the process lifetime without an npm intermediary.
  if (args.length === 1 && args[0] === '--print-binary-path') {
    process.stdout.write(`${binary}\n`);
    return;
  }
  const stdio = ['inherit', 'inherit', 'inherit'];
  if (args[0] === 'bridge') {
    const index = args.lastIndexOf('--password-fd');
    const fd = index === -1 ? 3 : Number(args[index + 1]);
    if (Number.isInteger(fd) && fd >= 3 && fd <= 1024) {
      try {
        const stat = fs.fstatSync(fd);
        if (stat.isFIFO() || stat.isSocket() || stat.isFile()) {
          while (stdio.length <= fd) stdio.push('ignore');
          stdio[fd] = fd;
        }
      } catch { /* The native bridge reports a missing credential descriptor. */ }
    }
  }
  const child = spawn(binary, args, { stdio, shell: false });
  const handlers = new Map();
  if (process.platform !== 'win32') {
    for (const signal of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
      const handler = () => { if (child.exitCode === null) child.kill(signal); };
      handlers.set(signal, handler);
      process.on(signal, handler);
    }
  } else {
    // Console Ctrl+C is delivered to both processes. Do not terminate the native
    // child before it has restored console modes.
    const handler = () => {};
    handlers.set('SIGINT', handler);
    process.on('SIGINT', handler);
  }
  const cleanup = () => {
    for (const [signal, handler] of handlers) process.removeListener(signal, handler);
  };
  child.once('error', (error) => {
    cleanup();
    process.stderr.write(`crossdesk-cli: cannot start native program (${error.code || 'unknown error'}).\n`);
    process.exitCode = 1;
  });
  child.once('exit', (code, signal) => {
    cleanup();
    if (signal && process.platform !== 'win32') process.kill(process.pid, signal);
    else process.exitCode = code ?? 1;
  });
}

try {
  main();
} catch (error) {
  process.stderr.write(`crossdesk-cli: ${error.message}\n`);
  process.exitCode = 1;
}
