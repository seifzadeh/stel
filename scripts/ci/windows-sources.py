#!/usr/bin/env python3
"""Archive pinned SDK/submodule sources and matching official Qt module sources."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DEPS = Path('C:/stel-deps')
DEST = ROOT / 'dist/windows-sources'
DEST.mkdir(parents=True, exist_ok=True)
lock = json.loads((ROOT / 'packaging/dependencies.json').read_text())
qt = lock['qt']['version']
manifest = {'dependencies': lock, 'qt_sources': []}
for module in ('qtbase', 'qtsvg'):
    name = f'{module}-everywhere-src-{qt}.tar.xz'
    url = f'https://download.qt.io/archive/qt/5.15/{qt}/submodules/{name}'
    with urllib.request.urlopen(url + '.sha256', timeout=120) as response:
        expected = response.read().decode().split()[0].lower()
    with urllib.request.urlopen(url, timeout=120) as response, (DEST / name).open('wb') as output:
        shutil.copyfileobj(response, output)
    actual = hashlib.sha256((DEST / name).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'Qt source checksum mismatch: {module}')
    manifest['qt_sources'].append({'url': url, 'sha256': actual})
(DEST / 'sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
(DEST / 'README.txt').write_text(
    'Matching source for the Windows Stel release. Qt module tarballs are official 5.15.2 sources.\n'
    'linphone-sdk includes every pinned submodule used by the Windows build profile. Revision list and build options\n'
    'are in notices/ and the accompanying Stel source archive (scripts/ci/windows-deps.ps1).\n'
    'MSVC/UCRT redistributables are Microsoft system runtime components, not GPL code.\n')
def include(member):
    if '.git' in Path(member.name).parts:
        return None
    return member
archive = ROOT / 'dist/Stel-dependencies-windows-sources.tar.gz'
with tarfile.open(archive, 'w:gz') as tar:
    tar.add(DEPS / 'src', arcname='dependencies/src', filter=include)
    tar.add(DEPS / 'notices', arcname='dependencies/notices')
    tar.add(DEST, arcname='dependencies/qt')
print(archive)
