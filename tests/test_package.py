#!/usr/bin/env python3
"""Exercise staging and ZIP/LHA creation with a fresh-checkout music tree."""
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('lha'), 'archive-capable lha is required')
class PackageTests(unittest.TestCase):
    def test_complete_music_tree_in_staging_and_both_archives(self):
        with tempfile.TemporaryDirectory(prefix='astralbound-package-test-') as temp:
            root = Path(temp)
            for name in ('release', 'docs', 'data', 'music'):
                shutil.copytree(ROOT/name, root/name)
            # Empty intro is not tracked in Git. It must still be shipped.
            shutil.rmtree(root/'music/intro', ignore_errors=True)
            for name in ('Astralbound.readme', 'Astralbound.guide', 'LICENSE',
                         'README.md', 'THIRD_PARTY.txt', 'Makefile.Amiga'):
                shutil.copy2(ROOT/name, root/name)
            (root/'tools').mkdir()
            shutil.copy2(ROOT/'tools/package.py', root/'tools/package.py')
            (root/'dist/amiga').mkdir(parents=True)
            for cpu in ('000', '020', '030', '020fpu', '040', '060'):
                (root/'dist/amiga'/('Astralbound.'+cpu)).write_bytes(cpu.encode())
            # Test all filename cases supported by the runtime, spaces, and
            # ancillary/nested assets. Hidden host files must stay out.
            for name in ('space-flight/MOD.Extra Track', 'battle/EXTRA.MOD',
                         'docked/credits.txt', 'extras/nested/asset.bin',
                         '.hidden/private.mod', 'battle/.DS_Store'):
                path = root/'music'/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            expected = {p.relative_to(root).as_posix(): p.read_bytes()
                        for p in (root/'music').rglob('*') if p.is_file()
                        and not any(c.startswith('.') for c in p.relative_to(root/'music').parts)}
            subprocess.run(['make', '-f', 'Makefile.Amiga', 'music-folders'],
                           cwd=root, check=True, stdout=subprocess.DEVNULL)
            staged = {p.relative_to(root/'dist/amiga').as_posix(): p.read_bytes()
                      for p in (root/'dist/amiga/music').rglob('*') if p.is_file()}
            self.assertEqual(staged, expected)
            self.assertTrue((root/'dist/amiga/music/intro').is_dir())
            subprocess.run(['python3', 'tools/package.py'], cwd=root, check=True,
                           stdout=subprocess.DEVNULL)
            with zipfile.ZipFile(root/'dist/Astralbound-0.1-amiga.zip') as archive:
                archived = {n.removeprefix('Astralbound/'): archive.read(n)
                            for n in archive.namelist()
                            if n.startswith('Astralbound/music/') and not n.endswith('/')}
                self.assertEqual(archived, expected)
                self.assertTrue(archive.getinfo('Astralbound/music/intro/').is_dir())
            extracted = root/'extracted'
            extracted.mkdir()
            subprocess.run(['lha', 'x', str(root/'dist/Astralbound.lha')],
                           cwd=extracted, check=True, stdout=subprocess.DEVNULL)
            archived = {p.relative_to(extracted/'Astralbound').as_posix(): p.read_bytes()
                        for p in (extracted/'Astralbound/music').rglob('*') if p.is_file()}
            self.assertEqual(archived, expected)
            self.assertTrue((extracted/'Astralbound/music/intro').is_dir())


if __name__ == '__main__':
    unittest.main()
