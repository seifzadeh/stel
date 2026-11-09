#!/usr/bin/env python3
"""Local release preparation. This tool never commits, pushes or calls GitHub."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = r'project\(Stel VERSION (\d+\.\d+\.\d+) LANGUAGES'
SOURCE_PATHS = ('CMakeLists.txt', '.gitignore', '.gitattributes', '.github', 'cmake', 'src',
                'tests', 'scripts', 'docs', 'website', 'packaging', 'third_party',
                'README.md', 'README.fa.md', 'LICENSE', 'LICENSES', 'NOTICE.md',
                'THIRD_PARTY.md', 'CHANGELOG.md', 'CONTRIBUTING.md')


def version(root=ROOT):
    match = re.search(VERSION_PATTERN, (root / 'CMakeLists.txt').read_text())
    if not match:
        raise ValueError('CMakeLists.txt must contain a three-part Stel version')
    return match[1]


def source_archive(output, root=ROOT):
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'Stel-source-{version(root)}.tar.gz'
    def include(item):
        parts = Path(item.name).parts
        if any(p in ('.git', '.secrets', '__pycache__', 'node_modules') for p in parts):
            return None
        if item.issym() or item.islnk():
            raise ValueError(f'Unexpected link in source tree: {item.name}')
        if item.name.endswith(('.pyc', '.log', '.key', '.pem', '.env')):
            return None
        item.uid = item.gid = 0
        item.uname = item.gname = ''
        return item
    with tarfile.open(archive, 'w:gz') as tar:
        for name in SOURCE_PATHS:
            if (root / name).exists():
                tar.add(root / name, arcname=f'Stel-{version(root)}/{name}', filter=include)
    return archive


def checksums(directory):
    files = sorted(p for p in directory.iterdir() if p.is_file() and p.name != 'SHA256SUMS')
    if not files:
        raise ValueError('No release assets found')
    lines = []
    for path in files:
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
        lines.append(f'{digest.hexdigest()}  {path.name}\n')
    (directory / 'SHA256SUMS').write_text(''.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    meta = sub.add_parser('metadata')
    meta.add_argument('--github-output', action='store_true')
    meta.add_argument('--ref', default='')
    bump = sub.add_parser('bump');bump.add_argument('version')
    source = sub.add_parser('source');source.add_argument('--output', type=Path, default=ROOT / 'dist')
    sums = sub.add_parser('checksums');sums.add_argument('directory', type=Path)
    notes = sub.add_parser('notes');notes.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'bump':
        if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', args.version):
            parser.error('Use a version such as 0.6.1')
        path = ROOT / 'CMakeLists.txt'
        path.write_text(re.sub(VERSION_PATTERN, f'project(Stel VERSION {args.version} LANGUAGES', path.read_text()))
        print(f'Version set to {args.version}. Add its CHANGELOG.md entry before committing.')
    elif args.command == 'metadata':
        v = version();tag = 'v' + v
        if args.ref.startswith('refs/tags/') and args.ref != 'refs/tags/' + tag:
            parser.error(f'Tag must match CMake version: {tag}')
        data = {'version': v, 'tag': tag}
        if args.github_output:
            with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                output.write(''.join(f'{key}={value}\n' for key, value in data.items()))
        print(json.dumps(data))
    elif args.command == 'source':
        print(source_archive(args.output))
    elif args.command == 'checksums':
        checksums(args.directory)
    elif args.command == 'notes':
        text = (ROOT / 'CHANGELOG.md').read_text()
        match = re.search(r'^## ' + re.escape(version()) + r'\b[^\n]*\n(.*?)(?=^## |\Z)', text, re.S | re.M)
        if not match:
            parser.error('Add a CHANGELOG entry for this version')
        args.output.write_text(f'# Stel {version()}\n\n' + match[1].strip() + '\n\n'
                              'Download the native `.run` (Linux) or portable `.exe` (Windows); no shell launcher is required. '
                              'Archives include runtime dependencies and notices. SHA256SUMS verifies all assets. '
                              'Source archives include matching application and dependency sources. '
                              'See BUILDING.md and RELEASE.md in the source archive for platform baselines.\n')


if __name__ == '__main__':
    main()
