#!/usr/bin/env python3
"""Build the native controller in isolation from desktop and iOS caches."""
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import re

ANDROID = Path(__file__).resolve().parents[1]
ROOT = ANDROID.parents[1]
SOURCE = ROOT / 'deps/submodules/minirtc'
CACHE = ANDROID / '.native'
PACKAGE_REVISION = 'eda39de3fb99b420c168f1ab9ab2d1791e11b662'


def main():
    sdk = Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
               or Path.home() / 'Library/Android/sdk')
    ndk = sdk / 'ndk/28.2.13676358'
    if not ndk.is_dir():
        raise SystemExit('Install Android NDK 28.2.13676358 using SDK Manager.')
    bundled = Path.home() / '.cache/crossdesk/toolchains/xmake-3.1.1/xmake'
    xmake = os.environ.get('XMAKE_BIN') or (str(bundled) if bundled.is_file() else shutil.which('xmake'))
    if not xmake:
        raise SystemExit('xmake is required; install it or set XMAKE_BIN.')
    version = subprocess.check_output([xmake, '--version'], text=True)
    if not re.search(r'xmake v3\.1\.1(?:\+|[,\s])', version):
        raise SystemExit('Use Xmake 3.1.1 (set XMAKE_BIN to its executable).')
    if not (SOURCE / 'xmake.lua').is_file():
        raise SystemExit('Initialize the MiniRTC submodule before building.')
    env = os.environ.copy()
    env['XMAKE_PKG_INSTALLDIR'] = str(CACHE / 'packages')
    env['XMAKE_GLOBALDIR'] = str(CACHE / 'global')
    env['ANDROID_NDK_HOME'] = str(ndk)
    env['ANDROID_NDK_ROOT'] = str(ndk)
    env['NO_COLOR'] = '1'
    work = ANDROID / 'native'
    repository = CACHE / 'global/.xmake/repositories/xmake-repo'
    if not (repository / '.git').is_dir():
        subprocess.run([xmake, 'repo', '--update'], cwd=work, env=env, check=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repository, text=True).strip()
    if revision != PACKAGE_REVISION:
        subprocess.run(['git', 'fetch', 'origin', PACKAGE_REVISION], cwd=repository, check=True)
        subprocess.run(['git', 'checkout', '--detach', PACKAGE_REVISION], cwd=repository, check=True)
    subprocess.run([xmake, 'f', '-P', str(work), '-y', '-p', 'android', '-a', 'arm64-v8a',
                    '-m', 'release', '--ndk=' + str(ndk), '--ndk_sdkver=26',
                    '--runtimes=c++_static', '--policies=package.precompiled:n',
                    '-o', str(CACHE / 'build')], cwd=work, env=env, check=True)
    subprocess.run([xmake, '-P', str(work), '-y', 'crossdesk_android'], cwd=work, env=env, check=True)


if __name__ == '__main__':
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE / 'build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        main()
