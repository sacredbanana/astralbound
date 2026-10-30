#!/usr/bin/env python3
"""Reduce downloaded NASA/USGS/NOAA/Hubble products to the checked-in maps.

    python3 -I tools/import_maps.py SOURCE_DIR

SOURCE_DIR holds one folder per product, each with the file exactly as
downloaded (data/README.md lists the URLs): earth-height/ETOPO5.DAT and so
on, as in SOURCES below. Raw grids are read with numpy, JPEG and TIFF images
through macOS sips; nothing else is needed. Every product is area-averaged
to a 1-degree equirectangular grid (longitude -180..180 east across, north
up) and written to data/maps/ as a 16-bit height or 8-bit albedo/ice PGM
for tools/compile_maps.py. Downloaded files are data: run this with -I,
from outside SOURCE_DIR."""
import struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'data' / 'maps'
W, H = 360, 180


def raw(path, w, h, dtype):
    return np.fromfile(path, dtype=dtype, count=w * h).reshape(h, w).astype(np.float64)


def image(path):
    """RGB float array (h, w, 3) of any image sips can open."""
    with tempfile.TemporaryDirectory() as tmp:
        bmp = Path(tmp) / 'image.bmp'
        subprocess.run(['sips', '-s', 'format', 'bmp', str(path), '--out', str(bmp)],
                       check=True, capture_output=True)
        d = bmp.read_bytes()
    offset, = struct.unpack('<I', d[10:14])
    w, h, _, bits = struct.unpack('<iiHH', d[18:30])
    assert bits == 24, path
    stride = (w * 3 + 3) & ~3
    rows = np.frombuffer(d[offset:offset + stride * abs(h)], np.uint8).reshape(abs(h), stride)
    rgb = rows[:, :w * 3].reshape(abs(h), w, 3)[:, :, ::-1]
    return (rgb if h < 0 else rgb[::-1]).astype(np.float64)


def tiff_float(path):
    """One-band uncompressed float32 strip TIFF, with its GDAL nodata."""
    d = Path(path).read_bytes()
    bo = '<' if d[:2] == b'II' else '>'
    ifd, = struct.unpack(bo + 'I', d[4:8])
    tags = {}
    for i in range(struct.unpack(bo + 'H', d[ifd:ifd + 2])[0]):
        tag, kind, count, value = struct.unpack(bo + 'HHII', d[ifd + 2 + 12 * i:ifd + 14 + 12 * i])
        if kind == 3 and count == 1:
            value = struct.unpack(bo + 'H', d[ifd + 10 + 12 * i:ifd + 12 + 12 * i])[0]
        tags[tag] = (kind, count, value, ifd + 10 + 12 * i)
    w, h = tags[256][2], tags[257][2]
    assert tags[258][2] == 32 and tags[259][2] == 1 and tags[339][2] == 3, 'float32, uncompressed'
    count, at = tags[273][1], tags[273][2]
    offsets = struct.unpack(bo + '%dI' % count, d[at:at + 4 * count]) if count > 1 else (at,)
    rows = h // count
    a = np.concatenate([np.frombuffer(d[o:o + 4 * w * rows], bo + 'f4') for o in offsets])
    a = a.reshape(h, w).astype(np.float64)
    if 42113 in tags:
        _, count, at, inline = tags[42113]
        text = d[at:at + count] if count > 4 else d[inline:inline + count]
        a[a == float(text.rstrip(b'\0'))] = np.nan
    return a


def tiff_u8(path):
    """One-band uncompressed 8-bit strip TIFF (a USGS ISIS mosaic): linear
    I/F, with 0 marking pixels no spacecraft imaged."""
    d = Path(path).read_bytes()
    bo = '<' if d[:2] == b'II' else '>'
    ifd, = struct.unpack(bo + 'I', d[4:8])
    tags = {}
    for i in range(struct.unpack(bo + 'H', d[ifd:ifd + 2])[0]):
        tag, kind, count, value = struct.unpack(bo + 'HHII', d[ifd + 2 + 12 * i:ifd + 14 + 12 * i])
        if kind == 3 and count == 1:
            value = struct.unpack(bo + 'H', d[ifd + 10 + 12 * i:ifd + 12 + 12 * i])[0]
        tags[tag] = (kind, count, value)
    w, h = tags[256][2], tags[257][2]
    assert tags[258][2] == 8 and tags[259][2] == 1 and tags[277][2] == 1, '8-bit, uncompressed, one band'
    offsets = np.frombuffer(d[tags[273][2]:tags[273][2] + 4 * h], bo + 'u4') if h > 1 else [tags[273][2]]
    a = np.stack([np.frombuffer(d[o:o + w], np.uint8) for o in offsets]).astype(np.float64)
    a[a == 0] = np.nan
    return a


def luminance(rgb):
    """Linear relative luminance of sRGB-encoded pixels."""
    lin = (rgb / 255) ** 2.2
    return lin @ np.array([0.2126, 0.7152, 0.0722])


def from_east(a, west):
    """Roll columns so the grid's west edge is longitude -180."""
    return np.roll(a, int(round((west + 180) / 360 * a.shape[1])), axis=1)


def area_mean(a, weight=None, cells=1):
    """Mean of every source pixel whose centre falls in each 1-degree cell,
    or each 1/cells degree. NaN pixels and zero weights are left out; empty
    cells become NaN."""
    h, w = a.shape
    gw, gh = W * cells, H * cells
    idx = ((np.arange(h) * gh // h)[:, None] * gw + (np.arange(w) * gw // w)[None, :]).ravel()
    ok = ~np.isnan(a)
    wt = ok.astype(np.float64) if weight is None else weight * ok
    s = np.bincount(idx, (np.where(ok, a, 0) * wt).ravel(), gw * gh)
    c = np.bincount(idx, wt.ravel(), gw * gh)
    with np.errstate(invalid='ignore', divide='ignore'):
        return (s / c).reshape(gh, gw)


def shift(a, dy, dx):
    """Neighbour values: longitude wraps, the poles do not."""
    a = np.roll(a, dx, 1)
    if dy > 0:
        return np.vstack([np.zeros_like(a[:dy]), a[:-dy]])
    return np.vstack([a[-dy:], np.zeros_like(a[:-dy])]) if dy else a


def fill(a, reach=400):
    """Unmapped cells take the mean of their mapped neighbours, growing in
    up to reach cells from the edges. Anything left takes the median."""
    a = a.copy()
    for _ in range(reach):
        gap = np.isnan(a)
        if not gap.any():
            break
        v = np.where(gap, 0, a)
        n = (~gap).astype(np.float64)
        s = sum(shift(v, dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        c = sum(shift(n, dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
        with np.errstate(invalid='ignore', divide='ignore'):
            a[gap] = (s / c)[gap]
    a[np.isnan(a)] = np.nanmedian(a)
    return a


def latitudes(h):
    return 90 - (np.arange(h) + 0.5) * 180 / h


def write_pgm(name, a, top, comment=b''):
    a = np.clip(np.rint(a), 0, top).astype('>u2' if top > 255 else 'u1')
    (OUT / name).write_bytes(b'P5\n' + comment + b'%d %d\n%d\n' % (a.shape[1], a.shape[0], top) + a.tobytes())
    print('wrote', name)


def write_height(name, metres):
    """16 bits linear between the limits the header comment records."""
    lo, hi = np.floor(metres.min()), np.ceil(metres.max())
    write_pgm(name, (metres - lo) / (hi - lo) * 65535, 65535, b'# metres %d %d\n' % (lo, hi))


def write_unit(name, a):
    write_pgm(name, a * 255, 255)


def write_albedo(name, a):
    """Relative reflectance: the median at 128, twice it and brighter 255."""
    write_unit(name, a / (2 * np.nanmedian(a)))


def earth(src):
    # ETOPO5: 4320 x 2160 big-endian metres, first row 90N, first column 0E.
    e = from_east(raw(src / 'earth-height/ETOPO5.DAT', 4320, 2160, '>i2'), 0)
    land = (e >= 0).astype(np.float64)
    frac = area_mean(land)
    up = area_mean(e, land)
    down = area_mean(e, 1 - land)
    # Coastal cells carry their land fraction, so the coast interpolates to
    # where half the cell is land; pure cells carry their mean height.
    s = (frac - 0.5) * 2
    height = np.where(frac >= 1, np.maximum(np.nan_to_num(up), 1),
                      np.where(frac <= 0, np.minimum(np.nan_to_num(down), -1), s * 300))
    write_height('earth-height.pgm', height)
    # Blue Marble 2002: cloud-free land; seasonal snow is not in it.
    rgb = image(next((src / 'earth-colour').glob('*.jpg')))
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mean = rgb.mean(axis=2)
    sea = (b > r + 20) & (b > g)
    lat = latitudes(rgb.shape[0])[:, None] + 0 * r
    # Ice sheets are bright and grey, but relief shading darkens much of
    # Antarctica, which is ice almost everywhere south of 60 degrees.
    ice = ~sea & (((rgb.max(axis=2) - rgb.min(axis=2) < 35) & (mean > 150) & (np.abs(lat) > 55)) | (lat < -60))
    # Dry land is red over green, and bright; 0 is the desert margin.
    dry = (r - g) + 0.3 * (mean - 90)
    write_pgm('earth-albedo.pgm', fill(area_mean(dry, (~sea & ~ice).astype(np.float64))) * 3 + 128, 255)
    write_unit('earth-ice.pgm', area_mean(ice.astype(np.float64)))


def mars(src):
    # MOLA MEGDR 4 px/deg: big-endian metres above the areoid from 0E.
    write_height('mars-height.pgm', area_mean(from_east(raw(src / 'mars-height/megt90n000cb.img', 1440, 720, '>i2'), 0)))
    a = tiff_float(next((src / 'mars-albedo').glob('*.tif')))  # TES bolometric albedo, -180..180
    # The product is clipped to 0.06..0.32; the floor also marks the
    # unmapped holes at both poles.
    a[(a <= 0.0601) | (a > 1)] = np.nan
    write_albedo('mars-albedo.pgm', fill(area_mean(a)))
    # Residual polar caps: bright frost poleward of 70 degrees.
    lat = latitudes(a.shape[0])[:, None] + 0 * a
    cap = np.where(np.isnan(a), np.nan, ((a >= 0.3) & (np.abs(lat) > 70)).astype(np.float64))
    write_unit('mars-ice.pgm', fill(area_mean(cap)))


def moon(src):
    # LOLA LDEM_4: little-endian half metres from 0E.
    write_height('moon-height.pgm', area_mean(from_east(raw(src / 'moon-height/ldem_4.img', 1440, 720, '<i2'), 0) * 0.5))
    write_albedo('moon-albedo.pgm', area_mean(luminance(image(next((src / 'moon-albedo').glob('*.jpg'))))))


def source(src, folder):
    return image(next(p for p in (src / folder).iterdir() if p.suffix in ('.jpg', '.tif')))


def mapped_image(src, folder, name, west, blank=False):
    """A global mosaic's relative reflectance; west is the longitude of its
    left edge (east increases across, as every source here is drawn).
    blank: near-black pixels are unimaged. A few cells round them take
    their neighbours; wide unimaged regions are the median, not streaks."""
    lum = luminance(source(src, folder))
    if blank:
        lum[lum < 0.15 * np.median(lum)] = np.nan
    cells = area_mean(from_east(lum, west))
    write_albedo(name, fill(cells, 4) if blank else fill(cells))


def mosaic(src, folder, name, west, cells=1, smooth=0):
    """A USGS ISIS global mosaic (linear I/F) at 1/cells degree; west is
    the east longitude of its left edge. Unimaged cells take neighbours'.
    smooth: blur over this many cells, for a body whose look is regional."""
    a = fill(area_mean(from_east(tiff_u8(next((src / folder).glob('*.tif'))), west), cells=cells), 4 * cells)
    write_albedo(name, blur(a, smooth) if smooth else a)


def blur(a, cells):
    """Box blur over 2 * cells + 1 grid cells, three times (nearly Gaussian).
    Longitude wraps; latitude clamps at the poles."""
    for _ in range(3):
        a = sum(np.roll(a, k, 1) for k in range(-cells, cells + 1)) / (2 * cells + 1)
        rows = [np.vstack([np.repeat(a[:1], max(0, -k), 0), a[max(0, k):a.shape[0] + min(0, k)],
                           np.repeat(a[-1:], max(0, k), 0)]) for k in range(-cells, cells + 1)]
        a = sum(rows) / (2 * cells + 1)
    return a


def spread(a, cells, pick):
    """Grey-level dilation (pick np.maximum) or erosion (np.minimum) over a
    square of 2 * cells + 1 grid cells."""
    for _ in range(cells):
        rows = [np.vstack([a[:1], a[:-1]]), a, np.vstack([a[1:], a[-1:]])]  # poles repeat
        a = pick.reduce([np.roll(r, dx, 1) for r in rows for dx in (-1, 0, 1)])
    return a


def europa(src):
    """Europa's lineae: 10-20 km dark bands, and the dark spots of chaos
    terrain. A black top-hat (closing minus image, over 2.5 degrees) keeps
    only dark features narrower than that, not the broad dark hemispheres
    or the steps where Galileo and Voyager tiles meet, and only those
    deeper than the mosaic's grain. Each band is widened
    by a half-degree cell either side, so the cube map's lattice cannot step
    between them; beside unimaged ground there are none. Under them the
    regional brightness is compressed to a gentle mottle, so the bands are
    the darkest ground and alone take the body's dark ramp."""
    raw = area_mean(from_east(tiff_u8(next((src / 'europa').glob('*.tif'))), 0), cells=2)
    seen = spread(np.isfinite(raw).astype(np.float64), 4, np.minimum)
    a = fill(raw, 8)
    closing = spread(spread(a, 2, np.maximum), 2, np.minimum)
    # Mosaic grain is 3% deep at the median; bands are over 10%.
    depth = spread(np.maximum((closing - a) / closing * seen - 0.08, 0), 1, np.maximum)
    regional = blur(a, 8)
    write_albedo('europa-albedo.pgm', (regional / np.median(regional)) ** 0.3 * (1 - 3 * depth))


def jupiter(src):
    """Cassini's map by whiteness (blue over red), not brightness: the Red
    Spot is no darker than a belt but much redder than its zone. Halfway to
    each latitude's median, belts and zones stay continuous through their
    festoons and white ovals. The Red Spot is the one feature far redder
    than its latitude (0.6 below its median; nothing else passes 0.3): it is
    set at 0.3 of the map's median, under every belt, for the deep ramp."""
    rgb = source(src, 'jupiter')
    cells = fill(area_mean(from_east((rgb[..., 2] + 8) / (rgb[..., 0] + 8), 0)))
    zonal = np.median(cells, axis=1, keepdims=True)
    out = (cells + zonal) / 2
    red = (cells - zonal) / np.median(cells)
    # The core, grown by a degree where it is still clearly redder: the
    # oval rather than only its reddest heart.
    spot = spread((red < -0.35).astype(np.float64), 1, np.maximum).astype(bool) & (red < -0.12)
    out[spot] = 0.3 * np.median(out)
    write_albedo('jupiter-albedo.pgm', out)


def banded(src, folder, name):
    """A giant's zonal profile: each latitude's median reflectance. Spots
    on Saturn and the ice giants come and go within months, and Hubble
    sees only part of each; the bands are what persists. Rows mostly
    hidden (the far hemisphere, Saturn's rings) extend from the nearest
    observed latitude. Data edges are black or fringed in false colour."""
    rgb = source(src, folder)
    lum = luminance(rgb)
    chroma = (rgb.max(axis=2) - rgb.min(axis=2)) / np.maximum(rgb.max(axis=2), 1)
    ok = (lum > 0.25 * np.median(lum[lum > 0])) & (chroma < 0.6)
    rows = area_mean(np.where(ok, lum, np.nan))
    seen = area_mean(ok.astype(np.float64))
    profile = np.array([np.nanmedian(r) if f > 0.7 else np.nan for r, f in zip(rows, seen.mean(axis=1))])
    # Rows beside a gap are limb-darkened and fringed: drop three.
    gap = np.isnan(profile)
    for k in (1, 2, 3):
        profile[np.roll(gap, k) | np.roll(gap, -k)] = np.nan
    have = np.flatnonzero(~np.isnan(profile))
    profile = profile[have[np.abs(have[None, :] - np.arange(H)[:, None]).argmin(axis=1)]]
    write_albedo(name, np.repeat(profile[:, None], W, axis=1))


SOURCES = {
    'earth': earth,
    'mars': mars,
    'moon': moon,
    # Left edges were checked against landmarks: Caloris at 162.7E; Pele
    # at 255W on Io; Europa's dark trailing side, Ganymede's Galileo Regio
    # and Callisto's Asgard; Pluto's Sputnik Planitia in the heart's west.
    'mercury': lambda s: mapped_image(s, 'mercury', 'mercury-albedo.pgm', -180),
    # Cassini, December 2000: west longitude increasing leftward from 0W at
    # the right edge; the Red Spot's turbulent wake trails west of it.
    'jupiter': jupiter,
    'saturn': lambda s: banded(s, 'saturn', 'saturn-albedo.pgm'),
    'uranus': lambda s: banded(s, 'uranus', 'uranus-albedo.pgm'),
    'neptune': lambda s: banded(s, 'neptune', 'neptune-albedo.pgm'),
    'io': lambda s: mapped_image(s, 'io', 'io-albedo.pgm', -180),
    # The 500 m mosaics: Europa's left edge is 0E (360W); Iapetus's is
    # 180W, with Cassini Regio dark across the leading hemisphere round 90W.
    'europa': europa,
    # Its photometry is flattened (dark and bright differ 1.5x, not 10x), so
    # its two tones are drawn from the regional brightness, not craters.
    'iapetus': lambda s: mosaic(s, 'iapetus', 'iapetus-albedo.pgm', -180, smooth=2),
    'ganymede': lambda s: mapped_image(s, 'ganymede', 'ganymede-albedo.pgm', 0, blank=True),
    'callisto': lambda s: mapped_image(s, 'callisto', 'callisto-albedo.pgm', 0, blank=True),
    # New Horizons: the south was in polar night, black in the mosaics.
    'pluto': lambda s: mapped_image(s, 'pluto', 'pluto-albedo.pgm', 0, blank=True),
    'charon': lambda s: mapped_image(s, 'charon', 'charon-albedo.pgm', 0, blank=True),
}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = Path(sys.argv[1]).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    for name in sys.argv[2:] or SOURCES:
        SOURCES[name](src)


if __name__ == '__main__':
    main()
