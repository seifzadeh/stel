#!/usr/bin/env python3
"""Deploy Windows runtime DLLs, audit PE imports, and create native portable assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
import pefile

ROOT = Path(__file__).resolve().parents[1]


def imports(path):
    pe = pefile.PE(str(path), fast_load=True)
    try:
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
        return {entry.dll.decode('ascii').lower() for attr in
                ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT')
                for entry in getattr(pe, attr, [])}
    finally:
        pe.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build-windows')
    parser.add_argument('--qt', type=Path, required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--keychain', type=Path, required=True)
    parser.add_argument('--ca', type=Path, required=True)
    parser.add_argument('--crt', type=Path, required=True)
    parser.add_argument('--ucrt', type=Path, required=True)
    parser.add_argument('--notices', type=Path)
    args = parser.parse_args()
    stage = ROOT / 'dist/Stel-windows-x64'
    if stage.exists():
        if not (stage / 'bundle-manifest.json').exists():
            raise SystemExit('Refusing to replace a folder without a Stel manifest')
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    shutil.copy2(args.build / 'stel.exe', stage)
    subprocess.run([str(args.qt / 'bin/windeployqt.exe'), '--release', '--no-translations',
                    '--no-compiler-runtime', str(stage / 'stel.exe')], check=True)
    runtime_roots = [args.sdk / 'bin', args.keychain / 'bin', args.crt, args.ucrt, args.qt / 'bin']
    for directory in runtime_roots[:4]:
        files = list(directory.glob('*.dll'))
        if not files:
            raise RuntimeError(f'Runtime DLL directory is empty: {directory}')
        for file in files:
            shutil.copy2(file, stage / file.name)
    for relative in ('share', 'lib/mediastreamer/plugins', 'lib/linphone/plugins'):
        source = args.sdk / relative
        if source.exists():
            shutil.copytree(source, stage / relative, dirs_exist_ok=True)
    if not (stage / 'share/belr/grammars').is_dir():
        raise RuntimeError('SDK SIP grammar resources are missing')
    (stage / 'share/certs').mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.ca, stage / 'share/certs/ca-certificates.crt')
    for name in ('LICENSE', 'NOTICE.md', 'THIRD_PARTY.md', 'README.md', 'README.fa.md'):
        shutil.copy2(ROOT / name, stage / name)
    shutil.copytree(ROOT / 'LICENSES', stage / 'LICENSES')
    if args.notices:
        shutil.copytree(args.notices, stage / 'share/licenses')
    (stage / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n')
    search = {p.name.lower(): p for directory in runtime_roots for p in directory.glob('*.dll')}
    system = Path(os.environ['SystemRoot']) / 'System32'
    system_names = {p.name.lower() for p in system.glob('*.dll')}
    checked = set(); external = set()
    while True:
        pending = [p for p in stage.rglob('*') if p.suffix.lower() in ('.exe', '.dll') and p not in checked]
        if not pending:
            break
        for binary in pending:
            checked.add(binary)
            present = {p.name.lower() for directory in (stage, binary.parent) for p in directory.glob('*.dll')}
            for name in imports(binary):
                if name in present:
                    continue
                if name in search:
                    shutil.copy2(search[name], stage / search[name].name);present.add(name)
                elif name.startswith(('api-ms-', 'ext-ms-')) or name in system_names:
                    if re.match(r'(vcruntime|msvcp|concrt|ucrtbase)', name):
                        raise RuntimeError(f'Redistributable runtime must be bundled: {name}')
                    external.add(name)
                else:
                    raise RuntimeError(f'Unresolved DLL: {binary.relative_to(stage)} -> {name}')
    version = re.search(r'project\(Stel VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    manifest = {'version': version, 'architecture': 'x64', 'fully_static': False,
                'external_os_libraries': sorted(external), 'audited_pe_files': len(checked),
                'legacy_windows_runtime_verified': False,
                'files': {p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in stage.rglob('*') if p.is_file()}}
    (stage / 'bundle-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    with zipfile.ZipFile(stage.with_suffix('.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(stage.rglob('*')):
            if p.is_file():
                archive.write(p, p.relative_to(stage.parent))
    subprocess.run(['python', str(ROOT / 'scripts/package-single.py'), '--launcher',
                    str(args.build / 'stel_portable_launcher.exe'), '--folder', str(stage),
                    '--output', str(stage.parent / 'Stel-portable-windows-x64.exe')], check=True)
    print(f'Packaged {len(checked)} audited PE files; system dependencies recorded in the manifest')


if __name__ == '__main__':
    main()
