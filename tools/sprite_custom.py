#!/usr/bin/env python3
"""Write sprite sets as custom sprite files for the port (display.sprites): one PNG per set.

Usage:
  python sprite_custom.py SRC OUT [--scale N] [--palette 0|1] [--bright] [--sets 0,3,10-12]

SRC is either
  - a PTOMBn.EXE, packed or unpacked: the game's own sprites, in a CGA palette (default 0,
    green / red / brown), or
  - a set PNG or a folder of them, in either layout below at any zoom, to convert or rescale
    (e.g. extract_gfx.py's files, or a pack drawn at another size).

For each set it writes OUT/setNN.png: the set's 4 frames side by side, 16x16 each and nothing
else (64 x 16), times --scale (default 1, the original resolution; 2-8 enlarge with
nearest-neighbour). That's a plain 4-frame sprite sheet on a 16x16 grid (times the scale) for
Aseprite and other pixel art editors, and what the port reads. Black (colour 0) becomes
transparent; a source PNG with transparency of its own keeps it instead.

The port also still reads extract_gfx.py's padded layout: 24x24 cells with a 1-px grid round
them (101 x 26 times a zoom), each frame at the top left of its cell.

Needs only the standard library (and extract_gfx.py / unlzexe.py next to it for EXEs).
"""

import argparse
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_gfx  # noqa: E402  (data segment search, frame parsing, palettes)
import unlzexe  # noqa: E402

SETS = 40
FRAMES = 4
FRAME = 16                          # every frame in the game is 16x16
CELL = 24                           # extract_gfx.py's cell
PADDED_W = FRAMES * (CELL + 1) + 1  # 101
PADDED_H = CELL + 2                 # 26
CLEAR = (0, 0, 0, 0)


# --- PNG ------------------------------------------------------------------------------------

def read_png(path):
    """Return (width, height, pixels, has_alpha) with pixels[y][x] = (r, g, b, a). 8- or 16-bit
    (and 1-4 bit indexed), any colour type, not interlaced."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit(f'{path}: not a PNG file')
    pos, idat, palette, trns = 8, b'', [], None
    width = height = depth = ctype = None
    while pos + 8 <= len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            width, height, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if interlace:
                sys.exit(f"{path}: interlaced PNGs aren't supported")
        elif kind == b'PLTE':
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
        pos += 12 + n
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in (8, 16) and not (ctype == 3 and depth in (1, 2, 4)):
        sys.exit(f'{path}: unsupported PNG (colour type {ctype}, depth {depth})')
    bits = channels * depth
    stride = (width * bits + 7) // 8
    bpp = max(1, bits // 8)
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        ftype, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(line)
        prev = line

    alpha = list(trns) + [255] * (256 - len(trns)) if trns and ctype == 3 else None
    pixels = []
    for line in rows:
        out = []
        for x in range(width):
            if ctype == 3:
                bit = x * depth
                i = line[bit // 8] >> (8 - depth - bit % 8) & ((1 << depth) - 1)
                r, g, b = palette[i] if i < len(palette) else (0, 0, 0)
                out.append((r, g, b, alpha[i] if alpha else 255))
                continue
            step = channels * (depth // 8)
            s = [line[x * step + k * (depth // 8)] for k in range(channels)]  # high byte of 16-bit
            if ctype == 0:
                out.append((s[0], s[0], s[0], 255))
            elif ctype == 2:
                out.append((s[0], s[1], s[2], 255))
            elif ctype == 4:
                out.append((s[0], s[0], s[0], s[1]))
            else:
                out.append(tuple(s))
        pixels.append(out)
    has_alpha = ctype in (4, 6) or trns is not None
    return width, height, pixels, has_alpha


def write_png(path, width, height, pixels):
    """pixels[y][x] = (r, g, b, a), written as 8-bit RGBA."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for p in row:
            raw.extend(p)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


# --- sources: each gives sets[n] = a sampler (frame, x, y, scale) -> (r, g, b, a) ------------

def sets_from_exe(path, palette):
    """The game's own sprites. Frames are 16x16 game pixels; colour 0 is transparent."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:2] != b'MZ':
        sys.exit(f'{path}: not a DOS executable')
    if data[0x1C:0x1E] == b'LZ':
        try:
            data, _ = unlzexe.unpack(data)
        except ValueError as e:
            sys.exit(f'{path}: {e}')
    image = data[struct.unpack_from('<H', data, 8)[0] * 16:]
    segs = {struct.unpack('<H', m.group(1))[0] for m in extract_gfx.ISR_PROLOGUE.finditer(image)}
    if len(segs) != 1:
        sys.exit(f"{path}: couldn't find the data segment")
    ds = image[segs.pop() * 16:]
    sets = {}
    for s in range(SETS):
        frames = []
        for fr in range(FRAMES):
            off = (s * FRAMES + fr) * extract_gfx.SLOT
            parsed = extract_gfx.parse_frame(ds[off:off + extract_gfx.SLOT])
            frames.append(parsed[2] if parsed else None)  # rows of colour indices, top first

        def sample(fr, x, y, scale, frames=frames):
            rows = frames[fr]
            if rows is None:
                return CLEAR
            gx, gy = x // scale, y // scale
            if gy >= len(rows) or gx >= len(rows[gy]):
                return CLEAR
            c = rows[gy][gx]
            return palette[c] + (255,) if c else CLEAR
        sets[s] = sample
    return sets


def set_number(path):
    m = re.match(r'set(\d\d)', os.path.basename(path))
    return int(m.group(1)) if m else None


def set_from_png(path):
    """A set file in either layout, any zoom."""
    w, h, px, has_alpha = read_png(path)
    if w % PADDED_W == 0 and h == w // PADDED_W * PADDED_H:
        zoom = w // PADDED_W
        origin = lambda fr: (zoom * (1 + fr * (CELL + 1)), zoom)  # noqa: E731
    elif w % (FRAMES * FRAME) == 0 and h * FRAMES == w:
        zoom = w // (FRAMES * FRAME)
        origin = lambda fr: (zoom * FRAME * fr, 0)  # noqa: E731
    else:
        sys.exit(f"{path}: {w}x{h} isn't a sprite set ({FRAMES * FRAME}x{FRAME} or {PADDED_W}x{PADDED_H}, "
                 f'times a zoom)')

    def sample(fr, x, y, scale):
        x0, y0 = origin(fr)
        # nearest-neighbour from the source's zoom to the output's, at the output pixel's centre
        sx = x0 + (2 * x + 1) * zoom // (2 * scale)
        sy = y0 + (2 * y + 1) * zoom // (2 * scale)
        r, g, b, a = px[sy][sx]
        if a < 128 or (not has_alpha and (r, g, b) == (0, 0, 0)):
            return CLEAR
        return (r, g, b, 255)
    return sample


def sets_from_pngs(src):
    paths = [src] if os.path.isfile(src) else sorted(
        os.path.join(src, n) for n in os.listdir(src) if n.lower().endswith('.png') and set_number(n) is not None)
    sets = {}
    for path in paths:
        n = set_number(path)
        # setNN.png wins over setNN_dsXXXX.png, as in the port
        if n is not None and n < SETS and (n not in sets or os.path.basename(path).lower() == f'set{n:02d}.png'):
            sets[n] = set_from_png(path)
    if not sets:
        sys.exit(f'{src}: no setNN*.png files')
    return sets


# --- main -----------------------------------------------------------------------------------

def parse_sets(text):
    chosen = set()
    for part in text.split(','):
        a, _, b = part.partition('-')
        chosen.update(range(int(a), int(b or a) + 1))
    return chosen


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('src', help='PTOMBn.EXE (packed or not), or a set PNG / folder of them')
    ap.add_argument('out', help='folder to write setNN.png into (e.g. build/sprites)')
    ap.add_argument('--scale', type=int, default=1, help='pixels per game pixel, 1-8 (default 1)')
    ap.add_argument('--palette', type=int, choices=(0, 1), default=0,
                    help='CGA palette for an EXE: 0 green/red/brown (default), 1 cyan/magenta/white')
    ap.add_argument('--bright', action='store_true', help='high-intensity colours (EXE)')
    ap.add_argument('--sets', help='only these sets, e.g. 0-3,10 (default all)')
    args = ap.parse_args()
    if not 1 <= args.scale <= 8:
        sys.exit('--scale must be 1-8 (the port reads zooms up to 8)')

    if os.path.isfile(args.src) and args.src.lower().endswith('.exe'):
        sets = sets_from_exe(args.src, extract_gfx.PALETTES[(args.palette, args.bright)])
    else:
        sets = sets_from_pngs(args.src)
    wanted = parse_sets(args.sets) if args.sets else set(range(SETS))

    os.makedirs(args.out, exist_ok=True)
    side = FRAME * args.scale
    written = 0
    for n in sorted(sets):
        if n not in wanted:
            continue
        sample = sets[n]
        pixels = [[sample(x // side, x % side, y, args.scale) for x in range(FRAMES * side)] for y in range(side)]
        write_png(os.path.join(args.out, f'set{n:02d}.png'), FRAMES * side, side, pixels)
        written += 1
    print(f'{args.src} -> {args.out}: {written} sets, {FRAMES * side}x{side} each '
          f'({FRAMES} frames of {side}x{side}, scale {args.scale})')


if __name__ == '__main__':
    main()
