#!/usr/bin/env python3
"""Invoked only by the trusted Actions release job, after all builds pass."""
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release import version, checksums

if os.environ.get('GITHUB_ACTIONS') != 'true':
    raise SystemExit('Publication is only available in GitHub Actions; local preparation never publishes')
repo = os.environ['GITHUB_REPOSITORY']
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
tag = 'v' + version()
request = urllib.request.Request(f'https://api.github.com/repos/{repo}/releases/tags/{tag}',
    headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'], 'Accept': 'application/vnd.github+json'})
try:
    with urllib.request.urlopen(request, timeout=30) as response:
        existing = json.load(response)
except urllib.error.HTTPError as error:
    if error.code != 404:
        raise
    existing = None
if existing and not existing['draft']:
    print(f'{tag} already published; it will not be overwritten. Bump the CMake version for a new release.')
    sys.exit(0)
if existing and existing['target_commitish'] != commit:
    raise SystemExit('An unfinished draft exists for another commit; review it before retrying')
assets = Path('release-assets')
required = {f'Stel-{version()}-linux-x86_64.run', f'Stel-{version()}-linux-x86_64.tar.gz',
            f'Stel-{version()}-windows-x64.exe', f'Stel-{version()}-windows-x64.zip',
            f'Stel-source-{version()}.tar.gz', f'Stel-{version()}-dependencies-linux-sources.tar.gz',
            f'Stel-{version()}-dependencies-windows-sources.tar.gz'}
if not required.issubset({p.name for p in assets.iterdir() if p.is_file()}):
    raise SystemExit('Missing required platform/source assets; refusing an incomplete release')
# A manually supplied tag must reference this exact source commit.
tag_request = urllib.request.Request(f'https://api.github.com/repos/{repo}/git/ref/tags/{tag}',
    headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'], 'Accept': 'application/vnd.github+json'})
try:
    with urllib.request.urlopen(tag_request, timeout=30) as response:
        obj = json.load(response)['object']
    while obj['type'] == 'tag':
        nested = urllib.request.Request(obj['url'], headers=tag_request.headers)
        with urllib.request.urlopen(nested, timeout=30) as response:
            obj = json.load(response)['object']
    if obj['sha'] != commit:
        raise SystemExit('Release tag points to another commit')
except urllib.error.HTTPError as error:
    if error.code != 404:
        raise
checksums(assets)
files = sorted(str(p) for p in assets.iterdir() if p.is_file())
if any(Path(p).stat().st_size >= 2 * 1024**3 for p in files):
    raise SystemExit('An asset exceeds GitHub\'s 2 GiB per-file limit')
if not existing:
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repo, '--target', commit,
                    '--title', 'Stel ' + version(), '--notes-file', 'release-notes.md', '--draft'], check=True)
subprocess.run(['gh', 'release', 'upload', tag, '--repo', repo, '--clobber', *files], check=True)
subprocess.run(['gh', 'release', 'edit', tag, '--repo', repo, '--draft=false', '--latest'], check=True)
