#!/usr/bin/env python3
"""Build the checksum-pinned native PDF renderer; no external viewer at runtime."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fax_sources',ROOT/'scripts/fax-sources.py')
sources=importlib.util.module_from_spec(spec);spec.loader.exec_module(sources)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,default=ROOT/'.deps/pdf-build')
    parser.add_argument('--output',type=Path,default=ROOT/'.deps/pdf-sdk')
    parser.add_argument('--download-only',action='store_true')
    args=parser.parse_args();work=args.work.resolve();output=args.output.resolve()
    source,archive=sources.fetch('mupdf',work/'src')
    if args.download_only:raise SystemExit(0)
    if os.name=='nt':
        subprocess.run(['msbuild',str(source/'platform/win32/mupdf.sln'),'/t:libmupdf','/m:4',
            '/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v142','/p:WindowsTargetPlatformVersion=10.0'],check=True)
        names=['libmupdf','libthirdparty','libresources','libharfbuzz','libpkcs7','libextract','libtesseract','libmubarcode']
        libraries=[]
        for name in names:
            candidates=[p for p in (source/'platform/win32').rglob(name+'.lib') if 'Release' in p.parts]
            candidates.sort(key=lambda p:'x64' not in p.parts)
            if not candidates:raise RuntimeError('Missing native PDF library: '+name)
            libraries.append(candidates[0])
    else:
        subprocess.run(['make','-j4','build=release','HAVE_X11=no','HAVE_GLUT=no',
            'HAVE_CURL=no','USE_TESSERACT=no','libs'],cwd=source,check=True)
        libraries=[source/'build/release/libmupdf.a',source/'build/release/libmupdf-third.a']
    (output/'lib').mkdir(parents=True,exist_ok=True)
    for library in libraries:shutil.copy2(library,output/'lib'/library.name)
    shutil.copytree(source/'include',output/'include',dirs_exist_ok=True)
    notices=output/'share/licenses/stel-pdf-engine';notices.mkdir(parents=True,exist_ok=True)
    for path in source.rglob('*'):
        if path.is_file() and path.name.upper().startswith(('COPYING','LICENSE','OFL')):
            dest=notices/path.relative_to(source);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest)
    entry=json.loads((ROOT/'packaging/dependencies.json').read_text())['fax']['mupdf']
    (notices/'source.json').write_text(json.dumps(entry,indent=2)+'\n')
    originals=output/'share/sources';originals.mkdir(parents=True,exist_ok=True)
    shutil.copy2(archive,originals/'mupdf-source.tar.gz')
    print('Native PDF renderer installed: '+str(output))
