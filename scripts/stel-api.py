#!/usr/bin/env python3
"""Small standard-library client for Stel Remote Control. No app runtime dependency."""
import argparse
import base64
import json
import os
from pathlib import Path
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default=os.environ.get('STEL_API', 'http://127.0.0.1:8877'))
    parser.add_argument('--token-env', default='STEL_TOKEN', help='Environment variable containing the API token')
    parser.add_argument('--ca', help='Trusted PEM CA for HTTPS')
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('status', 'calls', 'blf', 'accounts', 'messages', 'faxes', 'events'):
        commands.add_parser(name)
    dial = commands.add_parser('dial'); dial.add_argument('target'); dial.add_argument('--account'); dial.add_argument('--video', action='store_true')
    for name in ('answer', 'reject', 'hangup', 'hold', 'resume', 'mute', 'unmute', 'record', 'stop-recording'):
        item = commands.add_parser(name); item.add_argument('call_id')
        if name == 'answer': item.add_argument('--video', action='store_true')
    for name in ('transfer', 'forward', 'dtmf', 'conference', 'attended-transfer'):
        item = commands.add_parser(name); item.add_argument('call_id'); item.add_argument('value')
    message = commands.add_parser('send-message'); message.add_argument('target'); message.add_argument('text'); message.add_argument('--account')
    fax = commands.add_parser('fax-send'); fax.add_argument('target'); fax.add_argument('--account'); fax.add_argument('--protocol', choices=['t38', 'g711'], default='t38')
    source = fax.add_mutually_exclusive_group(required=True); source.add_argument('--file', type=Path); source.add_argument('--text')
    receive = commands.add_parser('fax-receive'); receive.add_argument('call_id'); receive.add_argument('--protocol', choices=['t38', 'g711'], default='t38')
    action = commands.add_parser('yealink'); action.add_argument('key'); action.add_argument('--call-id'); action.add_argument('--outgoing-uri')
    args = parser.parse_args()
    token = os.environ.get(args.token_env)
    if not token: parser.error(f'Set {args.token_env} to the token generated in Settings → Remote API')
    context = ssl.create_default_context(cafile=args.ca) if args.url.startswith('https://') else None

    def api(path, body=None):
        data = None if body is None else json.dumps(body, ensure_ascii=False).encode('utf-8')
        request = urllib.request.Request(args.url.rstrip('/') + path, data=data,
            headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, context=context, timeout=20) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(error.read().decode('utf-8', errors='replace')) from None

    def account(body):
        if getattr(args, 'account', None): body['accountId'] = args.account
        return body

    def call_path(action): return '/api/v1/calls/' + urllib.parse.quote(args.call_id, safe='') + '/' + action
    command = args.command
    if command in ('status', 'calls', 'blf', 'accounts', 'messages', 'faxes', 'events'): result = api('/api/v1/' + command)
    elif command == 'dial': result = api('/api/v1/calls', account({'target': args.target, 'video': args.video}))
    elif command == 'send-message': result = api('/api/v1/messages', account({'target': args.target, 'text': args.text}))
    elif command == 'fax-send':
        if args.file:
            if args.file.stat().st_size > 10 * 1024 * 1024: parser.error('This helper accepts files up to 10 MiB')
            document = {'filename': args.file.name, 'base64': base64.b64encode(args.file.read_bytes()).decode('ascii')}
        else: document = {'text': args.text}
        prepared = api('/api/v1/documents', document)['document']; deadline = time.monotonic() + 120
        while prepared['state'] == 'converting' and time.monotonic() < deadline:
            time.sleep(0.5); prepared = api('/api/v1/documents/' + prepared['id'])['document']
        if prepared['state'] != 'ready': raise RuntimeError('Document not ready: ' + json.dumps(prepared, ensure_ascii=False))
        result = api('/api/v1/faxes', account({'target': args.target, 'documentId': prepared['id'], 'protocol': args.protocol}))
    elif command == 'fax-receive': result = api(call_path('fax'), {'protocol': args.protocol})
    elif command == 'yealink':
        query = {'key': args.key}
        if args.call_id: query['callId'] = args.call_id
        if args.outgoing_uri: query['outgoing_uri'] = args.outgoing_uri
        result = api('/servlet?' + urllib.parse.urlencode(query))
    else:
        body = {}
        if command == 'answer': body['video'] = args.video
        if command in ('transfer', 'forward'): body['target'] = args.value
        if command in ('conference', 'attended-transfer'): body['otherCallId'] = args.value
        if command == 'dtmf': body['digit'] = args.value
        result = api(call_path(command), body)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr); sys.exit(1)
