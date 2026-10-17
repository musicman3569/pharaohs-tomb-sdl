#!/usr/bin/env python3
"""Prepare sprite sets for an AI image editor and turn its results back into custom sprites.

Usage:
  python sprite_ai.py split SRC [SRC ...] --out DIR [--size 1024]
  python sprite_ai.py join EDITED.png --set N --out SPRITE_DIR [--zoom 2] [--black 24]
                           [--clip ORIGINAL_SET.png]

split
  SRC is a sprite set file in the layout tools/extract_gfx.py writes (4 frames in 24x24 cells
  with a 1-px grid, any zoom), or a folder of them. For each set it writes DIR/setNN_sheet.png:
  the four 16x16 frames alone, without the padding and grid, as a 2x2 sheet (frames 1 2 / 3 4)
  scaled up with nearest-neighbour to SIZE x SIZE. Square and padding-free is what image-edit
  models handle well, and seeing all four frames at once keeps them consistent.

join
  EDITED is the editor's result for one sheet, at any size: it is read as a 2x2 grid of
  quadrants. Each quadrant is scaled down to a 16 * ZOOM square (2 = twice the original
  pixels on each axis) by averaging the middle of each block, which ignores the soft edges
  models put between pixels. Near-black pixels (all channels <= BLACK) become transparent, so
  the port shows the background through them. The result is written as SPRITE_DIR/setNN.png in
  the port's layout (101 x 26 times ZOOM), which the port prefers over setNN_dsXXXX.png in the
  same folder. --clip ORIGINAL also makes transparent everything outside the original
  sprite's pixels, so the new art can't spill past the shape the game collides with.

Needs only the standard library.
"""

import argparse
import os
import re
import struct
import sys
import zlib

FRAMES = 4
CELL = 24      # cell size in the set layout, in game pixels
SPRITE = 16    # every frame in the game is 16x16
STRIP_W = FRAMES * (CELL + 1) + 1   # 101
STRIP_H = CELL + 2                  # 26
GRID_RGBA = (0x30, 0x30, 0x30, 0xFF)  # extract_gfx.py's grid colour


# --- PNG ------------------------------------------------------------------------------------

def read_png(path):
    """Return (width, height, pixels) with pixels[y][x] = (r, g, b, a). 8- or 16-bit, any colour
    type, not interlaced."""
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
                sys.exit(f'{path}: interlaced PNGs aren\'t supported')
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
    return width, height, pixels


def write_png(path, width, height, pixels, alpha):
    """pixels[y][x] = (r, g, b, a); written as RGBA if alpha, else RGB."""
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for p in row:
            raw.extend(p if alpha else p[:3])

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6 if alpha else 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


# --- the set layout -------------------------------------------------------------------------

def read_frames(path):
    """The four 16x16 frames of a set file, as frames[f][y][x] = (r, g, b) in game pixels."""
    w, h, px = read_png(path)
    zoom = w // STRIP_W
    if zoom < 1 or w != STRIP_W * zoom or h != STRIP_H * zoom:
        sys.exit(f'{path}: {w}x{h} isn\'t a sprite set ({STRIP_W}x{STRIP_H} times a zoom)')
    frames = []
    for f in range(FRAMES):
        x0, y0 = zoom * (1 + f * (CELL + 1)), zoom
        frames.append([[px[y0 + y * zoom + zoom // 2][x0 + x * zoom + zoom // 2][:3] for x in range(SPRITE)]
                       for y in range(SPRITE)])
    return frames


def set_number(path):
    m = re.match(r'set(\d\d)', os.path.basename(path))
    return int(m.group(1)) if m else None


# --- commands -------------------------------------------------------------------------------

def split(args):
    files = []
    for src in args.src:
        if os.path.isdir(src):
            files += sorted(os.path.join(src, n) for n in os.listdir(src)
                            if n.lower().endswith('.png') and set_number(n) is not None)
        else:
            files.append(src)
    seen = set()
    os.makedirs(args.out, exist_ok=True)
    side = 2 * SPRITE
    if args.size % side:
        sys.exit(f'--size must be a multiple of {side}')
    scale = args.size // side
    for path in files:
        n = set_number(path)
        if n is None or n in seen:  # one per set (setNN.png and setNN_dsXXXX.png may both exist)
            continue
        seen.add(n)
        frames = read_frames(path)
        sheet = [[(0, 0, 0)] * side for _ in range(side)]
        for f, frame in enumerate(frames):
            qx, qy = (f % 2) * SPRITE, (f // 2) * SPRITE
            for y in range(SPRITE):
                sheet[qy + y][qx:qx + SPRITE] = frame[y]
        big = []
        for row in sheet:
            wide = [p + (255,) for p in row for _ in range(scale)]
            big.extend([wide] * scale)
        out = os.path.join(args.out, f'set{n:02d}_sheet.png')
        write_png(out, args.size, args.size, big, alpha=False)
        print(f'{path} -> {out}')


def join(args):
    w, h, px = read_png(args.edited)
    zoom = args.zoom
    size = SPRITE * zoom
    clip = read_frames(args.clip) if args.clip else None
    qw, qh = w / 2, h / 2
    layout = [[(0, 0, 0, 0)] * (STRIP_W * zoom) for _ in range(STRIP_H * zoom)]
    for y in range(STRIP_H * zoom):  # the grid between and round the cells, as extract_gfx.py draws it
        for x in range(STRIP_W * zoom):
            if y < zoom or y >= (STRIP_H - 1) * zoom or (x // zoom) % (CELL + 1) == 0:
                layout[y][x] = GRID_RGBA
    cleared = 0
    for f in range(FRAMES):
        qx0, qy0 = (f % 2) * qw, (f // 2) * qh
        bw, bh = qw / size, qh / size  # one output pixel's block in the edited image
        x0, y0 = zoom * (1 + f * (CELL + 1)), zoom
        for v in range(size):
            for u in range(size):
                # average the middle half of the block
                xa, xb = int(qx0 + (u + 0.25) * bw), max(int(qx0 + (u + 0.75) * bw), int(qx0 + (u + 0.25) * bw) + 1)
                ya, yb = int(qy0 + (v + 0.25) * bh), max(int(qy0 + (v + 0.75) * bh), int(qy0 + (v + 0.25) * bh) + 1)
                acc, n = [0, 0, 0, 0], 0
                for yy in range(ya, min(yb, h)):
                    for xx in range(xa, min(xb, w)):
                        p = px[yy][xx]
                        for k in range(4):
                            acc[k] += p[k]
                        n += 1
                r, g, b, a = (round(c / n) for c in acc) if n else (0, 0, 0, 0)
                transparent = a < 128 or max(r, g, b) <= args.black
                if clip and clip[f][v // zoom][u // zoom] == (0, 0, 0):
                    transparent = True
                if transparent:
                    cleared += 1
                layout[y0 + v][x0 + u] = (0, 0, 0, 0) if transparent else (r, g, b, 255)
    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, f'set{args.set:02d}.png')
    write_png(out, STRIP_W * zoom, STRIP_H * zoom, layout, alpha=True)
    print(f'{args.edited} ({w}x{h}) -> {out} ({STRIP_W * zoom}x{STRIP_H * zoom}, zoom {zoom}, '
          f'{cleared} of {FRAMES * size * size} sprite pixels transparent)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    sp = sub.add_parser('split', help='set files -> square 2x2 sheets for the editor')
    sp.add_argument('src', nargs='+', help='set PNG files or folders of them')
    sp.add_argument('--out', required=True, help='folder for the setNN_sheet.png files')
    sp.add_argument('--size', type=int, default=1024, help='sheet size in pixels (default 1024)')
    jp = sub.add_parser('join', help='an edited sheet -> a custom sprite set file')
    jp.add_argument('edited', help="the editor's output for one sheet")
    jp.add_argument('--set', type=int, required=True, help='which set it is (0-39)')
    jp.add_argument('--out', required=True, help='the sprites folder (writes setNN.png there)')
    jp.add_argument('--zoom', type=int, default=2, help='output pixels per game pixel (1-8, default 2)')
    jp.add_argument('--black', type=int, default=24,
                    help='pixels with every channel at or below this become transparent (default 24)')
    jp.add_argument('--clip', help='original set file: also clear pixels outside its sprite shapes')
    args = ap.parse_args()
    if args.cmd == 'join' and not (0 <= args.set < 40 and 1 <= args.zoom <= 8):
        sys.exit('--set must be 0-39 and --zoom 1-8')
    split(args) if args.cmd == 'split' else join(args)


if __name__ == '__main__':
    main()
