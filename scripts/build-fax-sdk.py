#!/usr/bin/env python3
"""Build Linphone 5.3.105 with the native external T.38 media extension on Linux."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fax_sources',ROOT/'scripts/fax-sources.py')
sources=importlib.util.module_from_spec(spec);spec.loader.exec_module(sources)

def patch(source):
    for name, marker in (('linphone-native-fax.patch','.stel-fax-applied.patch'),
                         ('linphone-shared-media-resume.patch','.stel-resume-applied.patch')):
        file=ROOT/'packaging/patches'/name
        previous=source/marker
        if subprocess.run(['git','-C',str(source),'apply','--reverse','--check',str(file)],capture_output=True).returncode:
            if previous.exists():
                subprocess.run(['git','-C',str(source),'apply','--reverse','--check',str(previous)],check=True)
                subprocess.run(['git','-C',str(source),'apply','--reverse',str(previous)],check=True)
            subprocess.run(['git','-C',str(source),'apply','--check',str(file)],check=True)
            subprocess.run(['git','-C',str(source),'apply',str(file)],check=True)
        shutil.copy2(file,previous)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,default=ROOT/'.deps/fax-build')
    parser.add_argument('--output',type=Path,default=ROOT/'.deps/fax-sdk')
    parser.add_argument('--patch-only',type=Path,help='Patch the liblinphone directory of an existing pinned SDK')
    args=parser.parse_args()
    if args.patch_only:
        patch(args.patch_only.resolve());raise SystemExit(0)
    work=args.work.resolve();output=args.output.resolve()
    source,archive=sources.fetch('linphone',work/'src');patch(source)
    options={'CMAKE_BUILD_TYPE':'Release','CMAKE_INSTALL_PREFIX':str(output),'BUILD_SHARED_LIBS':'ON',
        'ENABLE_DB_STORAGE':'OFF','ENABLE_LDAP':'OFF','ENABLE_LIME_X3DH':'OFF','ENABLE_ADVANCED_IM':'OFF',
        'ENABLE_UNIT_TESTS':'OFF','ENABLE_STRICT':'OFF','ENABLE_DOC':'OFF','ENABLE_CXX_WRAPPER':'OFF',
        'ENABLE_FLEXIAPI':'OFF','ENABLE_QRCODE':'OFF','ENABLE_TUTORIALS':'OFF','ENABLE_DAEMON':'OFF',
        'ENABLE_TOOLS':'OFF','ENABLE_JAVA_WRAPPER':'OFF','ENABLE_CSHARP_WRAPPER':'OFF'}
    subprocess.run(['cmake','-S',str(source),'-B',str(work/'build'),'-G','Ninja']+['-D'+k+'='+v for k,v in options.items()],check=True)
    subprocess.run(['cmake','--build',str(work/'build'),'--target','install','--parallel','4'],check=True)
    notices=output/'share/licenses/stel-fax-sdk';notices.mkdir(parents=True,exist_ok=True)
    shutil.copy2(archive,notices/'liblinphone-5.3.105-source.tar.gz')
    for name in ('linphone-native-fax.patch','linphone-shared-media-resume.patch'):
        shutil.copy2(ROOT/'packaging/patches'/name,notices)
    shutil.copy2(source/'LICENSE.txt',notices) if (source/'LICENSE.txt').exists() else None
    (notices/'BUILD.json').write_text(json.dumps(options,indent=2)+'\n')
    print('Fax SDK installed: '+str(output))
