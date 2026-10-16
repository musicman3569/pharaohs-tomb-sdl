#!/usr/bin/env python3
"""Decompress a DOS MZ executable packed with LZEXE 0.90 / 0.91.

Usage: python unlzexe.py PACKED.EXE OUTPUT.EXE

How LZEXE works:
  The packed EXE's load module holds the LZ77-compressed original image, followed by a
  small decompressor ("loader") segment. The MZ header's CS:IP points at the loader. The
  loader starts with a table of the original program's registers and sizes, then its code,
  then a compressed relocation table. At run time it decompresses the image in place,
  applies the relocations, and jumps to the original CS:IP.

  This script does the same work offline and writes a normal, uncompressed MZ executable.
  The format follows Mitugu (Kou) Suzuki's UNLZEXE.
"""

import argparse
import struct
import sys

PARA = 16


class BitReader:
    """LZEXE's control bit stream: 16-bit little-endian words read LSB first, interleaved
    with the literal and offset bytes in the same input stream."""

    def __init__(self, data, pos):
        self.data = data
        self.pos = pos
        self.buf = self.word()
        self.count = 16

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b

    def word(self):
        w = struct.unpack_from("<H", self.data, self.pos)[0]
        self.pos += 2
        return w

    def bit(self):
        b = self.buf & 1
        self.count -= 1
        if self.count == 0:
            # The next control word is fetched as soon as the current one runs out,
            # before any data bytes that follow this bit.
            self.buf = self.word()
            self.count = 16
        else:
            self.buf >>= 1
        return b


def decompress(data, start):
    """Decompress the image starting at file offset `start`. Returns (bytes, end_offset)."""
    r = BitReader(data, start)
    out = bytearray()
    while True:
        if r.bit():
            out.append(r.byte())  # literal
            continue
        if not r.bit():
            # Short match: 2-bit length (2..5), 8-bit offset (-256..-1).
            length = ((r.bit() << 1) | r.bit()) + 2
            span = r.byte() | 0xFF00
        else:
            # Long match: 13-bit offset (-8192..-1), 3-bit length (3..9) or extended length.
            lo = r.byte()
            hi = r.byte()
            span = lo | ((hi & ~0x07) << 5) | 0xE000
            length = (hi & 0x07) + 2
            if length == 2:
                length = r.byte()
                if length == 0:
                    break  # end of compressed image
                if length == 1:
                    continue  # segment-change marker; the DOS loader uses it to renormalize pointers
                length += 1
        span -= 0x10000  # signed 16-bit backward distance
        for _ in range(length):
            out.append(out[span])
    return bytes(out), r.pos


def read_relocs_091(data, pos):
    """LZEXE 0.91 relocation table: deltas packed as bytes, with escapes."""
    relocs = []
    seg = off = 0
    while True:
        span = data[pos]
        pos += 1
        if span == 0:
            span = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            if span == 0:
                seg += 0x0FFF  # skip ahead without emitting an entry
                continue
            if span == 1:
                break  # end of table
        off += span
        seg += (off & ~0x0F) >> 4
        off &= 0x0F
        relocs.append((off, seg))
    return relocs


def read_relocs_090(data, pos):
    """LZEXE 0.90 relocation table: 16 groups (one per 0x1000-paragraph segment) of
    count + list of offsets."""
    relocs = []
    for seg in range(0, 0x10000, 0x1000):
        count = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        for _ in range(count):
            off = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            relocs.append((off, seg))
    return relocs


def unpack(data):
    if data[:2] != b"MZ":
        raise ValueError("not an MZ executable")
    h = list(struct.unpack_from("<14H", data, 0))
    sig = data[0x1C:0x20]
    if sig == b"LZ91":
        ver = 91
    elif sig == b"LZ09":
        ver = 90
    else:
        raise ValueError(f"no LZEXE signature at 0x1C (found {sig!r})")

    header_bytes = h[4] * PARA
    file_size = (h[2] - 1) * 512 + (h[1] or 512)
    in_load_paras = (file_size - header_bytes + PARA - 1) // PARA

    loader = header_bytes + h[11] * PARA  # file offset of the decompressor segment (CS)
    ip, cs, sp, ss, comp_paras, inc_paras, loader_size = struct.unpack_from("<7H", data, loader)

    image, end = decompress(data, header_bytes)

    reloc_pos = loader + (0x158 if ver == 91 else 0x19D)
    relocs = (read_relocs_091 if ver == 91 else read_relocs_090)(data, reloc_pos)

    # New header: 0x1C bytes of fields, then the relocation table, padded to 512 bytes.
    reloc_table_off = 0x1C
    hdr_len = reloc_table_off + 4 * len(relocs)
    hdr_len = (hdr_len + 0x1FF) & ~0x1FF
    out_load_paras = (len(image) + PARA - 1) // PARA

    # Give the unpacked program the same total memory the packed one asked for. That is
    # always enough, because the original program ran inside that same allocation.
    min_alloc = max(0, in_load_paras + h[5] - out_load_paras)
    max_alloc = 0xFFFF if h[6] == 0xFFFF else max(min_alloc, in_load_paras + h[6] - out_load_paras)

    total = hdr_len + len(image)
    hdr = bytearray(hdr_len)
    struct.pack_into(
        "<14H", hdr, 0,
        0x5A4D,                   # 'MZ'
        total % 512,              # bytes on last page
        (total + 511) // 512,     # pages
        len(relocs),
        hdr_len // PARA,          # header size in paragraphs
        min_alloc,
        max_alloc,
        ss, sp,
        0,                        # checksum
        ip, cs,
        reloc_table_off,
        0,                        # overlay number
    )
    for i, (off, seg) in enumerate(relocs):
        struct.pack_into("<HH", hdr, reloc_table_off + 4 * i, off, seg)

    info = {
        "version": "0.91" if ver == 91 else "0.90",
        "entry": f"{cs:04X}:{ip:04X}",
        "stack": f"{ss:04X}:{sp:04X}",
        "packed_image": end - header_bytes,
        "unpacked_image": len(image),
        "relocations": len(relocs),
        "min_alloc_paras": min_alloc,
        "total_memory": (out_load_paras + min_alloc) * PARA,
        "packed_total_memory": (in_load_paras + h[5]) * PARA,
        "loader_offset": loader,
        "loader_size_field": loader_size,
        "comp_paras_field": comp_paras,
        "inc_paras_field": inc_paras,
    }

    # Sanity checks: the compressed stream must end before the loader, and the
    # original entry point and stack must lie inside the program's memory.
    if end > loader:
        raise ValueError(f"compressed stream overran the loader ({end:#x} > {loader:#x})")
    if cs * PARA + ip >= len(image):
        raise ValueError("original entry point is outside the unpacked image")
    if ss * PARA + sp > info["total_memory"]:
        raise ValueError("original stack is outside the program's memory")
    for off, seg in relocs:
        if seg * PARA + off + 2 > len(image):
            raise ValueError(f"relocation {seg:04X}:{off:04X} is outside the unpacked image")

    return bytes(hdr) + image, info


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input")
    ap.add_argument("output")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()
    try:
        out, info = unpack(data)
    except ValueError as e:
        sys.exit(f"{args.input}: {e}")
    with open(args.output, "wb") as f:
        f.write(out)

    print(f"{args.input} -> {args.output} ({len(data)} -> {len(out)} bytes)")
    for k, v in info.items():
        print(f"  {k:20} {v}")


if __name__ == "__main__":
    main()
