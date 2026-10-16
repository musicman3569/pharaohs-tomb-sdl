# Pharaoh's Tomb port

A personal learning project: reverse engineering Pharaoh's Tomb (Micro/FX / Apogee, 1990,
re-released as freeware in 2009) with Ghidra, then porting it to run natively on modern systems.

## Layout

| Path        | Contents                                                        |
|-------------|-----------------------------------------------------------------|
| `game/`     | Original game files (not committed, copy them in yourself)      |
| `unpacked/` | Decompressed EXEs produced by `tools/unlzexe.py` (not committed) |
| `tools/`    | Helper scripts                                                  |
| `docs/`     | Reverse engineering notes                                       |

## Step 1: unpack the executables

The shipped `PTOMBn.EXE` files are compressed with LZEXE 0.91, so Ghidra only sees the
decompression stub. Unpack them first:

```
python tools/unlzexe.py game/PTOMB1.EXE unpacked/PTOMB1.EXE
```

Then import `unpacked/PTOMB1.EXE` into Ghidra (format: Old-style DOS Executable (MZ)) and run
auto-analysis.
