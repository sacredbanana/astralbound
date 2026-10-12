#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Build a self-booting OS3 disk from original files, without distributing ROMs."""
from pathlib import Path
import os, shutil, subprocess, tempfile, hashlib
root = Path(__file__).resolve().parents[1]
xdftool = os.environ.get('XDFTOOL') or shutil.which('xdftool')
if not xdftool:
    candidate = root/'build/release-venv/bin/xdftool'
    if candidate.exists(): xdftool = str(candidate)
if not xdftool:
    raise SystemExit('Install amitools in a venv and set XDFTOOL to its xdftool.')
vasm = shutil.which('vasmm68k_mot') or '/opt/amiga/bin/vasmm68k_mot'
boot = root/'build/space-boot.bin'
subprocess.run([vasm, '-m68000', '-Fbin', '-quiet', str(root/'release/boot.s'), '-o', str(boot)], check=True)
output = root/'dist/Space-0.1-boot.adf'
files = {
    'Space.000': root/'dist/amiga/Space.000',
    'Space.000.info': root/'release/Space.000.info',
    'Space.guide': root/'Space.guide',
    'Space.guide.info': root/'release/Space.guide.info',
    'Disk.info': root/'release/Disk.info',
    'LICENSE': root/'LICENSE',
    's/startup-sequence': root/'release/startup-sequence',
}
cmd = [xdftool, '-f', str(output), 'create', '+', 'format', 'Space', 'DOS0', '+', 'boot', 'write', str(boot), '+', 'makedir', 's']
for name, source in files.items(): cmd += ['+', 'write', str(source), name]
subprocess.run(cmd, check=True)
with tempfile.TemporaryDirectory(prefix='space-adf-') as temp:
    for name, source in files.items():
        extracted = Path(temp)/source.name
        subprocess.run([xdftool, str(output), 'read', name, str(extracted)], check=True, stdout=subprocess.DEVNULL)
        assert extracted.read_bytes() == source.read_bytes(), name
# All longword checksums in the 1024-byte boot block add to 0xffffffff,
# with end-around carry. This includes the entry program and root pointer.
data = output.read_bytes()
checksum = 0
for offset in range(0, 1024, 4):
    checksum += int.from_bytes(data[offset:offset+4], 'big')
    checksum = (checksum & 0xffffffff) + (checksum >> 32)
assert checksum == 0xffffffff and data[:4] == b'DOS\0'
print(f'{output}\nSHA256 {hashlib.sha256(data).hexdigest()}')
subprocess.run([xdftool, str(output), 'info'], check=True)
