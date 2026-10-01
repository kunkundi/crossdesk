#!/usr/bin/env python3
"""Check Android's native packets against the shared desktop wire parser."""
import os
from pathlib import Path
import subprocess
import tempfile

ANDROID = Path(__file__).resolve().parents[1]
ROOT = ANDROID.parents[1]
headers = list((ANDROID / '.native/packages/n/nlohmann_json').glob('*/**/include/nlohmann/json.hpp'))
if not headers:
    raise SystemExit('Build the Android app first to populate its pinned native dependencies.')
with tempfile.TemporaryDirectory(prefix='crossdesk-android-protocol-') as temporary:
    binary = Path(temporary) / 'controller-protocol-test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17',
                    '-I' + str(headers[0].parents[1]),
                    '-I' + str(ANDROID / 'native'),
                    '-I' + str(ROOT / 'deps/submodules/minirtc/src/api'),
                    '-I' + str(ROOT / 'libs/wire/include'),
                    str(ANDROID / 'test/native/controller_protocol_test.cpp'),
                    str(ROOT / 'libs/wire/src/remote_action.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    video_test = Path(temporary) / 'video-pipeline-test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread',
                    '-I' + str(ANDROID / 'native'),
                    '-I' + str(ROOT / 'deps/submodules/minirtc/src/api'),
                    str(ANDROID / 'test/native/video_pipeline_test.cpp'),
                    '-o', str(video_test)], check=True)
    subprocess.run([str(video_test)], check=True, timeout=10)
print('PASS: Android video settings / desktop wire round-trip and native network fields')
print('PASS: long video latency, invalid clocks, latest-frame rendering and shutdown')
