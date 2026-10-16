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
files = [root/'dist/amiga'/('Space.'+cpu) for cpu in ('000','020','030','020fpu','040','060')]
files += [
         root/'Space.guide', root/'LICENSE', root/'README.md', root/'THIRD_PARTY.txt']
files += sorted((root/'docs').glob('*.md'))
music_dirs = sorted(p for p in (root/'music').rglob('*') if p.is_dir())
# Users add their own tracks locally; never redistribute them implicitly.
files += [root/'music/README.txt']
files += sorted(p for p in (root/'release').iterdir() if p.name not in ('Disk.info','boot.s','startup-sequence'))
def archive_name(path):
    if path == root/'release/Space.info': return 'Space.info'
    name = path.name if path.parent in (root/'dist/amiga',root/'release') else str(path.relative_to(root))
    return 'Space/'+name
output = root/'dist/Space-0.1-amiga.zip'
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in music_dirs:
        archive.writestr('Space/'+str(path.relative_to(root))+'/', '')
    for path in files:
        archive.write(path, archive_name(path))
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for path in music_dirs:
        assert archive.getinfo('Space/'+str(path.relative_to(root))+'/').is_dir()
    for path in files:
        assert archive.read(archive_name(path)) == path.read_bytes()
print(f'{output}\nSHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')

lha = shutil.which('lha')
if lha:
    with tempfile.TemporaryDirectory(prefix='space-package-') as temp:
        stage=Path(temp)
        for path in music_dirs:
            (stage/'Space'/path.relative_to(root)).mkdir(parents=True,exist_ok=True)
        for path in files:
            dest=stage/archive_name(path)
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,dest)
        archive=root/'dist/Space-0.1-amiga.lha'
        if archive.exists(): archive.unlink()
        subprocess.run([lha,'a',str(archive),'Space','Space.info'],cwd=stage,check=True,stdout=subprocess.DEVNULL)
        extracted=stage/'verify'
        extracted.mkdir()
        subprocess.run([lha,'x',str(archive)],cwd=extracted,check=True,stdout=subprocess.DEVNULL)
        for path in files:
            assert (extracted/archive_name(path)).read_bytes()==path.read_bytes()
        for path in music_dirs:
            assert (extracted/'Space'/path.relative_to(root)).is_dir()
        print(f'{archive}\nSHA256 {hashlib.sha256(archive.read_bytes()).hexdigest()}')
