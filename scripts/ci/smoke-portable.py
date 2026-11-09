#!/usr/bin/env python3
"""Execute a portable file alone, with a clean runtime environment and Unicode paths."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import version

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
env = os.environ.copy()
for key in list(env):
    if key.startswith(('STEL_', 'QT_')) or key in ('LD_LIBRARY_PATH', 'QTDIR', 'CMAKE_PREFIX_PATH'):
        env.pop(key, None)
env['PATH'] = (os.pathsep.join([str(Path(env['SystemRoot']) / 'System32'), env['SystemRoot']])
               if os.name == 'nt' else '/usr/bin:/bin')
env['QT_QPA_PLATFORM'] = 'windows' if os.name == 'nt' else 'offscreen'
env['QT_QPA_PLATFORMTHEME'] = ''
with tempfile.TemporaryDirectory(prefix='Stel تست standalone ') as directory:
    root = Path(directory)
    binary = root / ('Stel.exe' if os.name == 'nt' else 'Stel.run')
    shutil.copy2(args.binary, binary)
    for flags in (['--version'], ['--config-dir', str(root / 'تنظیمات مستقل'), '--settings', '--smoke-test']):
        proc = subprocess.run([str(binary), *flags], env=env, capture_output=True, timeout=45)
        if proc.returncode or (flags == ['--version'] and version().encode() not in proc.stdout):
            sys.stdout.buffer.write(proc.stdout);sys.stderr.buffer.write(proc.stderr)
            raise SystemExit(f'Portable smoke failed: {flags}, exit={proc.returncode}')
        print(f'PASS portable {flags[0]}')
