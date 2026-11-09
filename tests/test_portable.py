#!/usr/bin/env python3
"""Exercise the real Linux release artifacts without development search paths."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

project = Path(__file__).resolve().parents[1]
single = Path(sys.argv[1]).resolve()
folder = Path(sys.argv[2]).resolve()
expected_version = json.loads((folder / 'bundle-manifest.json').read_text())['version']
logs = project / 'build/qa'
results = {}
prefix = sys.argv[3] if len(sys.argv) > 3 else 'lifecycle-portable'
assert prefix.replace('-', '').isalnum() and prefix.isascii()
clean = {k: v for k, v in os.environ.items() if not k.startswith('STEL_') and k not in
         ('LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'CMAKE_PREFIX_PATH', 'QTDIR')}
clean.update(PATH='/usr/bin:/bin', QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='')

def check(name, command, *, env=clean, cwd=None, expected=0, timeout=240):
    start = time.monotonic()
    completed = subprocess.run([str(x) for x in command], env=env, cwd=cwd,
                               capture_output=True, text=True, timeout=timeout)
    (logs / f'{prefix}-{name}.log').write_text(completed.stdout + completed.stderr)
    results[name] = {'exit_code': completed.returncode, 'seconds': round(time.monotonic() - start, 2)}
    assert completed.returncode == expected, f'{name}: {completed.returncode}\n{completed.stderr[-2000:]}'
    print(name, results[name], flush=True)
    return completed

try:
    with tempfile.TemporaryDirectory(prefix='stel release ') as work_text:
        work = Path(work_text)
        alone = work / 'one file only فارسی'
        alone.mkdir()
        binary = alone / 'Stel.run'
        shutil.copy2(single, binary)
        temporary = work / 'runtime files'
        temporary.mkdir(mode=0o700)
        env = {**clean, 'TMPDIR': str(temporary)}
        completed = check('single_version', [binary, '--version'], env=env, cwd=alone)
        assert expected_version in completed.stdout
        assert list(alone.iterdir()) == [binary]
        check('single_settings', [binary, '--config-dir', work / 'new settings فارسی', '--settings', '--smoke-test'], env=env, cwd=alone)
        assert not list(temporary.glob('stel-*')), 'Temporary extraction was not removed after exit'

        config = work / 'single instance'
        with (logs / f'{prefix}-instance.log').open('w') as log:
            first = subprocess.Popen([str(binary), '--config-dir', str(config)], env=env, cwd=alone,
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 15
                while not (config / 'instance.lock').exists() and time.monotonic() < deadline:
                    assert first.poll() is None, 'First application exited during startup'
                    time.sleep(.05)
                child = int((config / 'instance.lock').read_text().splitlines()[0])
                check('single_instance', [binary, '--config-dir', config, '--smoke-test'], env=env, cwd=alone)
                assert first.poll() is None, 'Second launch terminated the first application'
                assert int((config / 'instance.lock').read_text().splitlines()[0]) == child
            finally:
                if first.poll() is None:
                    first.terminate()
                first.wait(timeout=15)
            assert not Path(f'/proc/{child}').exists(), 'Orphan application process'
        # A force-killed Qt child can leave its local IPC socket until the next
        # launch. Its extraction directories and processes must already be gone.
        leftovers = list(temporary.glob('stel-*'))
        assert not any(p.is_dir() for p in leftovers), 'Temporary extraction remained after termination'
        check('stale_lock_restart', [binary, '--config-dir', config, '--smoke-test'], env=env, cwd=alone)
        assert not (config / 'instance.lock').exists()
        assert not list(temporary.glob('stel-*'))

        corrupt = work / 'corrupt.run'
        shutil.copy2(binary, corrupt)
        with corrupt.open('r+b') as file:
            file.seek(-49, os.SEEK_END)
            value = file.read(1)
            file.seek(-49, os.SEEK_END)
            file.write(bytes([value[0] ^ 1]))
        check('corrupt_payload', [corrupt, '--version'], env=env, cwd=alone, expected=1)
        assert not list(temporary.glob('stel-*'))

        relocated = work / 'folder release فارسی'
        shutil.move(str(folder), str(relocated))
        try:
            check('folder_native', [relocated / 'stel', '--config-dir', work / 'folder settings', '--settings', '--smoke-test'], cwd=alone)
            runtime = {**clean, 'LD_LIBRARY_PATH': str(relocated / 'lib'),
                       'QT_PLUGIN_PATH': str(relocated / 'plugins'),
                       'QT_QPA_PLATFORM_PLUGIN_PATH': str(relocated / 'plugins/platforms'),
                       'STEL_RESOURCE_PREFIX': str(relocated),
                       'STEL_CA_BUNDLE': str(relocated / 'share/certs/ca-certificates.crt'),
                       'ALSA_CONFIG_DIR': str(relocated / 'share/alsa'),
                       'ALSA_PLUGIN_DIR': str(relocated / 'lib/alsa-lib')}
            check('core', [relocated / 'bin/stel_tests'], env=runtime)
            check('settings_ui', [relocated / 'bin/stel_ui_tests'], env=runtime)
            check('tls', ['/usr/bin/python3', project / 'tests/test_tls.py', relocated / 'bin/stel_probe'], env=runtime)
        finally:
            shutil.move(str(relocated), str(folder))
finally:
    (logs / f'{prefix}-results.json').write_text(json.dumps(results, indent=2) + '\n')
