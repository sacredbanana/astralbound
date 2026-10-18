#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Fetch, build and run Cranker, the Amiga executable cruncher.

Cranker's cruncher is GPLv2, so it is built into build/ rather than vendored.
Only its public-domain decrunch header ends up inside crunched executables.
"""
from pathlib import Path
import hashlib, os, shutil, struct, subprocess, sys, urllib.request
root = Path(__file__).resolve().parents[1]
URL = 'https://aminet.net/util/pack/cranker.lha'
SHA256 = 'fea0d6f073c14642ff391bc6b8e8f9cd7c31c75c176378ddd3b1eb21c5c0aa22'  # 0.72
work = root/'build/cranker'

def cranker_path():
    """Return a Cranker binary: $CRANKER, a cached build, or a fresh build."""
    if os.environ.get('CRANKER'): return os.environ['CRANKER']
    binary = work/'cranker'
    if binary.exists(): return str(binary)
    lha = shutil.which('lha')
    if not lha: raise SystemExit('Building Cranker requires an lha command in PATH.')
    work.mkdir(parents=True, exist_ok=True)
    archive = work/'cranker.lha'
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        print(f'Fetching {URL}', file=sys.stderr)
        with urllib.request.urlopen(URL, timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise SystemExit(f'{URL} does not match the pinned Cranker 0.72 archive; '
                         'set CRANKER to a cranker binary instead.')
    source = work/'src'
    shutil.rmtree(source, ignore_errors=True)
    source.mkdir()
    subprocess.run([lha, 'xq', str(archive)], cwd=source, check=True)
    subprocess.run(['make', '-s', 'BUILDDIR=build/host'], cwd=source/'cranker/src', check=True)
    shutil.copy2(source/'cranker/src/build/host/cranker', binary)
    return str(binary)

def hunks(path):
    """Loadable hunks of an executable as (memory flags, size, data, relocs)."""
    data = Path(path).read_bytes()
    pos = 0
    def long():
        nonlocal pos
        pos += 4
        return struct.unpack('>I', data[pos-4:pos])[0]
    assert long() == 0x3f3, path
    while long(): pass
    long()
    first, last = long(), long()
    sizes = [long() for _ in range(last-first+1)]
    out, current = [], None
    while pos < len(data):
        kind = long() & 0x3fffffff
        if kind in (0x3e9, 0x3ea, 0x3eb):
            size = long()*4
            current = [b'' if kind == 0x3eb else data[pos:pos+size], {}]
            if kind != 0x3eb: pos += size
        elif kind == 0x3ec:
            while (count := long()):
                target = long()
                current[1].setdefault(target, set()).update(long() for _ in range(count))
        elif kind == 0x3f0:
            while (count := long()): pos += 4*count+4
        elif kind == 0x3f1:
            size = long()*4
            pos += size
        elif kind == 0x3f2:
            # GCC ends with a debug-only hunk, which Cranker strips.
            if current: out.append(current)
            current = None
        else: raise ValueError(f'{path}: unsupported hunk {kind:#x}')
    # Data equality ignores trailing zero padding: code and data hunks may be
    # stored shorter than their allocation, with the rest cleared like BSS.
    return [(size & 0xc0000000, (size & 0x3fffffff)*4, data.rstrip(b'\0'), relocs)
            for size, (data, relocs) in zip(sizes, out)]

def crunch(source, output):
    """Crunch an executable and check it decrunches to the same hunks."""
    cranker = cranker_path()
    subprocess.run([cranker, '-q', '-f', str(source), '-o', str(output)], check=True)
    unpacked = Path(output).with_name(Path(output).name+'.verify')
    subprocess.run([cranker, '-q', '-u', '-f', str(output), '-o', str(unpacked)], check=True)
    if hunks(unpacked) != hunks(source):
        raise SystemExit(f'{output} does not decrunch to the hunks of {source}.')
    unpacked.unlink()

if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit('usage: cranker.py SOURCE OUTPUT')
    crunch(sys.argv[1], sys.argv[2])
