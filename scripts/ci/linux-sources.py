#!/usr/bin/env python3
"""Download exact Debian source packages for the audited binary bundle."""
import argparse
import json
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('bundle', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
metadata = json.loads((args.bundle / 'share/licenses/debian-sources.json').read_text())
if not metadata:
    raise SystemExit('No corresponding-source metadata was collected')
args.output.mkdir(parents=True, exist_ok=True)
sources = args.output / 'debian-sources'
sources.mkdir(exist_ok=True)
for package in metadata:
    subprocess.run(['apt-get', 'source', '--download-only', '--yes',
                    f"{package['source']}={package['source_version']}"], cwd=sources, check=True)
(sources / 'packages.json').write_text(json.dumps(metadata, indent=2) + '\n')
(sources / 'README.txt').write_text(
    'Exact Debian source packages corresponding to this Stel bundle.\n'
    'Unpack each .dsc with dpkg-source -x; retain the orig tarballs and Debian patches.\n'
    'Build metadata is in each package\'s debian/ directory; Stel build scripts are in its source archive.\n')
with tarfile.open(args.output / 'Stel-dependencies-linux-sources.tar.gz', 'w:gz') as tar:
    tar.add(sources, arcname=sources.name)
print(f'Downloaded {len(metadata)} matching source packages')
