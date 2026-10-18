#!/usr/bin/env python3
"""Exercise staging, ZIP/LHA creation with a fresh-checkout music tree, and the
Cranker hunk checks used for the boot ADF."""
import os
import shutil
import struct
import subprocess
import sys
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


sys.path.insert(0, str(ROOT/'tools'))
import cranker  # noqa: E402


def executable(hunks, debug_hunk=False):
    """Minimal hunk file: (type, payload, relocs) per hunk, BSS payload = size."""
    longs = lambda *values: struct.pack('>%dI' % len(values), *values)
    sizes = [len(p) if t != 0x3eb else p for t, p, _ in hunks] + ([16] if debug_hunk else [])
    out = longs(0x3f3, 0, len(sizes), 0, len(sizes)-1, *[(s+3)//4 for s in sizes])
    for kind, payload, relocs in hunks:
        if kind == 0x3eb:
            out += longs(kind, (payload+3)//4)
        else:
            out += longs(kind, (len(payload)+3)//4) + payload.ljust((len(payload)+3)//4*4, b'\0')
        if relocs:
            out += longs(0x3ec)
            for target, offsets in relocs.items():
                out += longs(len(offsets), target, *offsets)
            out += longs(0)
        out += longs(0x3f0, 1) + b'main' + longs(0, 0) + longs(0x3f2)
    if debug_hunk:
        out += longs(0x3f1, 4) + bytes(16) + longs(0x3f2)
    return out


class CrankerTests(unittest.TestCase):
    CODE = bytes(range(256)) * 64 + b'\x4e\x75'
    HUNKS = [(0x3e9, CODE, {1: [0, 8]}), (0x3ea, b'DATA' * 512 + bytes(64), {0: [4]}),
             (0x3eb, 4096, {})]

    def test_hunks_skips_symbols_debug_hunk_and_zero_padding(self):
        with tempfile.TemporaryDirectory() as temp:
            plain, debug = Path(temp)/'plain', Path(temp)/'debug'
            plain.write_bytes(executable(self.HUNKS))
            debug.write_bytes(executable(self.HUNKS, debug_hunk=True))
            parsed = cranker.hunks(plain)
            self.assertEqual(parsed, cranker.hunks(debug))
            self.assertEqual([size for _, size, _, _ in parsed], [16388, 2112, 4096])
            self.assertEqual(parsed[0][3], {1: {0, 8}})
            self.assertEqual(parsed[1][2], b'DATA' * 512)

    @unittest.skipUnless(os.environ.get('CRANKER') or (ROOT/'build/cranker/cranker').exists(),
                         'Cranker is not built (run tools/make_adf.py once)')
    def test_crunch_round_trips_hunks(self):
        with tempfile.TemporaryDirectory() as temp:
            source, output = Path(temp)/'game', Path(temp)/'game.crunched'
            source.write_bytes(executable(self.HUNKS, debug_hunk=True))
            cranker.crunch(source, output)
            self.assertLess(output.stat().st_size, source.stat().st_size)
            self.assertFalse(Path(temp, 'game.crunched.verify').exists())


if __name__ == '__main__':
    unittest.main()
