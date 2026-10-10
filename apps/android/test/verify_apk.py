#!/usr/bin/env python3
"""Check the shipped native ABI, ELF load alignment and APK zip alignment."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import zipfile


def verify_package_version(badging, version_name, version_code):
    package = next((line for line in badging.splitlines() if line.startswith('package: ')), '')
    attributes = dict(re.findall(r"(\w+)='([^']*)'", package))
    expected = {'name': 'cn.crossdesk.mobile', 'versionName': version_name, 'versionCode': str(version_code)}
    for key, value in expected.items():
        if attributes.get(key) != value:
            raise ValueError(f'Unexpected APK {key}: expected {value!r}, got {attributes.get(key)!r}')


def verify(path):
    with zipfile.ZipFile(path) as archive, open(path, 'rb') as raw:
        libraries = [i for i in archive.infolist() if i.filename.endswith('.so')]
        if [i.filename for i in libraries] != ['lib/arm64-v8a/libcrossdesk_android.so']:
            raise ValueError('APK must contain exactly the arm64 native controller library')
        library = libraries[0]
        data = archive.read(library)
        if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
            raise ValueError('Native library is not little-endian ELF64 AArch64')
        offset = struct.unpack_from('<Q', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 54)
        segments = [struct.unpack_from('<IIQQQQQQ', data, offset + i * size) for i in range(count)]
        loads = [s for s in segments if s[0] == 1]
        if not loads or any(s[7] < 16384 or (s[2] - s[3]) % 16384 for s in loads):
            raise ValueError('ELF PT_LOAD segments must be aligned to at least 16 KB')
        if library.compress_type != zipfile.ZIP_STORED:
            raise ValueError('Native library must be stored uncompressed')
        raw.seek(library.header_offset + 26)
        name, extra = struct.unpack('<HH', raw.read(4))
        if (library.header_offset + 30 + name + extra) % 16384:
            raise ValueError('APK native library entry is not 16 KB aligned')
        if 'assets/THIRD_PARTY_NOTICES.txt' not in archive.namelist():
            raise ValueError('Missing third-party notices')
        if archive.read('assets/PRIVACY.md') != (Path(__file__).resolve().parents[3] / 'PRIVACY.md').read_bytes():
            raise ValueError('Missing or outdated bundled privacy policy')
        catalog = json.loads(archive.read('assets/ThirdPartyLicenses.json'))
        source = json.loads(archive.read('assets/SourceMetadata.json'))
        components = catalog['components']
        ids = [item['id'] for item in components]
        if catalog['schemaVersion'] != 1 or len(ids) != len(set(ids)) or not {
                'crossdesk', 'minirtc', 'openfec', 'openh264', 'libiconv', 'android-ndk', 'svt-av1'}.issubset(ids):
            raise ValueError('Incomplete About catalog')
        for component in components:
            if not component['documents']:
                raise ValueError('Missing component documents')
            for document in component['documents']:
                text = archive.read('assets/'+document['asset'])
                if not text or hashlib.sha256(text).hexdigest() != document['sha256']:
                    raise ValueError('Incomplete license document: '+document['asset'])
            for document in component.get('buildDocuments', []):
                if not archive.read('assets/'+document['asset']):
                    raise ValueError('Missing build document')
        if source['schemaVersion'] != 1 or len(source['revision']) != 40:
            raise ValueError('Missing source revision')
        for document in source['buildDocuments']:
            if not archive.read('assets/'+document['asset']):
                raise ValueError('Missing source build instructions')
    print(f'PASS: {Path(path).name}: arm64, 16 KB ELF/ZIP alignment, privacy policy, notices, About catalog and documents')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--aapt2', type=Path)
    parser.add_argument('--version-name')
    parser.add_argument('--version-code', type=int)
    args = parser.parse_args()
    if any(value is not None for value in (args.aapt2, args.version_name, args.version_code)):
        if any(value is None for value in (args.aapt2, args.version_name, args.version_code)):
            parser.error('--aapt2, --version-name and --version-code must be supplied together')
        badging = subprocess.check_output([str(args.aapt2), 'dump', 'badging', str(args.apk)], text=True)
        verify_package_version(badging, args.version_name, args.version_code)
        print(f'PASS: {args.apk.name}: versionName={args.version_name}, versionCode={args.version_code}')
    verify(args.apk)
