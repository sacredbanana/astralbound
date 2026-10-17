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
lha = shutil.which('lha')
if not lha:
    raise SystemExit('LHA packaging requires an archive-capable lha command in PATH.')
readme = root/'Astralbound.readme'
readme_text = readme.read_bytes().decode('ascii')
if '\r' in readme_text or any(len(line) > 78 for line in readme_text.splitlines()):
    raise SystemExit('Astralbound.readme must use LF lines of at most 78 characters.')
header = dict(line.split(':', 1) for line in readme_text.split('\n\n', 1)[0].splitlines())
if (not readme_text.startswith('Short:') or
        any(not header.get(key, '').strip() for key in ('Short', 'Uploader', 'Type', 'Architecture')) or
        len(header['Short'].strip()) > 40 or '@' not in header['Uploader']):
    raise SystemExit('Astralbound.readme has invalid or missing Aminet header fields.')
files = [root/'dist/amiga'/('Astralbound.'+cpu) for cpu in ('000','020','030','020fpu','040','060')]
files += [
         root/'Astralbound.guide', readme, root/'LICENSE', root/'README.md', root/'THIRD_PARTY.txt']
files += sorted((root/'docs').glob('*.md'))
files += [root/'data/README.md', root/'data/LICENSE-HYG.txt']
music_dirs = sorted(p for p in (root/'music').rglob('*') if p.is_dir())
# Users add their own tracks locally; never redistribute them implicitly.
files += [root/'music/README.txt']
files += sorted(p for p in (root/'release').iterdir() if p.name not in ('Disk.info','boot.s','startup-sequence'))
def archive_name(path):
    if path == root/'release/Astralbound.info': return 'Astralbound.info'
    name = path.name if path.parent in (root/'dist/amiga',root/'release') else str(path.relative_to(root))
    return 'Astralbound/'+name
output = root/'dist/Astralbound-0.1-amiga.zip'
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in music_dirs:
        archive.writestr('Astralbound/'+str(path.relative_to(root))+'/', '')
    for path in files:
        archive.write(path, archive_name(path))
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for path in music_dirs:
        assert archive.getinfo('Astralbound/'+str(path.relative_to(root))+'/').is_dir()
    for path in files:
        assert archive.read(archive_name(path)) == path.read_bytes()
print(f'{output}\nSHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')

with tempfile.TemporaryDirectory(prefix='astralbound-package-') as temp:
    stage=Path(temp)
    for path in music_dirs:
        (stage/'Astralbound'/path.relative_to(root)).mkdir(parents=True,exist_ok=True)
    for path in files:
        dest=stage/archive_name(path)
        dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,dest)
    # Aminet updates use a stable basename, shared with the external readme.
    archive=root/'dist/Astralbound.lha'
    if archive.exists(): archive.unlink()
    subprocess.run([lha,'a',str(archive),'Astralbound','Astralbound.info'],cwd=stage,check=True,stdout=subprocess.DEVNULL)
    extracted=stage/'verify'
    extracted.mkdir()
    subprocess.run([lha,'x',str(archive)],cwd=extracted,check=True,stdout=subprocess.DEVNULL)
    for path in files:
        assert (extracted/archive_name(path)).read_bytes()==path.read_bytes()
    for path in music_dirs:
        assert (extracted/'Astralbound'/path.relative_to(root)).is_dir()
    print(f'{archive}\nSHA256 {hashlib.sha256(archive.read_bytes()).hexdigest()}')
    sidecar = archive.with_suffix('.readme')
    shutil.copy2(readme, sidecar)
    assert sidecar.read_bytes() == readme.read_bytes()
    print(sidecar)
