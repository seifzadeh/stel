#!/usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import ROOT, platform_assets

parser = argparse.ArgumentParser()
parser.add_argument('platform', choices=['linux', 'windows'])
args = parser.parse_args()
files = platform_assets(args.platform)
destination = ROOT / 'release-assets'
destination.mkdir(exist_ok=True)
for source, name in files.items():
    shutil.copy2(ROOT / 'dist' / source, destination / name)
