#!/usr/bin/env python3
"""Build the native controller in isolation from desktop and iOS caches."""
import argparse
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
XMAKE_MIN_VERSION = (3, 1, 1)


def find_xmake():
    xmake = os.environ.get('XMAKE_BIN') or shutil.which('xmake')
    minimum = '.'.join(map(str, XMAKE_MIN_VERSION))
    if not xmake:
        raise SystemExit(f'Xmake {minimum} or newer is required; install it or set XMAKE_BIN.')
    output = subprocess.check_output([xmake, '--version'], text=True)
    version = re.search(r'xmake v(\d+)\.(\d+)\.(\d+)(?:\+|[,\s]|$)', output)
    if not version or tuple(map(int, version.groups())) < XMAKE_MIN_VERSION:
        raise SystemExit(f'Use Xmake {minimum} or newer (set XMAKE_BIN to its executable).')
    return xmake


def main(configure_only=False):
    sdk = Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
               or Path.home() / 'Library/Android/sdk')
    ndk = sdk / 'ndk/28.2.13676358'
    if not ndk.is_dir():
        raise SystemExit('Install Android NDK 28.2.13676358 using SDK Manager.')
    xmake = find_xmake()
    if not (SOURCE / 'xmake.lua').is_file():
        raise SystemExit('Initialize the MiniRTC submodule before building.')
    env = os.environ.copy()
    env['XMAKE_PKG_INSTALLDIR'] = str(CACHE / 'packages')
    env['ANDROID_NDK_HOME'] = str(ndk)
    env['ANDROID_NDK_ROOT'] = str(ndk)
    env['NO_COLOR'] = '1'
    work = ANDROID / 'native'
    # Use the desktop repository policy; Xmake manages recipe initialization
    # and updates. Only installed Android packages/build products are isolated.
    subprocess.run([xmake, 'f', '-P', str(work), '-y', '-p', 'android', '-a', 'arm64-v8a',
                    '-m', 'release', '--ndk=' + str(ndk), '--ndk_sdkver=26',
                    '--runtimes=c++_static',
                    '--policies=package.precompiled:n,package.librarydeps.strict_compatibility',
                    '-o', str(CACHE / 'build')], cwd=work, env=env, check=True)
    if configure_only:
        return
    subprocess.run([xmake, '-P', str(work), '-y', 'crossdesk_android'], cwd=work, env=env, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configure-only', action='store_true',
                        help='Install native dependencies before compiling application targets.')
    args = parser.parse_args()
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE / 'build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        main(configure_only=args.configure_only)
