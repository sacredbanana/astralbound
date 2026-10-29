#!/usr/bin/env python3
"""Bake the checked-in Solar System maps into src/body_maps.inc; no network.

data/maps.csv lists each mapped body and its channels. Channel images in
data/maps/ are equirectangular PGMs (longitude -180..180 east across, north
up), written by tools/import_maps.py: heights are 16-bit, linear between the
metre limits in their "# metres LO HI" comment; albedo has the body's median
at 128 and ice its frozen fraction, both 8-bit. Each channel becomes a cube map of (n + 1)^2 lattice points
per face, directions exactly as src/body_map.c and planet_contours.c index
them, so points on a face edge are the same sample from both faces."""
import csv, math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAMPS = ['MATERIAL', 'GREY', 'RUST', 'TAN', 'BROWN', 'CLOUD', 'SKY', 'DEEP', 'SLATE']


def read_pgm(path):
    data = path.read_bytes()
    fields, pos, comments = [], 0, []
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            end = data.index(b'\n', pos)
            comments.append(data[pos + 1:end].decode().split())
            pos = end
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    assert fields[0] == b'P5', path
    w, h, top = int(fields[1]), int(fields[2]), int(fields[3])
    size = 2 if top > 255 else 1
    raw = data[pos:pos + w * h * size]
    assert len(raw) == w * h * size, path
    values = [int.from_bytes(raw[i:i + size], 'big') for i in range(0, len(raw), size)]
    if size == 2:
        lo, hi = next(map(float, c[1:]) for c in comments if c[0] == 'metres')
        values = [lo + (hi - lo) * v / top for v in values]
    return w, h, top, values


def sampler(image):
    """Bilinear lookup by latitude and east longitude in degrees."""
    w, h, _, v = image

    def at(lat, lon):
        x = (lon + 180) / 360 * w - 0.5
        y = (90 - lat) / 180 * h - 0.5
        y = min(max(y, 0), h - 1)
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        y1 = min(y0 + 1, h - 1)
        a, b = v[y0 * w + x0 % w], v[y0 * w + (x0 + 1) % w]
        c, d = v[y1 * w + x0 % w], v[y1 * w + (x0 + 1) % w]
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
    return at


def lattice_dirs(n, retrograde=False):
    """(latitude, longitude) of every lattice point, face by face."""
    out = []
    for face in range(6):
        axis, sign = face // 2, -n if face & 1 else n
        for j in range(n + 1):
            for i in range(n + 1):
                u, v = -n + 2 * i, -n + 2 * j
                x, y, z = ((sign, u, v), (u, sign, v), (u, v, sign))[axis]
                r = math.sqrt(x * x + y * y + z * z)
                # Body -z is the positive (spin) pole and +x the prime
                # meridian, with east towards +y (tools/compile_solar.py):
                # the ground moves east and maps show unmirrored. A
                # retrograde body's IAU north is its negative pole, +z.
                lat = math.degrees(math.asin(-z / r))
                out.append((-lat if retrograde else lat, math.degrees(math.atan2(y, x))))
    return out


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(round(v))))


def median(values):
    s = sorted(values)
    return s[len(s) // 2]


def earth_base(e):
    """Real metres to the generator's planet-scale base height: coasts kept
    exactly, deep ocean and high plateaus folded into its -1450..1750 m."""
    if e < 0:
        return max(e, -1450)
    return e if e <= 500 else min(500 + (e - 500) / 6, 1750)


def channel(row, key):
    name = row[key]
    return read_pgm(ROOT / 'data' / 'maps' / name) if name else None


def bake(row):
    n = int(row['n'])
    dirs = lattice_dirs(n, 'RETROGRADE' in row['flags'].split())
    out = {}
    height = channel(row, 'height')
    if height:
        at = sampler(height)
        metres = [at(la, lo_) for la, lo_ in dirs]
        if row['type'] == 'OCEAN':
            vals = []
            for e in metres:
                c = (earth_base(e) + 1450) * 4096 / 3200 / 16
                # Sea level is exactly 116 * 16: never move a coast in rounding.
                vals.append(max(116, clamp(c)) if e >= 0 else min(115, clamp(c)))
            out['continent'] = vals
        else:
            datum = median(metres)
            scale = float(row['relief_scale'])
            game = [(e - datum) * scale for e in metres]
            step = max(1, math.ceil(max(abs(g) for g in game) / 127))
            out['relief'] = [clamp(128 + g / step) for g in game]
            out['relief_step'] = step
    albedo = channel(row, 'albedo')
    if albedo:
        at = sampler(albedo)
        raw = [at(la, lo_) for la, lo_ in dirs]
        contrast = float(row['contrast'])
        # Earth's channel is already centred on the desert margin.
        ref = 128 if 'ARID' in row['flags'] else median(raw)
        out['albedo'] = [clamp(128 + 128 * contrast * (a / ref - 1)) for a in raw]
    ice = channel(row, 'ice')
    if ice:
        at = sampler(ice)
        out['ice'] = [clamp(at(la, lo_)) for la, lo_ in dirs]
    return n, out


def c_array(name, values):
    lines = ['static const uint8_t %s[%d] = {' % (name, len(values))]
    for i in range(0, len(values), 24):
        lines.append('    ' + ','.join(str(v) for v in values[i:i + 24]) + ',')
    lines.append('};')
    return lines


def main():
    rows = list(csv.DictReader(open(ROOT / 'data' / 'maps.csv')))
    lines = ['/* Measured Solar System maps; see data/README.md. Generated by tools/compile_maps.py. */']
    table = []
    for row in rows:
        n, out = bake(row)
        ident = row['name'].lower()
        refs = {}
        for key in ('continent', 'relief', 'albedo', 'ice'):
            if key in out:
                lines += c_array('%s_%s' % (ident, key), out[key])
                refs[key] = '%s_%s' % (ident, key)
        flags = ' | '.join('BODY_MAP_' + f for f in row['flags'].split() if f != 'RETROGRADE') or '0'
        ramps = ','.join('RAMP_' + r for r in row['ramps'].split())
        assert all(r in RAMPS for r in row['ramps'].split()) and len(row['ramps'].split()) == 6, row['name']
        table.append('    {"%s",%d,%s,%s,%s,%d,%s,%s,{%s},RAMP_%s},' % (
            row['name'], n, flags, refs.get('continent', 'NULL'), refs.get('relief', 'NULL'),
            out.get('relief_step', 0), refs.get('albedo', 'NULL'), refs.get('ice', 'NULL'),
            ramps, row['dark_ramp'] or 'MATERIAL'))
    lines += ['static const BodyMap body_maps[] = {'] + table + ['};']
    (ROOT / 'src' / 'body_maps.inc').write_text('\n'.join(lines) + '\n')
    print(len(rows), 'mapped bodies')


if __name__ == '__main__':
    main()
