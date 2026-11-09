#!/usr/bin/env python3
"""Restore checksummed upstream Git snapshots unavailable from Git mirrors."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]


def restore(sdk, entry, cache):
    expected = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse',
        'HEAD:' + entry['sdk_gitlink']], text=True).strip()
    if expected != entry['sdk_commit']:
        raise ValueError(f"Archive does not match official SDK gitlink: {entry['path']}")
    target = (sdk / entry['path']).resolve()
    if not target.is_relative_to(sdk.resolve()) or target == sdk.resolve():
        raise ValueError('Archive destination must stay inside the SDK')
    if not entry['url'].startswith('https://'):
        raise ValueError('Source snapshots require HTTPS')
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / (entry['commit'] + '.tar.gz')
    if not archive.exists():
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
                temporary = Path(output.name)
                with urllib.request.urlopen(entry['url'], timeout=120) as response:
                    shutil.copyfileobj(response, output)
            temporary.replace(archive)
        except BaseException:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
            raise
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != entry['sha256']:
        raise ValueError(f"Source archive SHA-256 mismatch: {entry['path']}")
    with tempfile.TemporaryDirectory(dir=sdk.parent) as directory, tarfile.open(archive) as tar:
        if tar.pax_headers.get('comment') != entry['commit']:
            raise ValueError('Git archive commit ID does not match the pinned snapshot')
        tar.extractall(directory, filter='data')
        roots = list(Path(directory).iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise ValueError('Expected one source directory in the snapshot')
        if target.exists():
            shutil.rmtree(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(roots[0]), target)
    print(f"Verified upstream archive: {entry['path']} @ {entry['commit']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    lock = json.loads((ROOT / 'packaging/dependencies.json').read_text())['linphone_sdk']
    actual = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != lock['commit']:
        raise ValueError('SDK checkout is not the pinned official revision')
    for entry in lock.get('source_archives', []):
        restore(sdk, entry, sdk.parent.parent / 'archives')


if __name__ == '__main__':
    main()
