#!/usr/bin/env python3
"""Verify package metadata and prove packaging preserved the ELF's appended payload."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import ROOT, version


def output(*command):
    return subprocess.check_output(list(map(str, command)), text=True).strip()


v = version(); dist = ROOT / 'dist'
manifest = json.loads((dist / 'Stel-linux-x86_64/bundle-manifest.json').read_text())
assert manifest['version'] == v
deb = dist / f'stel_{v}_amd64.deb'; rpm = dist / f'stel-{v}-1.x86_64.rpm'
assert output('dpkg-deb', '-f', deb, 'Version') == v
assert output('dpkg-deb', '-f', deb, 'Architecture') == 'amd64'
assert f"libc6 (>= {manifest['minimum_glibc']})" in output('dpkg-deb', '-f', deb, 'Depends')
with tempfile.TemporaryDirectory(prefix='stel-rpm-db-') as database:
    subprocess.run(['rpmdb', '--dbpath', database, '--initdb'], check=True)
    assert output('rpm', '--dbpath', database, '-qp', '--qf', '%{VERSION} %{ARCH}', rpm) == v + ' x86_64'
    assert f"glibc >= {manifest['minimum_glibc']}" in output('rpm', '--dbpath', database, '-qp', '--requires', rpm)
expected = hashlib.sha256((dist / 'Stel-linux-x86_64.run').read_bytes()).hexdigest()
for kind, package in [('deb', deb), ('rpm', rpm)]:
    with tempfile.TemporaryDirectory(prefix='stel-package-check-') as directory:
        root = Path(directory)
        if kind == 'deb':
            subprocess.run(['dpkg-deb', '-x', str(package), str(root)], check=True)
        else:
            with subprocess.Popen(['rpm2cpio', str(package)], stdout=subprocess.PIPE) as producer:
                subprocess.run(['cpio', '-idm', '--quiet', '--no-absolute-filenames'], cwd=root,
                               stdin=producer.stdout, check=True)
                producer.stdout.close()
                if producer.wait(): raise RuntimeError('RPM payload extraction failed')
        app = root / 'opt/stel/stel'
        assert hashlib.sha256(app.read_bytes()).hexdigest() == expected, 'The packaged ELF payload was changed'
        assert (root / 'usr/bin/stel').readlink() == Path('../../opt/stel/stel')
        assert (root / 'usr/share/icons/hicolor/128x128/apps/stel.png').is_file()
        subprocess.run(['desktop-file-validate', str(root / 'usr/share/applications/stel.desktop')], check=True)
        subprocess.run([sys.executable, str(ROOT / 'scripts/ci/smoke-portable.py'), str(app)], check=True)
    print(f'PASS {kind}: metadata, intact application payload, menu/icon and standalone launch')
