#!/usr/bin/env python3
"""Build a relocatable Linux directory with the ELF dependency closure.

glibc and the OS loader remain platform dependencies. Drivers, the display
server and audio services remain OS facilities. A closure audit is emitted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

PROJECT = Path(__file__).resolve().parents[1]
BASE = re.compile(r'^(libc\.so\.|libm\.so\.|libpthread\.so\.|libdl\.so\.|librt\.so\.|libresolv\.so\.|ld-linux|libutil\.so\.|libanl\.so\.)')


def dependencies(path, env):
    proc = subprocess.run(['ldd', str(path)], env=env, text=True, capture_output=True)
    result = []
    for line in proc.stdout.splitlines():
        if 'not found' in line:
            raise RuntimeError(f'Unresolved dependency of {path}: {line}')
        match = re.search(r'^\s*(\S+)\s+=>\s+(/\S+)', line)
        if match:
            result.append((match[1], Path(match[2])))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=PROJECT / 'build')
    parser.add_argument('--output', type=Path, default=PROJECT / 'dist/Stel-linux-x86_64')
    args = parser.parse_args()
    source_origins = set()
    def runtime_file(source, destination):
        source_origins.add(str(Path(source)))
        source_origins.add(str(Path(source).resolve()))
        shutil.copy2(source, destination)
    def runtime_tree(source, destination):
        for path in Path(source).rglob('*'):
            if path.is_file():
                source_origins.add(str(path));source_origins.add(str(path.resolve()))
        shutil.copytree(source, destination, symlinks=False)
    output = args.output.resolve()
    if output.exists():
        # Only replace our own generated directory.
        if not (output / 'bundle-manifest.json').exists():
            raise SystemExit('Output directory exists without a Stel manifest; refusing to replace it.')
        shutil.rmtree(output)
    for part in ('bin', 'lib', 'plugins', 'share/doc/stel', 'share/licenses', 'share/certs'):
        (output / part).mkdir(parents=True, exist_ok=True)
    executable = args.build.resolve() / 'stel'
    shutil.copy2(executable, output / 'stel')
    if (args.build / 'stel_probe').exists():
        shutil.copy2(args.build / 'stel_probe', output / 'bin/stel_probe')
    if (args.build / 'stel_tests').exists():
        shutil.copy2(args.build / 'stel_tests', output / 'bin/stel_tests')
    if (args.build / 'stel_ui_tests').exists():
        shutil.copy2(args.build / 'stel_ui_tests', output / 'bin/stel_ui_tests')
    if shutil.which('strip'):
        subprocess.run(['strip', '--strip-unneeded', str(output / 'stel')], check=True)
        for binary in (output / 'bin').iterdir():
            subprocess.run(['strip', '--strip-unneeded', str(binary)], check=True)
    plugin_prefix = Path('/usr/lib/x86_64-linux-gnu/qt5/plugins')
    for name in ('platforms', 'iconengines', 'xcbglintegrations', 'platforminputcontexts', 'bearer'):
        if (plugin_prefix / name).exists():
            runtime_tree(plugin_prefix / name, output / 'plugins' / name)
    (output / 'plugins/imageformats').mkdir()
    for name in ('libqjpeg.so', 'libqsvg.so'):
        source = plugin_prefix / 'imageformats' / name
        if source.exists():
            runtime_file(source, output / 'plugins/imageformats' / name)
    for source, dest in [('/usr/share/belr/grammars', 'share/belr/grammars'),
                         ('/usr/share/linphone', 'share/linphone'),
                         ('/usr/share/sounds/linphone', 'share/sounds/linphone'),
                         ('/usr/share/mediastreamer2', 'share/mediastreamer2'),
                         ('/usr/share/alsa', 'share/alsa')]:
        if Path(source).exists():
            runtime_tree(source, output / dest)
    msplugins = output / 'lib/mediastreamer/plugins'
    msplugins.mkdir(parents=True, exist_ok=True)
    # Qt Widgets renders into native windows. The QML renderer plugin would
    # unnecessarily pull Qt Quick/QML into this small application.
    for source in Path('/usr/lib/x86_64-linux-gnu/mediastreamer/plugins').glob('*.so'):
        if source.name != 'libmsqogl.so':
            runtime_file(source, msplugins / source.name)
    alsaplugins = Path('/usr/lib/x86_64-linux-gnu/alsa-lib')
    if alsaplugins.exists():
        runtime_tree(alsaplugins, output / 'lib/alsa-lib')
    ca = Path('/etc/ssl/certs/ca-certificates.crt')
    if ca.exists():
        shutil.copy2(ca, output / 'share/certs/ca-certificates.crt')
    for extra in ('libsoci_sqlite3.so.4.0', 'libssl.so.3', 'libcrypto.so.3'):
        source = Path('/usr/lib/x86_64-linux-gnu') / extra
        if source.exists():
            runtime_file(source, output / 'lib' / extra)
    for file in ('README.md', 'README.fa.md', 'LICENSE', 'NOTICE.md', 'THIRD_PARTY.md'):
        if not (PROJECT / file).exists():
            continue
        shutil.copy2(PROJECT / file, output / 'share/doc/stel' / file)
    shutil.copytree(PROJECT / 'LICENSES', output / 'share/doc/stel/LICENSES')
    if (PROJECT / 'docs').exists():
        shutil.copytree(PROJECT / 'docs', output / 'share/doc/stel/docs')
    for component in ('ms2-av1', 'ms2-legacy'):
        for file in ('README.md', 'LICENSE.txt'):
            source = PROJECT / 'third_party' / component / file
            if source.exists():
                target = output / 'share/licenses' / component
                target.mkdir(exist_ok=True)
                shutil.copy2(source, target / file)
    shutil.copytree('/usr/share/common-licenses', output / 'share/licenses/common', symlinks=False)
    queue = [p for p in output.rglob('*') if p.is_file() and p.read_bytes()[:4] == b'\x7fELF']
    origins = {}; base = set(); checked = set()
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(output / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
    while queue:
        file = queue.pop()
        if file in checked:
            continue
        checked.add(file)
        for soname, source in dependencies(file, env):
            if BASE.match(soname):
                base.add(soname)
                continue
            dest = output / 'lib' / soname
            if not dest.exists():
                shutil.copy2(source, dest)
                origins[soname] = str(source.resolve())
                source_origins.add(str(source));source_origins.add(str(source.resolve()))
                queue.append(dest)
    patchelf = shutil.which('patchelf')
    if not patchelf:
        raise SystemExit('patchelf is required (source scripts/env.sh).')
    elf_files = [p for p in output.rglob('*') if p.is_file() and p.read_bytes()[:4] == b'\x7fELF']
    for file in elf_files:
        # No reference to the development prefix survives packaging.
        relative = os.path.relpath(output / 'lib', file.parent)
        subprocess.run([patchelf, '--set-rpath', '$ORIGIN/' + relative, str(file)], check=True)
    (output / 'bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\n')
    (output / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=plugins\n')
    shutil.copy2(PROJECT / 'packaging/stel.desktop', output)
    shutil.copy2(PROJECT / 'packaging/stel.svg', output)
    for size in (16,24,32,48,64,128,256,512):
        icon_dir = output / f'share/icons/hicolor/{size}x{size}/apps'
        icon_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PROJECT / f'packaging/icons/stel-{size}.png', icon_dir / 'stel.png')
    scalable = output / 'share/icons/hicolor/scalable/apps'
    scalable.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PROJECT / 'packaging/stel.svg', scalable / 'stel.svg')
    # Collect copyright/source package metadata for bundled distribution libraries.
    packages = set()
    for origin in source_origins:
        if '/.deps/root/' in origin:
            origin = '/' + origin.split('/.deps/root/', 1)[1]
        proc = subprocess.run(['dpkg-query', '-S', origin], capture_output=True, text=True)
        for line in proc.stdout.splitlines():
            packages.add(line.split(': ')[0].split(':')[0])
    package_info = []
    source_packages = {}
    for package in sorted(packages):
        copyright = Path('/usr/share/doc') / package / 'copyright'
        if copyright.exists():
            shutil.copy2(copyright, output / 'share/licenses' / (package + '.copyright'))
        version = subprocess.run(['dpkg-query', '-W', '-f=${Package}\t${Version}\t${source:Package}\t${source:Version}', package], capture_output=True, text=True, check=True)
        package_info.append(version.stdout)
        binary, binary_version, source, source_version = version.stdout.split('\t')
        key = (source, source_version)
        source_packages.setdefault(key, {'source': source, 'source_version': source_version, 'binaries': []})['binaries'].append({'name': binary, 'version': binary_version})
    # The rootless development environment extracts QtKeychain without adding it
    # to the host dpkg database. Retain its exact .deb provenance as well.
    if not any(item['source'] == 'qtkeychain' for item in source_packages.values()):
        for deb in (PROJECT / '.deps/debs').glob('libqt5keychain1_*.deb'):
            fields = subprocess.check_output(['dpkg-deb', '--show',
                '--showformat=${Package}\t${Version}\t${Source}', str(deb)], text=True).split('\t')
            binary, binary_version, source_field = fields
            source = source_field.split()[0] if source_field else binary
            match = re.search(r'\(([^)]+)\)', source_field)
            source_version = match[1] if match else binary_version
            source_packages[(source, source_version)] = {'source': source, 'source_version': source_version,
                'binaries': [{'name': binary, 'version': binary_version}]}
            package_info.append('\t'.join([binary, binary_version, source, source_version]))
    (output / 'share/licenses/debian-packages.txt').write_text('\n'.join(package_info) + '\n')
    (output / 'share/licenses/debian-sources.json').write_text(json.dumps(list(source_packages.values()), indent=2) + '\n')
    keychain_notice = PROJECT / '.deps/root/usr/share/doc/libqt5keychain1/copyright'
    if keychain_notice.exists():
        shutil.copy2(keychain_notice, output / 'share/licenses/qtkeychain.copyright')
    # Audit every ELF with bundle search paths. Only the documented OS base may resolve externally.
    external = set()
    for file in elf_files:
        for soname, location in dependencies(file, env):
            if not str(location).startswith(str(output)):
                if not BASE.match(soname):
                    raise RuntimeError(f'Non-base external dependency: {soname}: {location}')
                external.add(soname)
    symbols = set()
    for file in elf_files:
        proc = subprocess.run(['readelf', '--version-info', str(file)], capture_output=True, text=True)
        symbols.update(re.findall(r'GLIBC_(\d+\.\d+)', proc.stdout))
    minimum = max(symbols, key=lambda x: tuple(map(int, x.split('.'))))
    version = re.search(r'project\(Stel VERSION ([0-9.]+)', (PROJECT / 'CMakeLists.txt').read_text())[1]
    manifest = {'version': version, 'architecture': 'x86_64', 'fully_static': False,
                'minimum_glibc': minimum, 'external_os_libraries': sorted(external),
                'bundled_elf_count': len(elf_files), 'development_prefix': 'removed',
                'files': {str(p.relative_to(output)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in output.rglob('*') if p.is_file()}}
    (output / 'bundle-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = output.with_suffix('.tar.gz')
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(output, arcname=output.name)
    print(f'Created {archive}: {len(elf_files)} ELF files; glibc >= {minimum}; dependency audit passed.')
    launcher = args.build.resolve() / 'stel_portable_launcher'
    if launcher.exists():
        subprocess.run(['python3', str(PROJECT / 'scripts/package-single.py'), '--launcher', str(launcher),
                        '--folder', str(output), '--output', str(output.parent / 'Stel-linux-x86_64.run')], check=True)


if __name__ == '__main__':
    main()
