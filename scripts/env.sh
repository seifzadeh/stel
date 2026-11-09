#!/usr/bin/env bash
# Source this file for the rootless Debian build environment.
STEL_PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
STEL_DEPS_PREFIX="$STEL_PROJECT_DIR/.deps/root/usr"
export PATH="$STEL_DEPS_PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$STEL_DEPS_PREFIX/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="$STEL_DEPS_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
