#!/usr/bin/env python3
"""Package existing reviewed notices for the Android runtime dependency set."""
import json
import hashlib
import os
from pathlib import Path
import subprocess
from urllib.parse import quote

ANDROID = Path(__file__).resolve().parents[1]
ROOT = ANDROID.parents[1]
COMPONENTS = {
    'asio', 'concurrentqueue', 'dav1d', 'glib', 'inih', 'nlohmann_json',
    'kcp', 'libdatachannel', 'libffi', 'libnice', 'libsrtp', 'libyuv',
    'minirtc', 'openfec', 'openssl3', 'libopus', 'pcre2', 'plog',
    'proxy-libintl', 'spdlog', 'svt-av1', 'usrsctp', 'webrtc', 'websocketpp', 'zlib',
}


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd, text=True).strip()


def write_pages(output, catalog, additional, revision, minirtc):
    """Small navigation catalog and separate, offline-selectable license documents."""
    source = f'https://github.com/kunkundi/crossdesk/tree/{revision}'
    mini_source = f'https://github.com/kunkundi/minirtc/tree/{minirtc}'
    components = []
    for entry in catalog['components']:
        if entry['id'] not in COMPONENTS | {'crossdesk'}:
            continue
        component = dict(entry)
        if component['id'] == 'crossdesk':
            component['sourceURL'] = source
            component['documents'] = [
                {'title': 'LICENSE', 'text': (ROOT / 'LICENSE').read_text()},
                {'title': 'apps/android/licenses/OPEN_SOURCE_NOTICE.txt',
                 'text': (ANDROID / 'licenses/OPEN_SOURCE_NOTICE.txt').read_text()},
            ]
        if component['id'] in {'minirtc', 'inih', 'webrtc'}:
            component.update(version=minirtc, sourceURL=mini_source)
        if '/thirdparty/' in component.get('buildSourceURL', ''):
            recipe = component['buildSourceURL'].split('/thirdparty/', 1)[1]
            component['buildSourceURL'] = f'{mini_source}/thirdparty/{recipe}'
        components.append(component)
    components += additional
    # Ship the actual build instructions and recipes used by this local build. The Android
    # directory may not exist at the public base commit of an unpublished checkout.
    bundled = [
        ('构建说明', ANDROID / 'README.md'),
        ('原生构建配方', ANDROID / 'native/xmake.lua'),
        ('OpenSSL 构建配方', ROOT / 'deps/submodules/minirtc/thirdparty/openssl/xmake.lua'),
        ('MiniRTC 构建配方', ROOT / 'deps/submodules/minirtc/xmake.lua'),
        ('原生构建脚本', ANDROID / 'scripts/build_native.py'),
        ('SVT-AV1 构建配方', ROOT / 'deps/submodules/minirtc/thirdparty/svt-av1/xmake.lua'),
    ]
    build_documents = []
    for index, (title, path) in enumerate(bundled):
        asset = f'about/build/{index}.txt'
        target = output / asset
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(path.read_text(), encoding='utf-8')
        build_documents.append({'title': title, 'asset': asset})
    for component in components:
        if component['id'] in {'minirtc', 'glib', 'libnice', 'openfec', 'openh264', 'openssl3', 'svt-av1'}:
            component['buildDocuments'] = build_documents[1:]
        documents = []
        for index, document in enumerate(component['documents']):
            asset = f"about/licenses/{component['id']}/{index}.txt"
            target = output / asset
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(document['text'], encoding='utf-8')
            documents.append({'title': document['title'], 'asset': asset,
                              'sha256': hashlib.sha256(document['text'].encode('utf-8')).hexdigest()})
        component['documents'] = documents
    (output / 'ThirdPartyLicenses.json').write_text(json.dumps(
        {'schemaVersion': 1, 'components': components}, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    tags = git('tag', '--points-at', 'HEAD').splitlines()
    tag = os.environ.get('CROSSDESK_SOURCE_TAG')
    if tag and tag not in tags:
        raise ValueError(f'Source tag {tag!r} does not point to the build commit')
    if not tag:
        versions = sorted(value for value in tags if value.startswith(('v', 'android-v')))
        tag = versions[0] if versions else None
    dirty = bool(git('status', '--porcelain', '--untracked-files=normal', '--ignore-submodules=none'))
    if os.environ.get('CROSSDESK_RELEASE_BUILD') == 'YES' and (dirty or not tag or not tag.startswith(('v', 'android-v'))):
        raise ValueError('Release source must be a clean checkout of a version tag, with matching submodules')
    metadata = {
        'schemaVersion': 1, 'revision': revision, 'tag': tag,
        'isModified': dirty,
        'sourceURL': source,
        'buildInstructionsURL': f'https://github.com/kunkundi/crossdesk/blob/{quote(revision, safe="")}/apps/android/README.md',
        'buildDocuments': build_documents, 'miniRTCRevision': minirtc,
    }
    (output / 'SourceMetadata.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def main():
    output = ANDROID / 'app/build/generated/notices'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'PRIVACY.md').write_text((ROOT / 'PRIVACY.md').read_text(encoding='utf-8'), encoding='utf-8')
    catalog = json.loads((ROOT / 'apps/ios/CrossDeskMobile/Resources/ThirdPartyLicenses.json').read_text())
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    minirtc = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT / 'deps/submodules/minirtc', text=True).strip()
    parts = [
        'CrossDesk Android — open-source notices\n',
        'CrossDesk: GPL-3.0-only; MiniRTC: LGPL-3.0-only. Other components retain their own licenses.\n'
        'Open-source licenses govern use, modification and redistribution; their warranty limitations apply.\n'
        'You can rebuild/relink modified libraries and install an APK signed with your own key.\n',
        f'Application base revision: {revision}\nMiniRTC base revision: {minirtc}\n'
        'This development build may contain local changes in CrossDesk and the MiniRTC submodule.\n'
        'Source: https://github.com/kunkundi/crossdesk\n'
        'Build: apps/android/README.md; native source: deps/submodules/minirtc\n',
        (ROOT / 'LICENSE').read_text(),
    ]
    for component in catalog['components']:
        if component['id'] not in COMPONENTS:
            continue
        parts.append(f"\n{component['name']} — {component['license']}\n{component['sourceURL']}\n")
        for document in component['documents']:
            parts.extend([document['title'], document['text']])
    additional = json.loads((ANDROID / 'licenses/additional-notices.json').read_text())
    for component in additional:
        parts.append(component['name']+' '+component['version'])
        for document in component['documents']:
            parts.extend([document['title'], document['text']])
    (output / 'THIRD_PARTY_NOTICES.txt').write_text('\n\n'.join(parts), encoding='utf-8')
    write_pages(output, catalog, additional, revision, minirtc)


if __name__ == '__main__':
    main()
