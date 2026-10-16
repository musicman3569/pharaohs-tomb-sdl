#!/usr/bin/env python3
"""Render Pharaoh's Tomb levels to PNG, reading everything straight from an unpacked EXE.

Usage: python render_levels.py UNPACKED.EXE OUTDIR [--palette 0|1] [--bright] [--zoom N]
       [--table]

How the game stores levels (see docs/notes.md):
  * StartLevel (main program) has one case per level. Each case calls
    DrawRow(row, s) twelve times, where s is a string[20] constant in the code segment:
        mov ax,ROW / push ax / mov di,STR / push cs / push di / call DrawRow
  * DrawRow walks the 20 characters. For each one it runs a long chain of
        mov di,CHAR / push cs / push di / callf StrCompare / jnz next
        ...actions...
    and the actions are PutPic(buffer, x, y) calls, a store into the 12x20 tile map
    (mov byte [di+MAP], code), and sometimes a call that spawns an enemy.
  * Sprite buffers are BSS copies made at startup by the sprite units:
        mov di,DST / push ds / push di / mov di,SRC / push ds / push di / push cs / call Copy
  * PutPic's Y is the bottom row of the picture, so tile (row, col) covers
    x = (col-1)*16 .. +15 and y = row*16-15 .. row*16.

Nothing here is specific to volume 1: addresses are found by pattern, so all four EXEs work.
"""

import argparse
import collections
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_gfx import PALETTES, load_data_segment, parse_frame, scale, write_png  # noqa: E402

COLS, ROWS, TILE = 20, 12, 16
SET_BYTES = 0x270


def load_image(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:2] != b"MZ" or d[0x1C:0x1E] == b"LZ":
        sys.exit(f"{path}: expected an unpacked MZ executable (run tools/unlzexe.py first)")
    h = struct.unpack_from("<14H", d, 0)
    return d[h[4] * 16:], h[11] * 16 + h[10]  # image, linear entry point


def s16(v):
    return v - 0x10000 if v >= 0x8000 else v


# --- Level rows ---------------------------------------------------------------------------

ROW_CALL = re.compile(rb"\xB8(.)\x00\x50\xBF(..)\x0E\x57\xE8(..)", re.S)


def find_levels(image, entry):
    """Return (draw_row_offset, [level -> [12 row strings]]). Main program code is segment 0."""
    main = image[:entry + 0x200]
    calls = []
    for m in ROW_CALL.finditer(main):
        row = m.group(1)[0]
        s = struct.unpack("<H", m.group(2))[0]
        target = (m.end() + s16(struct.unpack("<H", m.group(3))[0])) & 0xFFFF
        calls.append((target, row, s))
    draw_row = collections.Counter(t for t, _, _ in calls).most_common(1)[0][0]
    levels, cur = [], []
    for target, row, s in calls:
        if target != draw_row:
            continue
        if row == 1 and cur:
            levels.append(cur)
            cur = []
        n = image[s]
        cur.append(image[s + 1:s + 1 + n].decode("latin-1").ljust(COLS)[:COLS])
    if cur:
        levels.append(cur)
    bad = [i + 1 for i, lv in enumerate(levels) if len(lv) != ROWS]
    if bad:
        sys.exit(f"levels {bad} do not have {ROWS} rows; pattern match is off")
    return draw_row, levels


# --- Tile table from DrawRow ----------------------------------------------------------------

# "jnz next" is a short jump, or "jz +3 / jmp near next" when the actions are too long.
CMP_BLOCK = re.compile(rb"\xBF(..)\x0E\x57\x9A(....)(?:\x75(.)|\x74\x03\xE9(..))", re.S)
PUSH_BUF = re.compile(rb"\xBF(..)\x1E\x57", re.S)
FAR_CALL = re.compile(rb"\x9A(..)(..)", re.S)
MAP_STORE = re.compile(rb"\xC6\x85(..)(.)", re.S)
ADD_DI = re.compile(rb"\x81\xC7(..)", re.S)  # buffer computed as (Random(4)+1)*0x9C + base


def find_tiles(image, draw_row):
    """Return {char: {"bufs": [...], "random_base": int|None, "map": int|None, "calls": [...]}}."""
    code = image[draw_row:draw_row + 0x2000]
    blocks = []
    for m in CMP_BLOCK.finditer(code):
        s = struct.unpack("<H", m.group(1))[0]
        if image[s] != 1:
            continue
        if m.group(3) is not None:
            rel = m.group(3)[0]
            end = m.end() + (rel - 0x100 if rel & 0x80 else rel)
        else:
            end = m.end() + s16(struct.unpack("<H", m.group(4))[0])
        blocks.append((chr(image[s + 1]), code[m.end():end], m.group(2)))
    if not blocks:
        sys.exit("no character comparisons found in DrawRow")
    strcmp = collections.Counter(b[2] for b in blocks).most_common(1)[0][0]
    blocks = [b for b in blocks if b[2] == strcmp]

    # PutPic is the far call that follows a pushed buffer; take the most common such target.
    pp = collections.Counter()
    for _, body, _ in blocks:
        for m in PUSH_BUF.finditer(body):
            c = FAR_CALL.search(body, m.end())
            if c:
                pp[c.group(0)] += 1
    putpic = pp.most_common(1)[0][0]
    map_disp = collections.Counter(
        m.group(1) for _, body, _ in blocks for m in MAP_STORE.finditer(body)).most_common(1)[0][0]

    # Byte runs that merely start with 9A look like far calls; only trust targets in a segment
    # the main program really calls often.
    seg_counts = collections.Counter(m.group(2) for m in FAR_CALL.finditer(image[:draw_row + 0x8000]))
    real_segs = {s for s, n in seg_counts.items() if n >= 20}

    tiles = {}
    for ch, body, _ in blocks:
        bufs = [struct.unpack("<H", m.group(1))[0] for m in PUSH_BUF.finditer(body)]
        n_putpic = body.count(putpic)
        random_base = None
        if n_putpic > len(bufs):  # a PutPic whose buffer is computed, not a constant
            m = ADD_DI.search(body)
            if m:
                random_base = struct.unpack("<H", m.group(1))[0]
        maps = [m.group(2)[0] for m in MAP_STORE.finditer(body) if m.group(1) == map_disp]
        far = [m for m in FAR_CALL.finditer(body)
               if m.group(0) not in (putpic, strcmp) and m.group(2) in real_segs]
        calls = sorted({f"{struct.unpack('<H', m.group(2))[0]:04X}:{struct.unpack('<H', m.group(1))[0]:04X}"
                        for m in far})
        targets = [struct.unpack("<H", m.group(2))[0] * 16 + struct.unpack("<H", m.group(1))[0] for m in far]
        enemy = None
        for m in far:
            e = parse_spawn_args(body, m.start())
            if e:
                enemy = e
        tiles[ch] = {"bufs": bufs, "random_base": random_base, "map": maps[-1] if maps else None,
                     "calls": calls, "targets": targets, "enemy": enemy}
    return tiles


def parse_spawn_args(body, p):
    """Read SpawnEnemy(type, x, y, speed, dx, dy, flag: byte) arguments backwards from a call.

    The call ends with:  [mov ax,speed|xor ax,ax] push ax  x3 (speed, dx, dy)  mov al,flag push ax
    and starts with:     mov ax,TYPE push ax  mov ax,[col] ...   (x and y are computed)
    """
    q = p
    if q < 3 or body[q - 3] != 0xB0 or body[q - 1] != 0x50:
        return None
    q -= 3
    vals = []
    for _ in range(3):
        if q >= 4 and body[q - 4] == 0xB8 and body[q - 1] == 0x50:
            vals.append(s16(struct.unpack("<H", body[q - 3:q - 1])[0]))
            q -= 4
        elif body[q - 3:q] == b"\x31\xC0\x50":
            vals.append(0)
            q -= 3
        else:
            return None
    dy, dx, speed = vals
    i = body.rfind(b"\x50\xA1", 0, q)
    if i < 3 or body[i - 3] != 0xB8:
        return None
    return {"type": struct.unpack("<H", body[i - 2:i])[0], "speed": speed, "dx": dx, "dy": dy}


# --- Per-level monster sprite (DrawSpecialTile) ---------------------------------------------

# cmp ax,LEVEL / jnz .. / mov ax,[bp+0Ah] / mov dx,9Ch / mul dx / mov di,ax / add di,BASE
SPECIAL_CASE = re.compile(rb"\x3D(..)\x75.\x8B\x46\x0A\xBA\x9C\x00\xF7\xE2\x8B\xF8\x81\xC7(..)", re.S)


def find_special(image, tiles):
    """Return (function offset, {level: base}) for the routine that draws 's'/'S' tiles."""
    for t in {x for v in tiles.values() for x in v["targets"]}:
        cases = {struct.unpack("<H", m.group(1))[0]: struct.unpack("<H", m.group(2))[0]
                 for m in SPECIAL_CASE.finditer(image[t:t + 0x400])}
        if len(cases) >= 10:
            return t, cases
    return None, {}


# --- Sprite buffers -------------------------------------------------------------------------

SPRITE_COPY = re.compile(rb"\xBF(..)\x1E\x57\xBF(..)\x1E\x57\x0E\xE8", re.S)


def find_sprite_copies(image):
    """Return [(bss_dst, ds_src)] for every sprite set copied at startup."""
    return [(struct.unpack("<H", m.group(1))[0], struct.unpack("<H", m.group(2))[0])
            for m in SPRITE_COPY.finditer(image)]


def buffer_to_ds(buf, copies):
    for dst, src in copies:
        if dst <= buf < dst + SET_BYTES:
            return src + (buf - dst)
    return None


def describe(off):
    return f"set {off // SET_BYTES:2} frame {(off % SET_BYTES) // 0x9C}"


# --- Rendering ------------------------------------------------------------------------------

def render(level, tiles, ds_data, copies, level_no, special):
    special_fn, special_bases = special
    fb = [bytearray(320) for _ in range(200)]
    unknown = set()

    def blit(off, x, top):
        parsed = parse_frame(ds_data[off:off + 0x9C])
        if not parsed:
            return False
        w, h, rows = parsed
        for y in range(h):
            if 0 <= top + y < 200:
                fb[top + y][x:x + w] = rows[y][:max(0, 320 - x)]
        return True

    for r, line in enumerate(level, start=1):
        for c, ch in enumerate(line, start=1):
            if ch == " ":
                continue
            x, top = (c - 1) * TILE, r * TILE - (TILE - 1)
            t = tiles.get(ch)
            drawn = False
            if t:
                for buf in t["bufs"]:
                    off = buffer_to_ds(buf, copies)
                    drawn |= off is not None and blit(off, x, top)
                if t["random_base"] is not None:
                    # The game picks one of 4 frames at random; use a fixed choice per cell.
                    pick = (r * 7 + c * 3 + level_no) % 4
                    off = buffer_to_ds((t["random_base"] + (pick + 1) * 0x9C) & 0xFFFF, copies)
                    drawn |= off is not None and blit(off, x, top)
                if special_fn in t["targets"] and level_no in special_bases:
                    # This level's monster, first animation frame (DrawSpecialTile frame 1).
                    off = buffer_to_ds((special_bases[level_no] + 0x9C) & 0xFFFF, copies)
                    drawn |= off is not None and blit(off, x, top)
            if not drawn and (not t or t["calls"]):
                # Drawn by other code (or unknown): outline the cell so it is visible.
                unknown.add(ch)
                for i in range(TILE):
                    for px, py in ((x + i, top), (x + i, top + TILE - 1), (x, top + i), (x + TILE - 1, top + i)):
                        if (i // 2) % 2 == 0 and py < 200:
                            fb[py][px] = 3
    # StartLevel then draws a full-width line at y=189 (status bar border).
    fb[189][:] = bytes([3]) * 320
    for y in range(190, 200):
        fb[y][:] = bytes(320)
    return fb, unknown


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("exe")
    ap.add_argument("outdir")
    ap.add_argument("--palette", type=int, choices=(0, 1), default=1)
    ap.add_argument("--bright", action="store_true")
    ap.add_argument("--zoom", type=int, default=2)
    ap.add_argument("--table", action="store_true", help="print the tile table and level text")
    args = ap.parse_args()
    palette = PALETTES[(args.palette, args.bright)]

    image, entry = load_image(args.exe)
    ds_data, _ = load_data_segment(args.exe)
    draw_row, levels = find_levels(image, entry)
    tiles = find_tiles(image, draw_row)
    copies = find_sprite_copies(image)
    special = find_special(image, tiles)

    if args.table:
        if special[0] is not None:
            per_level = []
            for lv, base in sorted(special[1].items()):
                o = buffer_to_ds((base + 0x9C) & 0xFFFF, copies)
                per_level.append(f"{lv}:{o // SET_BYTES if o is not None else '?'}")
            print(f"Level monster (s/S) sprite set by level: {' '.join(per_level)}")
        print(f"DrawRow at 0000:{draw_row:04X}, {len(tiles)} tile characters, "
              f"{len(copies)} sprite sets, {len(levels)} levels")
        for ch, t in tiles.items():
            sprites = [describe(o) if (o := buffer_to_ds(b, copies)) is not None else f"buf {b:04X}"
                       for b in t["bufs"]]
            if t["random_base"] is not None:
                o = buffer_to_ds((t["random_base"] + 0x9C) & 0xFFFF, copies)
                sprites.append(f"random frame of set {o // SET_BYTES}" if o is not None else "random")
            if special[0] in t["targets"]:
                sprites.append("level monster")
            m = "--" if t["map"] is None else f"{t['map']:02X}"
            e = t["enemy"]
            et = (f"  enemy type {e['type']} speed {e['speed']} dx {e['dx']:+d} dy {e['dy']:+d}"
                  if e else "")
            print(f"  {ch!r:5} map {m}  {'; '.join(sprites) or '(no sprite)'}{et}"
                  f"{'  calls ' + ', '.join(t['calls']) if t['calls'] else ''}")

    os.makedirs(args.outdir, exist_ok=True)
    sheet_cols = 4
    sheet = [bytearray(sheet_cols * 322) for _ in range(((len(levels) + sheet_cols - 1) // sheet_cols) * 202)]
    all_unknown = set()
    for i, level in enumerate(levels, start=1):
        fb, unknown = render(level, tiles, ds_data, copies, i, special)
        all_unknown |= unknown
        write_png(os.path.join(args.outdir, f"level{i:02d}.png"), scale(fb, args.zoom),
                  320 * args.zoom, 200 * args.zoom, palette)
        with open(os.path.join(args.outdir, f"level{i:02d}.txt"), "w", encoding="latin-1") as f:
            f.write("\n".join(level) + "\n")
        sx, sy = ((i - 1) % sheet_cols) * 322 + 1, ((i - 1) // sheet_cols) * 202 + 1
        for y in range(200):
            sheet[sy + y][sx:sx + 320] = fb[y]
        if args.table:
            print(f"\nlevel {i}:")
            for line in level:
                print(f"  |{line}|")
    write_png(os.path.join(args.outdir, "levels_sheet.png"), sheet, len(sheet[0]), len(sheet), palette)
    print(f"{args.exe}: {len(levels)} levels -> {args.outdir}/level*.png, levels_sheet.png")
    if all_unknown:
        print(f"  outlined (drawn by other code): {' '.join(sorted(all_unknown))}")


if __name__ == "__main__":
    main()
