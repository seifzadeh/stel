#!/usr/bin/env python3
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
for p in root.rglob('*'):
    if p.is_symlink() and not p.exists():
        target = os.readlink(p)
        if target.startswith('/'):
            candidate = pathlib.Path(target)
        else:
            candidate = pathlib.Path('/') / p.parent.relative_to(root) / target
        if candidate.exists():
            if not target.startswith('/'):
                local_target = p.parent / target
                local_target.parent.mkdir(parents=True, exist_ok=True)
                local_target.symlink_to(candidate)
            else:
                p.unlink()
                p.symlink_to(candidate)

# Qt's CMake plugin targets refer to runtime plugin files even when the host
# already had those packages (and apt therefore did not download them).
plugins = pathlib.Path('/usr/lib/x86_64-linux-gnu/qt5/plugins')
if plugins.exists():
    for host in plugins.rglob('*'):
        if host.is_file():
            dest = root / host.relative_to('/')
            if not dest.exists() and not dest.is_symlink():
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.symlink_to(host)
