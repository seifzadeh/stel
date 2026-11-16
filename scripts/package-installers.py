#!/usr/bin/env python3
"""Build native installers from the audited, tested portable application."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

from release import ROOT, version


def checked(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def validate_payload(payload, manifest, expected_version, platform):
    data = json.loads(manifest.read_text())
    if data.get('version') != expected_version or not payload.is_file():
        raise ValueError('Portable binary and bundle manifest must match the release version')
    if data.get('architecture') != ('x64' if platform == 'windows' else 'x86_64'):
        raise ValueError('Only the audited x86-64 build can be packaged')
    with payload.open('rb') as stream:
        magic = stream.read(4)
    if not (magic.startswith(b'MZ') if platform == 'windows' else magic == b'\x7fELF'):
        raise ValueError('Executable format does not match the installer platform')
    # Windows runs --version without loading the GUI. Linux launchers also
    # support it without depending on a display or developer library paths.
    result = subprocess.run([str(payload.resolve()), '--version'], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=60,
                            check=True)
    # Runtime libraries may print their own startup messages on Windows. Match
    # the application's complete version line, never a substring in those logs.
    versions = [line.strip() for line in result.stdout.splitlines()
                if re.fullmatch(r'Stel \d+\.\d+\.\d+', line.strip())]
    print(f'Portable --version stdout: {result.stdout!r}')
    if versions != ['Stel ' + expected_version]:
        raise ValueError(f'Portable executable version differs from its bundle manifest: '
                         f'expected Stel {expected_version!s}; '
                         f'stdout={result.stdout!r}; stderr={result.stderr!r}')
    return data


def linux_tree(root, payload):
    def copy(source, target):
        path = root / target; path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, path)
    copy(payload, 'opt/stel/stel')
    (root / 'opt/stel/stel').chmod(0o755)
    (root / 'usr/bin').mkdir(parents=True)
    (root / 'usr/bin/stel').symlink_to('../../opt/stel/stel')
    copy(ROOT / 'packaging/stel.desktop', 'usr/share/applications/stel.desktop')
    for size in (16, 24, 32, 48, 64, 128, 256, 512):
        copy(ROOT / f'packaging/icons/stel-{size}.png',
             f'usr/share/icons/hicolor/{size}x{size}/apps/stel.png')
    copy(ROOT / 'packaging/stel.svg', 'usr/share/icons/hicolor/scalable/apps/stel.svg')
    for name in ('README.md', 'README.fa.md', 'THIRD_PARTY.md', 'NOTICE.md'):
        copy(ROOT / name, 'usr/share/doc/stel/' + name)
    copy(ROOT / 'LICENSE', 'usr/share/licenses/stel/COPYING')
    copy(ROOT / 'LICENSES/AGPL-3.0.txt', 'usr/share/licenses/stel/AGPL-3.0.txt')


def linux(payload, manifest, output):
    current = version(); data = validate_payload(payload, manifest, current, 'linux')
    minimum = data.get('minimum_glibc', '')
    if not re.fullmatch(r'\d+\.\d+', minimum):
        raise ValueError('The audited minimum glibc is required')
    if data.get('includes_development_tests'):
        raise ValueError('Do not distribute development tests in the installers')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='stel-installers-') as directory:
        work = Path(directory); tree = work / 'payload'; tree.mkdir()
        linux_tree(tree, payload)
        control = tree / 'DEBIAN'; control.mkdir()
        size = sum(p.stat().st_size for p in tree.rglob('*')
                   if p.is_file() and not p.is_symlink()) // 1024 + 1
        (control / 'control').write_text(f'''Package: stel
Version: {current}
Section: net
Priority: optional
Architecture: amd64
Maintainer: Mehrdad Seifzadeh <mehrdad.seifzadeh@gmail.com>
Installed-Size: {size}
Depends: libc6 (>= {minimum})
Recommends: gnome-keyring | kwallet6
Homepage: https://github.com/seifzadeh/stel
Description: Multi-account SIP softphone with voice, video, chat and fax
 Stel is a Qt/C++ softphone based on liblinphone. Application libraries are
 included. Accounts and history are stored in the user's profile.
''')
        deb = output / f'stel_{current}_amd64.deb'
        checked(['dpkg-deb', '--build', '--root-owner-group', '-Zgzip', '-z1', tree, deb])
        shutil.rmtree(control)
        top = work / 'rpm'
        for name in ('BUILD', 'BUILDROOT', 'RPMS', 'SOURCES', 'SPECS', 'SRPMS'):
            (top / name).mkdir(parents=True)
        with tarfile.open(top / 'SOURCES/payload.tar', 'w') as archive:
            def owner(info):
                info.uid = info.gid = 0; info.uname = info.gname = 'root'; return info
            for path in sorted(tree.iterdir()):
                archive.add(path, arcname=path.name, filter=owner)
        spec = ROOT / 'packaging/stel.spec.in'
        text = spec.read_text().replace('@VERSION@', current).replace('@GLIBC@', minimum)
        target = top / 'SPECS/stel.spec'; target.write_text(text)
        checked(['rpmbuild', '-bb', '--define', f'_topdir {top}', target])
        rpm = top / f'RPMS/x86_64/stel-{current}-1.x86_64.rpm'
        shutil.copy2(rpm, output / rpm.name)
        print(f'Created {deb.name} and {rpm.name}; glibc >= {minimum}')


def windows(payload, manifest, output, makensis):
    current = version(); validate_payload(payload, manifest, current, 'windows')
    output.mkdir(parents=True, exist_ok=True)
    destination = (output / 'Stel-setup-windows-x64.exe').resolve()
    arguments = [f'VERSION={current}', f'PAYLOAD={payload.resolve()}',
                 f'OUTPUT={destination}', f'PROJECT={ROOT}',
                 f'ESTIMATED_SIZE={payload.stat().st_size // 1024 + 200}']
    prefix = '/D' if os.name == 'nt' else '-D'
    checked([makensis, *[prefix + item for item in arguments], ROOT / 'packaging/stel.nsi'])
    print(f'Created {destination}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=['linux', 'windows'])
    parser.add_argument('--payload', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--makensis', default='makensis')
    args = parser.parse_args()
    defaults = ('Stel-linux-x86_64.run', 'Stel-linux-x86_64') if args.platform == 'linux' else ('Stel-portable-windows-x64.exe', 'Stel-windows-x64')
    payload = args.payload or ROOT / 'dist' / defaults[0]
    manifest = args.manifest or ROOT / 'dist' / defaults[1] / 'bundle-manifest.json'
    if args.platform == 'linux': linux(payload, manifest, args.output)
    else: windows(payload, manifest, args.output, args.makensis)


if __name__ == '__main__':
    main()
