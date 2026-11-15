#!/usr/bin/env python3
"""Fetch checksum-pinned fax dependency sources; never modify the application's Git state."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def download(url, archive, expected):
    if archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest() == expected:
        return
    temporary = archive.with_suffix('.download')
    with urllib.request.urlopen(url, timeout=120) as source, temporary.open('wb') as out:
        shutil.copyfileobj(source, out)
    if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
        temporary.unlink()
        raise RuntimeError('Dependency source checksum mismatch: ' + archive.name)
    temporary.replace(archive)

def distribution_patches(name, source, entry, work):
    patches = entry.get('patches')
    if not patches:
        return
    archive = work / (name + '-debian.tar.xz')
    download(patches['url'], archive, patches['sha256'])
    stamp = source / '.stel-distribution-patches'
    if stamp.exists() and stamp.read_text().strip() == patches['sha256']:
        return
    directory = work / (name + '-patches');directory.mkdir(exist_ok=True)
    with tarfile.open(archive) as tar:
        series = tar.extractfile('debian/patches/series').read().decode()
        for line in series.splitlines():
            line = line.split('#',1)[0].strip()
            if not line:
                continue
            filename = line.split()[0]
            if Path(filename).is_absolute() or '..' in Path(filename).parts:
                raise RuntimeError('Invalid distribution patch path')
            patch = directory / Path(filename).name
            patch.write_bytes(tar.extractfile('debian/patches/' + filename).read())
            command = ['git','-C',str(source),'apply']
            if subprocess.run(command+['--reverse','--check',str(patch.resolve())],capture_output=True).returncode:
                subprocess.run(command+['--check',str(patch.resolve())],check=True)
                subprocess.run(command+[str(patch.resolve())],check=True)
    stamp.write_text(patches['sha256']+'\n')

def fetch(name, work):
    entry = json.loads((ROOT / 'packaging/dependencies.json').read_text())['fax'][name]
    work.mkdir(parents=True, exist_ok=True)
    archive = work / (name + '-source.tar')
    download(entry['url'], archive, entry['sha256'])
    source = work / entry['directory']
    if not source.is_dir():
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                path = Path(member.name)
                if path.is_absolute() or '..' in path.parts:
                    raise RuntimeError('Unexpected source archive member: ' + member.name)
            # Materialize internal links as copies. Windows builders should
            # not need Developer Mode or symlink privileges to unpack sources.
            links = [m for m in tar.getmembers() if m.issym() or m.islnk()]
            tar.extractall(work, members=[m for m in tar.getmembers()
                                         if not m.issym() and not m.islnk()], filter='data')
            root = work.resolve()
            for member in links:
                destination = work / member.name
                target = ((destination.parent if member.issym() else work) / member.linkname).resolve()
                if not target.is_relative_to(root) or not target.exists():
                    raise RuntimeError('Invalid source archive link: ' + member.name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if target.is_dir():
                    shutil.copytree(target, destination)
                else:
                    shutil.copy2(target, destination)
        if not source.is_dir():
            raise RuntimeError('Source archive root does not match the dependency lock: ' + name)
    distribution_patches(name, source, entry, work)
    print('Verified fax dependency: ' + name, flush=True)
    return source, archive

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('dependencies', nargs='+', choices=('linphone','spandsp','libtiff','mupdf'))
    args = parser.parse_args()
    for name in args.dependencies:
        fetch(name, args.work)
