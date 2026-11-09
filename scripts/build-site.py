#!/usr/bin/env python3
"""Build a dependency-free static GitHub Pages site, with the CMake version."""
import argparse
from pathlib import Path
import shutil
from release import ROOT, version

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / '_site')
args = parser.parse_args()
if args.output.resolve() in (ROOT, ROOT / 'website'):
    raise SystemExit('Choose a separate output directory')
shutil.copytree(ROOT / 'website', args.output, dirs_exist_ok=True)
path = args.output / 'index.html'
path.write_text(path.read_text().replace('__STEL_VERSION__', version()))
(args.output / '.nojekyll').touch()
print(f'Stel {version()} site built in {args.output}')
