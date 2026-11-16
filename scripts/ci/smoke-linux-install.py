#!/usr/bin/env python3
"""Install/reinstall/remove the real package only inside the disposable CI job."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import ROOT, version


def run(*args):
    subprocess.run(list(map(str, args)), check=True)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('format', choices=['deb', 'rpm'])
parser.add_argument('package', type=Path)
args = parser.parse_args()
if os.environ.get('GITHUB_ACTIONS') != 'true' or os.geteuid() != 0:
    raise SystemExit('System installation tests run only in the disposable root CI container')
package = args.package.resolve()
install = ['apt-get', 'install', '-y', str(package)] if args.format == 'deb' else ['dnf', 'install', '-y', str(package)]
reinstall = ['apt-get', 'install', '--reinstall', '-y', str(package)] if args.format == 'deb' else ['rpm', '-Uvh', '--replacepkgs', str(package)]
remove = ['apt-get', 'remove', '-y', 'stel'] if args.format == 'deb' else ['dnf', 'remove', '-y', 'stel']
with tempfile.TemporaryDirectory(prefix='stel-installed-test-') as temporary:
    canary = Path(temporary) / 'settings-to-preserve.json'; canary.write_text('keep user settings')
    for command in (install, reinstall):
        run(*command)
        assert Path('/usr/bin/stel').resolve() == Path('/opt/stel/stel')
        assert Path('/usr/share/applications/stel.desktop').is_file()
        assert Path('/usr/share/icons/hicolor/128x128/apps/stel.png').is_file()
        actual = subprocess.check_output(['/usr/bin/stel', '--version'], text=True).strip()
        assert actual == 'Stel ' + version(), actual
        run('desktop-file-validate', '/usr/share/applications/stel.desktop')
    run(sys.executable, ROOT / 'scripts/ci/smoke-portable.py', '/usr/bin/stel')
    run(*remove)
    for path in ('/opt/stel/stel', '/usr/bin/stel', '/usr/share/applications/stel.desktop'):
        assert not os.path.lexists(path), path
    assert canary.read_text() == 'keep user settings'
print(f'PASS {args.format}: install, reinstall, installed binary, desktop entry and removal')
