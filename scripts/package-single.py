#!/usr/bin/env python3
"""Append a self-contained application folder to the native portable launcher."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import struct
import tempfile
import zlib

@contextmanager
def atomic_output(path):
    descriptor, name = tempfile.mkstemp(prefix='.stel-package-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as output:
            yield output
        os.chmod(name, 0o755)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--folder', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    launcher = args.launcher.read_bytes()
    windows = launcher[:2] == b'MZ'
    if not windows and launcher[:4] != b'\x7fELF':
        raise SystemExit('Launcher must be a native ELF or PE binary.')
    root = args.folder.resolve()
    if not (root / ('stel.exe' if windows else 'stel')).is_file():
        raise SystemExit('Application entry binary is missing.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    compressor = None if windows else zlib.compressobj(6)
    with atomic_output(args.output) as output:
        output.write(launcher)
        offset = output.tell()
        def write(data):
            output.write(compressor.compress(data) if compressor else data)
        for path in sorted(root.rglob('*')):
            if not path.is_file():
                continue
            name = path.relative_to(root).as_posix()
            encoded = name.encode('utf-8')
            size = path.stat().st_size
            write(struct.pack('<IIQ', len(encoded), path.stat().st_mode & 0o777, size))
            write(encoded)
            with path.open('rb') as source:
                while data := source.read(1024 * 1024):
                    write(data)
        write(bytes(16))
        if compressor:
            output.write(compressor.flush())
        length = output.tell() - offset
        output.write(struct.pack('<16sQQQQ', b'STEL_PORTABLE_01', offset, length, 0 if windows else 1, 0))
    print(f'Created native single-file application: {args.output} ({args.output.stat().st_size / 1024**2:.1f} MiB)')

if __name__ == '__main__':
    main()
