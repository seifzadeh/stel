#!/usr/bin/env python3
"""Apply the documented MSVC PSA header compatibility patch to the pinned SDK."""
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
mbed = sdk / 'external/mbedtls'
expected = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD:external/mbedtls'], text=True).strip()
if subprocess.check_output(['git', '-C', str(mbed), 'rev-parse', 'HEAD'], text=True).strip() != expected:
    raise SystemExit('Refusing to patch an unexpected Mbed TLS revision')
patch = ROOT / 'packaging/patches/mbedtls-msvc-psa-initializers.patch'
reverse = subprocess.run(['git', '-C', str(mbed), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
if reverse.returncode:
    subprocess.run(['git', '-C', str(mbed), 'apply', '--check', str(patch)], check=True)
    subprocess.run(['git', '-C', str(mbed), 'apply', str(patch)], check=True)
print('MSVC PSA initializer compatibility patch verified/applied')
