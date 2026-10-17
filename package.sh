#!/bin/sh
set -eu
cd "$(dirname "$0")"
if ! command -v lha >/dev/null 2>&1; then
    echo "LHA packaging requires an archive-capable lha command in PATH." >&2
    exit 1
fi
./build-amiga.sh all variants
python3 tools/package.py
python3 tools/make_adf.py
