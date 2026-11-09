#!/usr/bin/env python3
"""Apply documented Windows compatibility patches to the pinned SDK."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sdk', type=Path)
args = parser.parse_args()
sdk = args.sdk.resolve()
lock = json.loads((ROOT / 'packaging/dependencies.json').read_text())['linphone_sdk']
actual = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
if actual != lock['commit']:
    raise SystemExit('Refusing to patch an unexpected SDK version')
for module, name in (
    ('external/mbedtls', 'mbedtls-msvc-psa-initializers.patch'),
    ('external/libyuv', 'libyuv-install-import-library.patch'),
):
    source = sdk / module
    expected = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD:' + module], text=True).strip()
    if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != expected:
        raise SystemExit('Refusing to patch an unexpected revision: ' + module)
    patch = ROOT / 'packaging/patches' / name
    reverse = subprocess.run(['git', '-C', str(source), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
    if reverse.returncode:
        subprocess.run(['git', '-C', str(source), 'apply', '--check', str(patch)], check=True)
        subprocess.run(['git', '-C', str(source), 'apply', str(patch)], check=True)
    print('SDK compatibility patch verified/applied: ' + name)
