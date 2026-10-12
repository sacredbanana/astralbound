#!/bin/sh
set -eu
cd "$(dirname "$0")"
./build-amiga.sh
python3 tools/package.py
