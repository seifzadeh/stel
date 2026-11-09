#!/usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import ROOT, version

parser = argparse.ArgumentParser()
parser.add_argument('platform', choices=['linux', 'windows'])
args = parser.parse_args()
v = version()
files = ({'Stel-linux-x86_64.run': f'Stel-{v}-linux-x86_64.run',
          'Stel-linux-x86_64.tar.gz': f'Stel-{v}-linux-x86_64.tar.gz',
          'Stel-dependencies-linux-sources.tar.gz': f'Stel-{v}-dependencies-linux-sources.tar.gz'}
         if args.platform == 'linux' else
         {'Stel-portable-windows-x64.exe': f'Stel-{v}-windows-x64.exe',
          'Stel-windows-x64.zip': f'Stel-{v}-windows-x64.zip',
          'Stel-dependencies-windows-sources.tar.gz': f'Stel-{v}-dependencies-windows-sources.tar.gz'})
destination = ROOT / 'release-assets'
destination.mkdir(exist_ok=True)
for source, name in files.items():
    shutil.copy2(ROOT / 'dist' / source, destination / name)
