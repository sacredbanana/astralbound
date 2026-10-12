#!/usr/bin/env python3
# SPDX-License-Identifier: Unlicense
"""Package original game files; do not bundle ROMs, SDKs or reference assets."""
from pathlib import Path
import hashlib
import zipfile
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
files = [root/'dist/amiga/Space.000', root/'dist/amiga/Space.020',
         root/'Space.guide', root/'LICENSE', root/'README.md', root/'THIRD_PARTY.txt']
files += sorted((root/'docs').glob('*.md'))
output = root/'dist/Space-0.1-amiga.zip'
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in files:
        name = path.name if path.parent == root/'dist/amiga' else str(path.relative_to(root))
        archive.write(path, 'Space/'+name)
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for path in files:
        name = path.name if path.parent == root/'dist/amiga' else str(path.relative_to(root))
        assert archive.read('Space/'+name) == path.read_bytes()
print(f'{output}\nSHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')

lha = shutil.which('lha')
if lha:
    with tempfile.TemporaryDirectory(prefix='space-package-') as temp:
        stage=Path(temp)
        for path in files:
            name = path.name if path.parent == root/'dist/amiga' else str(path.relative_to(root))
            dest=stage/'Space'/name
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,dest)
        archive=root/'dist/Space-0.1-amiga.lha'
        if archive.exists(): archive.unlink()
        subprocess.run([lha,'a',str(archive),'Space'],cwd=stage,check=True,stdout=subprocess.DEVNULL)
        extracted=stage/'verify'
        extracted.mkdir()
        subprocess.run([lha,'x',str(archive)],cwd=extracted,check=True,stdout=subprocess.DEVNULL)
        for path in files:
            name = path.name if path.parent == root/'dist/amiga' else str(path.relative_to(root))
            assert (extracted/'Space'/name).read_bytes()==path.read_bytes()
        print(f'{archive}\nSHA256 {hashlib.sha256(archive.read_bytes()).hexdigest()}')
