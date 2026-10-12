#!/bin/sh
set -eu
cd "$(dirname "$0")"
./build-amiga.sh all variants
python3 tools/package.py
python3 tools/make_adf.py
