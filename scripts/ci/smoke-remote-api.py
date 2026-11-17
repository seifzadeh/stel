#!/usr/bin/env python3
"""Verify HTTPS remote control in the standalone binary without developer paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import signal
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
env = os.environ.copy()
for name in list(env):
    if name.startswith(('STEL_', 'QT_')) or name in ('LD_LIBRARY_PATH', 'QTDIR', 'CMAKE_PREFIX_PATH'):
        env.pop(name, None)
if os.name == 'nt':
    system_root = os.environ['SystemRoot']; env['PATH'] = os.pathsep.join([str(Path(system_root) / 'System32'), system_root])
else:
    env['PATH'] = '/usr/bin:/bin'
env['QT_QPA_PLATFORM'] = 'windows' if os.name == 'nt' else 'offscreen'
env['QT_QPA_PLATFORMTHEME'] = ''
with tempfile.TemporaryDirectory(prefix='Stel API تست ') as directory:
    root = Path(directory); config = root / 'config'; config.mkdir()
    binary = root / ('Stel.exe' if os.name == 'nt' else 'Stel.run'); shutil.copy2(args.binary, binary)
    # Generate fixture keys before cleaning the child runtime environment. Using
    # relative names also works with OpenSSL tools on Unicode Windows paths.
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=localhost', '-addext', 'subjectAltName=IP:127.0.0.1,DNS:localhost',
        '-keyout', 'server.key', '-out', 'server.pem'], cwd=root, check=True, capture_output=True)
    token = secrets.token_hex(32)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
    settings = {'schema': 1, 'accounts': [], 'preferences': {'signallingAddress': '127.0.0.1',
        'sipTcpPort': 0, 'sipTlsPort': 0, 'remoteControl': {'enabled': True, 'address': '127.0.0.1',
        'port': port, 'allowedPeers': ['127.0.0.1/32'], 'tokenHash': hashlib.sha256(token.encode()).hexdigest(),
        'certificate': str(root / 'server.pem'), 'privateKey': str(root / 'server.key')}}}
    (config / 'settings.json').write_text(json.dumps(settings), encoding='utf-8')
    context = ssl.create_default_context(cadata=(root / 'server.pem').read_text())
    base = f'https://127.0.0.1:{port}'

    def api(path, body=None, supplied_token=token):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(base + path, data=data,
            headers={'Authorization': 'Bearer ' + supplied_token, 'Content-Type': 'application/json'})
        return urllib.request.urlopen(request, context=context, timeout=3)

    with (root / 'application.log').open('wb') as log:
        process = subprocess.Popen([str(binary), '--config-dir', str(config)], env=env,
            stdin=subprocess.DEVNULL, stdout=log, stderr=log,
            **({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}))
        try:
            deadline = time.monotonic() + 40; status = None
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    with api('/api/v1/status') as response: status = json.load(response)
                    break
                except (OSError, urllib.error.URLError): time.sleep(0.2)
            if status is None:
                raise RuntimeError('Packaged HTTPS API did not start: ' + (root / 'application.log').read_text(errors='replace')[-4000:])
            assert status['apiVersion'] == 1 and status['calls'] == [] and len(status['blf']) == 12
            try:
                api('/api/v1/status', supplied_token='invalid')
                raise AssertionError('Invalid credentials were accepted')
            except urllib.error.HTTPError as error: assert error.code == 401
            with api('/api/v1/dnd', {'enabled': True}) as response: assert json.load(response)['ok']
            with api('/api/v1/status') as response: assert json.load(response)['dnd']
            with api('/servlet?key=DNDOff') as response: assert json.load(response)['ok']
            with api('/screencapture') as response:
                assert response.headers['Content-Type'] == 'image/jpeg' and response.read(2) == b'\xff\xd8'
            print('PASS packaged Remote API: HTTPS certificate verification, authentication, status, DND, Yealink command and screenshot')
        finally:
            if os.name == 'nt':
                subprocess.run([str(Path(os.environ['SystemRoot']) / 'System32/taskkill.exe'),
                    '/PID', str(process.pid), '/T', '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                try: os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError: pass
            try: process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                if os.name != 'nt': os.killpg(process.pid, signal.SIGKILL)
                else: process.kill()
                process.wait(timeout=8)
