#!/usr/bin/env bash
set -euo pipefail
STEL_PROJECT_DIR="$(cd -- "$(dirname -- "$0")/.." && pwd)"
exec python3 "$STEL_PROJECT_DIR/scripts/release.py" source
