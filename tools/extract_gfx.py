#!/usr/bin/env python3
"""Extract Pharaoh's Tomb graphics to PNG: the sprite sets built into the EXE and the .MNU
title screen (a PCX file).

Usage:
  python extract_gfx.py sprites UNPACKED.EXE OUTDIR [--palette 0|1] [--bright]
  python extract_gfx.py pcx FILE.MNU OUT.png [--palette 0|1] [--bright]

Sprite format (Turbo Pascal Graph3 GetPic buffer):
  word mode (2 = 320x200 4-colour), word width, word height,
  then `height` rows of 2-bit pixels, MSB first, ceil(width/4) bytes per row,
  BOTTOM row first (matching PutPic, whose Y is the picture's bottom row).
The game keeps 40 sprite sets at the start of its data segment. Each set is 4 animation
frames, and each frame sits in a 0x9C-byte slot (room for up to 24x24).

Needs only the standard library; PNGs are written as 8-bit indexed images.
"""

import argparse
import os
import re
import struct
import sys
import zlib

SETS = 40
FRAMES = 4
SLOT = 0x9C

# CGA palettes, indexed [palette][bright]. The game itself uses palette 1 (cyan / magenta /
# white): it selects it with INT 10h AH=0Bh BX=0100h and leaves intensity off, and the MNU PCX
# header says the same (byte 19 = 0x40). Palette 0 (green / red / brown) is offered as an
# alternative look.
PALETTES = {
    (0, False): [(0, 0, 0), (0x00, 0xAA, 0x00), (0xAA, 0x00, 0x00), (0xAA, 0x55, 0x00)],
    (0, True): [(0, 0, 0), (0x55, 0xFF, 0x55), (0xFF, 0x55, 0x55), (0xFF, 0xFF, 0x55)],
    (1, False): [(0, 0, 0), (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0xAA), (0xAA, 0xAA, 0xAA)],
    (1, True): [(0, 0, 0), (0x55, 0xFF, 0xFF), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0xFF)],
}
GAP = 4  # palette index for grid lines in contact sheets
GAP_RGB = (0x30, 0x30, 0x30)


def write_png(path, pixels, width, height, palette):
    """pixels: list of rows, each a bytes-like of palette indices."""
    raw = b"".join(b"\x00" + bytes(row) for row in pixels)

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b"PLTE", b"".join(bytes(c) for c in palette)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def scale(pixels, n):
    out = []
    for row in pixels:
        wide = bytes(p for p in row for _ in range(n))
        out.extend([wide] * n)
    return out


def unpack_2bpp(data, width, height):
    stride = (width * 2 + 7) // 8
    rows = []
    for y in range(height):
        line = data[y * stride:(y + 1) * stride]
        rows.append(bytes((line[x // 4] >> (6 - 2 * (x % 4))) & 3 for x in range(width)))
    return rows


# --- EXE / data segment -------------------------------------------------------------------

# Every Turbo Pascal `interrupt` procedure starts by saving registers and loading DS:
#   push ax bx cx dx si di ds es bp / mov bp,sp / [sub sp,n] / mov ax,DSEG / mov ds,ax
# In the unpacked (not yet relocated) file, DSEG is the data segment relative to the image.
ISR_PROLOGUE = re.compile(rb"\x50\x53\x51\x52\x56\x57\x1e\x06\x55\x89\xe5(?:\x83\xec.)?\xb8(..)\x8e\xd8", re.S)


def load_data_segment(exe_path):
    with open(exe_path, "rb") as f:
        d = f.read()
    if d[:2] != b"MZ" or d[0x1C:0x1E] == b"LZ":
        sys.exit(f"{exe_path}: expected an unpacked MZ executable (run tools/unlzexe.py first)")
    image = d[struct.unpack_from("<H", d, 8)[0] * 16:]
    segs = {struct.unpack("<H", m.group(1))[0] for m in ISR_PROLOGUE.finditer(image)}
    if len(segs) != 1:
        sys.exit(f"{exe_path}: could not find a unique data segment (found {sorted(map(hex, segs))})")
    ds = segs.pop()
    return image[ds * 16:], ds


def parse_frame(slot):
    mode, w, h = struct.unpack_from("<3H", slot, 0)
    if mode != 2 or not (0 < w <= 24 and 0 < h <= 24) or 6 + h * ((w * 2 + 7) // 8) > SLOT:
        return None
    # Graph3 stores the bottom row first (PutPic's Y is the bottom row), so flip to top-first.
    return w, h, unpack_2bpp(slot[6:], w, h)[::-1]


def extract_sprites(exe_path, outdir, palette, zoom):
    ds_data, ds = load_data_segment(exe_path)
    os.makedirs(outdir, exist_ok=True)
    cell = 24
    sets = []
    sizes = {}
    for s in range(SETS):
        frames = []
        for fr in range(FRAMES):
            off = (s * FRAMES + fr) * SLOT
            parsed = parse_frame(ds_data[off:off + SLOT])
            if parsed is None:
                print(f"  set {s:2} frame {fr}: no valid GetPic header at DS:{off:04X}")
            frames.append(parsed)
            if parsed:
                sizes[(parsed[0], parsed[1])] = sizes.get((parsed[0], parsed[1]), 0) + 1
        sets.append(frames)

        # One PNG per set: the 4 frames side by side.
        strip = [bytearray([GAP] * (FRAMES * (cell + 1) + 1)) for _ in range(cell + 2)]
        for fr, parsed in enumerate(frames):
            if not parsed:
                continue
            w, h, rows = parsed
            for y in range(h):
                x0 = 1 + fr * (cell + 1)
                strip[1 + y][x0:x0 + w] = rows[y]
                strip[1 + y][x0 + w:x0 + cell] = bytes(cell - w)
            for y in range(h, cell):
                strip[1 + y][1 + fr * (cell + 1):1 + fr * (cell + 1) + cell] = bytes(cell)
        name = os.path.join(outdir, f"set{s:02d}_ds{s * FRAMES * SLOT:04x}.png")
        write_png(name, scale(strip, zoom), len(strip[0]) * zoom, len(strip) * zoom, palette + [GAP_RGB])

    # Contact sheet: two columns of 20 sets, 4 frames each.
    cols, per_col = 2, SETS // 2
    row_w = FRAMES * (cell + 1) + 1
    sheet_w = cols * row_w + (cols - 1) * 4
    sheet_h = per_col * (cell + 1) + 1
    sheet = [bytearray([GAP] * sheet_w) for _ in range(sheet_h)]
    for s, frames in enumerate(sets):
        cx = (s // per_col) * (row_w + 4)
        cy = (s % per_col) * (cell + 1)
        for fr, parsed in enumerate(frames):
            x0 = cx + 1 + fr * (cell + 1)
            for y in range(cell):
                sheet[cy + 1 + y][x0:x0 + cell] = bytes(cell)
            if parsed:
                w, h, rows = parsed
                for y in range(h):
                    sheet[cy + 1 + y][x0:x0 + w] = rows[y]
    sheet_path = os.path.join(outdir, "sprites_sheet.png")
    write_png(sheet_path, scale(sheet, zoom), sheet_w * zoom, sheet_h * zoom, palette + [GAP_RGB])

    print(f"{exe_path}: data segment {ds:04X} (Ghidra {ds + 0x1000:04X}), {SETS} sets x {FRAMES} frames")
    print(f"  frame sizes: {', '.join(f'{w}x{h}: {n}' for (w, h), n in sorted(sizes.items()))}")
    print(f"  wrote {outdir}/set*.png and {sheet_path}")


# --- PCX ------------------------------------------------------------------------------------

def decode_pcx(data):
    (manuf, ver, enc, bpp, x0, y0, x1, y1) = struct.unpack_from("<4B4H", data, 0)
    planes, bpl = data[65], struct.unpack_from("<H", data, 66)[0]
    if manuf != 0x0A or enc != 1:
        raise ValueError("not an RLE PCX file")
    if bpp != 2 or planes != 1:
        raise ValueError(f"unsupported PCX layout: {bpp} bpp x {planes} planes")
    w, h = x1 - x0 + 1, y1 - y0 + 1
    out = bytearray()
    pos = 128
    need = bpl * h
    while len(out) < need and pos < len(data):
        b = data[pos]
        pos += 1
        if b >= 0xC0:
            out += bytes([data[pos]]) * (b & 0x3F)
            pos += 1
        else:
            out.append(b)
    if len(out) < need:
        raise ValueError(f"PCX data ended early ({len(out)} of {need} bytes)")
    rows = [unpack_2bpp(out[y * bpl:(y + 1) * bpl], w, 1)[0] for y in range(h)]
    info = {"version": ver, "size": f"{w}x{h}", "bytes_per_line": bpl,
            "cga_palette_byte": f"{data[19]:#04x}", "background": data[16] >> 4,
            "trailing_bytes": len(data) - pos}
    return w, h, rows, info


def extract_pcx(path, out_png, palette, zoom):
    with open(path, "rb") as f:
        data = f.read()
    w, h, rows, info = decode_pcx(data)
    write_png(out_png, scale(rows, zoom), w * zoom, h * zoom, palette)
    print(f"{path} -> {out_png}")
    for k, v in info.items():
        print(f"  {k:18} {v}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("sprites")
    sp.add_argument("exe")
    sp.add_argument("outdir")
    pp = sub.add_parser("pcx")
    pp.add_argument("pcx")
    pp.add_argument("out")
    for p in (sp, pp):
        p.add_argument("--palette", type=int, choices=(0, 1), default=1,
                       help="CGA palette: 1 = cyan/magenta/white (the game's own), 0 = green/red/brown")
        p.add_argument("--bright", action="store_true", help="use the high-intensity colours")
        p.add_argument("--zoom", type=int, default=None, help="pixel scale (default 4 sprites, 2 pcx)")
    args = ap.parse_args()
    palette = PALETTES[(args.palette, args.bright)]

    if args.cmd == "sprites":
        extract_sprites(args.exe, args.outdir, palette, args.zoom or 4)
    else:
        extract_pcx(args.pcx, args.out, palette, args.zoom or 2)


if __name__ == "__main__":
    main()
