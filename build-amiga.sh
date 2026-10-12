#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "${CLEAN:-0}" = 1 ]; then make -f Makefile.Amiga clean; fi
if command -v m68k-amigaos-gcc >/dev/null 2>&1; then
    make -f Makefile.Amiga "$@"
elif [ -x /opt/amiga/bin/m68k-amigaos-gcc ]; then
    make -f Makefile.Amiga AMIGA_CC=/opt/amiga/bin/m68k-amigaos-gcc "$@"
else
    docker run --rm -v "$PWD:/work" -w /work \
        -e USER="$(id -u)" -e GROUP="$(id -g)" \
        sacredbanana/amiga-compiler:m68k-amigaos make -f Makefile.Amiga "$@"
fi
