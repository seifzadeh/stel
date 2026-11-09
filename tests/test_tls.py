#!/usr/bin/env python3
"""Real liblinphone REGISTER over TLS against an ephemeral local registrar.

The registrar requires client certificates and verifies SIP digest, without a PBX
or test keys committed to the repository. Negative cases must never REGISTER.
"""
import hashlib
import json
import os
import pathlib
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import uuid
import wave
import math
import struct


def openssl(directory, *args):
    subprocess.run(['openssl', *args], cwd=directory, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def certificates(d):
    for ca in ('ca', 'other'):
        openssl(d, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
                '-subj', f'/CN=Stel test {ca}', '-keyout', f'{ca}.key', '-out', f'{ca}.pem')
    for name, dns, usage in [('server', 'localhost', 'serverAuth'),
                             ('wrong', 'wrong.invalid', 'serverAuth'),
                             ('client', 'sip:alice@localhost', 'clientAuth'),
                             ('bob', 'sip:bob@localhost', 'clientAuth')]:
        openssl(d, 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', f'/CN={dns}',
                '-keyout', f'{name}.key', '-out', f'{name}.csr')
        (d / f'{name}.ext').write_text(f'subjectAltName=DNS:{dns}\nextendedKeyUsage={usage}\n')
        openssl(d, 'x509', '-req', '-in', f'{name}.csr', '-CA', 'ca.pem', '-CAkey',
                'ca.key', '-CAcreateserial', '-days', '2', '-extfile', f'{name}.ext', '-out', f'{name}.pem')


class Registrar:
    def __init__(self, directory, cert='server'):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(directory / f'{cert}.pem', directory / f'{cert}.key')
        self.context.load_verify_locations(directory / 'ca.pem')
        self.context.verify_mode = ssl.CERT_REQUIRED
        self.socket = socket.socket()
        self.socket.bind(('127.0.0.1', 0))
        self.socket.listen(5)
        self.socket.settimeout(0.2)
        self.port = self.socket.getsockname()[1]
        self.stop = threading.Event()
        self.registered = False
        self.identities = set()
        self.identity_mismatch = False
        self.workers = []
        self.bindings = {}
        self.transactions = {}
        self.send_locks = {}
        self.forwarded_invites = 0
        self.client_verified = False
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.socket.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            worker = threading.Thread(target=self.handle, args=(connection,), daemon=True)
            self.workers.append(worker)
            worker.start()

    def handle(self, connection):
        try:
            try:
                connection.settimeout(1)
                with self.context.wrap_socket(connection, server_side=True) as conn:
                    self.client_verified = bool(conn.getpeercert())
                    self.send_locks[conn] = threading.Lock()
                    peer_cn = dict(x for part in conn.getpeercert().get("subject", ()) for x in part).get("commonName", "")
                    buffer = b''
                    while not self.stop.is_set():
                        try:
                            data = conn.recv(65536)
                        except socket.timeout:
                            continue
                        if not data:
                            break
                        buffer += data
                        while b'\r\n\r\n' in buffer:
                            head = buffer.split(b'\r\n\r\n', 1)[0]
                            size_match = re.search(rb'(?im)^Content-Length:\s*(\d+)', head)
                            size = len(head) + 4 + (int(size_match[1]) if size_match else 0)
                            if len(buffer) < size:
                                break
                            packet, buffer = buffer[:size], buffer[size:]
                            lines = head.decode().split('\r\n')
                            if not lines[0]:
                                continue
                            headers = {}
                            for line in lines[1:]:
                                k, _, v = line.partition(':')
                                headers[k.lower()] = v.strip()
                            if not lines[0].startswith('REGISTER '):
                                self.forward(packet, conn, headers)
                                continue
                            auth = headers.get('authorization', '')
                            identity = re.search(r'sip:([^@;>]+)@', headers.get('from', ''))
                            username = identity[1] if identity else ''
                            matches = peer_cn == f'sip:{username}@localhost'
                            self.identity_mismatch |= not matches
                            accepted = self.digest_valid(auth) and matches
                            code = '200 OK' if accepted else '401 Unauthorized'
                            out = [f'SIP/2.0 {code}']
                            for name in ('Via', 'From', 'To', 'Call-ID', 'CSeq'):
                                value = headers.get(name.lower(), '')
                                if name == 'To' and ';tag=' not in value:
                                    value += ';tag=stel-test'
                                out.append(f'{name}: {value}')
                            if accepted:
                                self.registered = True
                                self.identities.add(username)
                                self.bindings[username] = conn
                                out.append('Contact: ' + headers['contact'])
                                out.append('Expires: 600')
                            else:
                                out.append('WWW-Authenticate: Digest realm="stel-test", nonce="stel-local-nonce", algorithm=MD5, qop="auth"')
                            out += ['Content-Length: 0', '', '']
                            self.send(conn, '\r\n'.join(out).encode())
            except (ssl.SSLError, OSError):
                connection.close()
        except Exception as exc:
            self.error = repr(exc)

    def send(self, conn, packet):
        with self.send_locks[conn]:
            conn.sendall(packet)

    def forward(self, packet, conn, headers):
        head, body = packet.split(b'\r\n\r\n', 1)
        lines = head.decode().split('\r\n')
        if lines[0].startswith('SIP/2.0 '):
            first_via = next((line for line in lines[1:] if line.lower().startswith('via:')), '')
            match = re.search(r'branch=([^;\s]+)', first_via)
            if not match or match[1] not in self.transactions:
                return
            lines.remove(first_via)
            destination = self.transactions[match[1]]
        else:
            user = re.search(r'sip:([^@;>]+)@', headers.get('to', ''))
            if not user or user[1] not in self.bindings:
                return
            destination = self.bindings[user[1]]
            branch = 'z9hG4bK-stel-' + uuid.uuid4().hex
            self.transactions[branch] = conn
            lines = [line for line in lines if not line.lower().startswith('route:')]
            lines.insert(1, f'Via: SIP/2.0/TLS localhost:{self.port};branch={branch}')
            if lines[0].startswith('INVITE '):
                self.forwarded_invites += 1
                lines.insert(2, f'Record-Route: <sip:localhost:{self.port};transport=tls;lr>')
        self.send(destination, '\r\n'.join(lines).encode() + b'\r\n\r\n' + body)

    @staticmethod
    def digest_valid(auth):
        if not auth.startswith('Digest '):
            return False
        fields = dict((key, quoted or token) for key, quoted, token in
                      re.findall(r'(\w+)=(?:"([^"]*)"|([^,\s]+))', auth))
        if fields.get('username') not in ('alice', 'bob'):
            return False
        md5 = lambda s: hashlib.md5(s.encode()).hexdigest()
        ha1 = md5(fields['username'] + ':stel-test:local-test-secret')
        ha2 = md5('REGISTER:' + fields.get('uri', ''))
        expected = md5(':'.join([ha1, 'stel-local-nonce', fields.get('nc', ''),
                                fields.get('cnonce', ''), 'auth', ha2]))
        return fields.get('response') == expected

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.socket.close()
        for worker in self.workers:
            worker.join(2)


def run_case(binary, directory, name, *, cert='server', ca='ca', client=True,
             password='local-test-secret', expect=False, multiple=False, call=False, codec=''):
    registrar = Registrar(directory, cert)
    try:
        account = {'id': 'test', 'username': 'alice', 'domain': 'localhost',
                   'server': f'sip:localhost:{registrar.port}', 'transport': 'tls',
                   'encryption': 'srtp' if call else 'none', 'ice': False, 'register': True}
        if client:
            account.update(certificate=str(directory / 'client.pem'),
                           privateKey=str(directory / 'client.key'))
        config = directory / f'{name}.json'
        accounts = [account]
        if multiple:
            accounts.append({**account, 'id': 'bob', 'username': 'bob',
                             'certificate': str(directory / 'bob.pem'), 'privateKey': str(directory / 'bob.key')})
        config.write_text(json.dumps({'accounts': accounts, 'call': call,
                                      'audioCodecId': codec, 'requireCodec': 'G729/8000' if codec else '',
                                      'preferences': {'caFile': str(directory / f'{ca}.pem')}}))
        tone = directory / 'tone.wav'
        if call:
            with wave.open(str(tone), 'wb') as wav:
                wav.setparams((1, 2, 8000, 0, 'NONE', 'not compressed'))
                wav.writeframes(b''.join(struct.pack('<h', int(math.sin(i*2*math.pi*440/8000)*5000)) for i in range(8000*15)))
        result = subprocess.run([binary, str(config)], capture_output=True, text=True,
                                env={**os.environ, 'STEL_TEST_PASSWORD': password, 'STEL_TEST_WAV_FILE': str(tone) if call else ''}, timeout=16)
        if registrar.error:
            raise AssertionError(registrar.error)
        if expect:
            assert result.returncode == 0 and registrar.registered and registrar.client_verified and not registrar.identity_mismatch, result.stdout + result.stderr[-5000:]
            if multiple:
                assert registrar.identities == {'alice', 'bob'}, registrar.identities
            if call:
                assert registrar.forwarded_invites >= 1 and 'CALL_OK' in result.stdout
        else:
            assert result.returncode != 0 and not registrar.registered, 'An invalid peer/credential was accepted!'
        print(f'PASS {name}: exit={result.returncode}, client_certificate_verified={registrar.client_verified}, registered={registrar.registered}', flush=True)
    finally:
        registrar.close()


if __name__ == '__main__':
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='stel-mtls-') as temp:
        directory = pathlib.Path(temp)
        certificates(directory)
        run_case(binary, directory, 'mutual_tls_and_digest', expect=True)
        run_case(binary, directory, 'two_accounts_distinct_certificates', multiple=True, expect=True)
        run_case(binary, directory, 'mutual_tls_srtp_call', multiple=True, call=True, expect=True)
        inventory = subprocess.run([binary, '--codecs'], capture_output=True, text=True, timeout=10).stdout
        if 'AUDIO G729/8000/1' in inventory:
            run_case(binary, directory, 'g729_policy_on_two_isolated_mtls_cores', multiple=True,
                     call=True, codec='g729/8000/1;annexb=yes', expect=True)
        run_case(binary, directory, 'reject_untrusted_server', ca='other')
        run_case(binary, directory, 'reject_wrong_hostname', cert='wrong')
        run_case(binary, directory, 'reject_missing_client_certificate', client=False)
        run_case(binary, directory, 'reject_bad_digest', password='incorrect')
