const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require(process.argv[2]);
const path = require('node:path');
const source = process.argv[3];
const sent = [], queries = [], timers = new Map();
let supported = true, capabilityError = false, nextTimer = 0;
const sdk = {
  isAutoStartupSupported() { if (capabilityError) throw Error('service unavailable'); return supported; },
  getAutoStartupStatusForSelf() { return new Promise((resolve, reject) => queries.push({ resolve, reject })); }
};
const flush = () => new Promise(resolve => setImmediate(resolve));
function load(file, imports, extra = {}) {
  const module = {};
  vm.runInNewContext(ts.transpileModule(fs.readFileSync(file, 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 }
  }).outputText, {
    exports: module, require: name => imports[name] ?? {},
    console: { warn() {}, error: e => { throw Error(e); } },
    setTimeout: fn => { timers.set(++nextTimer, fn); return nextTimer; },
    clearTimeout: id => timers.delete(id), ...extra
  });
  return module;
}
const { AutostartHost: host } = load(source, { '@kit.AbilityKit': { autoStartupManager: sdk } });
const notify = e => sent.push(JSON.parse(JSON.stringify(e)));
const latest = () => sent.at(-1);
const check = value => assert.deepEqual(latest(), { op: 'autostartState', ...value });

(async () => {
  host.refresh(); assert.equal(sent.length, 0, 'no queries before startup');
  supported = false; host.start(notify);
  check({ ok: true, supported: false }); assert.equal(queries.length, 0);
  assert.equal(timers.size, 0);
  supported = true; host.refresh(); check({ pending: true });
  queries.at(-1).resolve(true); await flush();
  check({ ok: true, supported: true, active: true });
  host.refresh(); queries.at(-1).resolve(false); await flush();
  check({ ok: true, supported: true, active: false });

  host.refresh(); queries.at(-1).reject(Error('permission/service failure')); await flush();
  check({ ok: false }); // Failure must never be displayed as "Disabled".
  capabilityError = true; host.refresh(); check({ ok: false });
  capabilityError = false;

  host.refresh(); const old = queries.at(-1);
  host.refresh(); const fresh = queries.at(-1);
  assert.equal(timers.size, 1, 'superseding a query clears its timeout');
  fresh.resolve(true); await flush(); const count = sent.length;
  old.resolve(false); await flush(); assert.equal(sent.length, count);
  check({ ok: true, supported: true, active: true });

  host.refresh(); const timedOut = queries.at(-1);
  [...timers.values()][0](); check({ ok: false });
  const afterTimeout = sent.length;
  timedOut.resolve(true); await flush(); assert.equal(sent.length, afterTimeout);
  host.refresh(); queries.at(-1).resolve(false); await flush();
  check({ ok: true, supported: true, active: false });

  host.refresh(); const beforeStop = queries.at(-1);
  host.stop(); const stopped = sent.length;
  beforeStop.resolve(true); await flush(); host.refresh();
  assert.equal(sent.length, stopped); assert.equal(timers.size, 0);
  host.start(notify); const previousRun = queries.at(-1);
  host.start(notify); queries.at(-1).resolve(false); await flush();
  const restarted = sent.length;
  previousRun.reject(Error('late failure')); await flush(); assert.equal(sent.length, restarted);
  host.stop();

  // Exercise the real desktop startup, window activation, EntryAbility and
  // native-command routing, not only the standalone query helper.
  const handlers = new Map(), commands = [], order = [];
  const target = {
    on: (name, fn) => handlers.set(name, fn),
    getWindowDecorVisible: () => false,
    getWindowProperties: () => ({ id: 1, windowRect: { left: 0, top: 0 } })
  };
  const { DesktopHost: desktop } = load(path.join(path.dirname(source), 'DesktopHost.ets'), {
    'libslint_desktop.so': { default: {
      start: () => order.push('native'), send: text => notify(JSON.parse(text)),
      poll: () => JSON.stringify(commands.splice(0))
    } },
    './AutostartHost': { AutostartHost: host },
    './FileHost': { FileHost: { prepare: async () => {} } },
    './DesktopAccess': { DesktopAccess: { start() {}, refresh() {}, stop() {} } },
    './DisplayHost': { DisplayHost: {
      snapshot: async () => [], observe() {}, global: () => ({ x: 0, y: 0 }), screen: () => ({ densityPixels: 1 })
    } },
    '@kit.LocalizationKit': { i18n: { System: { getSystemLanguage: () => 'en-US' } } },
    '@kit.ArkUI': { display: { getDefaultDisplaySync: () => ({ width: 1280, height: 800, densityPixels: 1, id: 0 }) },
      window: { WindowEventType: { WINDOW_ACTIVE: 1, WINDOW_INACTIVE: 2 } } },
    '@kit.BasicServicesKit': { pasteboard: { getSystemPasteboard: () => ({ on() {}, off() {} }) } },
    '@kit.DeviceCertificateKit': { certificateManager: { getCertificateStorePath: () => '/test/ca', CertType: {} } }
  }, { LocalStorage: class { setOrCreate() {} }, setInterval: () => 1, clearInterval() {} });
  desktop.foreground(); assert.equal(timers.size, 0);
  desktop.start({ getMainWindowSync: () => target, loadContent: async () => {} },
    { filesDir: '/test/files', cacheDir: '/test/cache', terminateSelf: () => order.push('exit') });
  await flush(); assert.deepEqual(order, ['native']); check({ pending: true });
  queries.at(-1).resolve(false); await flush();
  check({ ok: true, supported: true, active: false });
  const { default: Ability } = load(path.join(path.dirname(source), 'EntryAbility.ets'), {
    '@kit.AbilityKit': { UIAbility: class {} }, '../pages/DesktopHost': { DesktopHost: desktop }
  });
  new Ability().onForeground(); check({ pending: true });
  queries.at(-1).resolve(true); await flush(); check({ ok: true, supported: true, active: true });
  handlers.get('windowEvent')(1); // Returning from system Settings refreshes.
  queries.at(-1).resolve(false); await flush(); check({ ok: true, supported: true, active: false });
  commands.push({ op: 'autostartRefresh' }); desktop.poll(); check({ pending: true });
  const last = queries.at(-1);
  commands.push({ op: 'exit' }); desktop.poll(); await flush();
  const exited = sent.length;
  last.resolve(true); await flush(); new Ability().onForeground();
  assert.equal(sent.length, exited); assert.equal(timers.size, 0);
  assert.deepEqual(order, ['native', 'exit']);
  console.log('Autostart system state, failures, timeout, stale replies, startup/foreground and exit tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
