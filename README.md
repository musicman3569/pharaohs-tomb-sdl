# Pharaoh's Tomb port

A personal learning project: reverse engineering Pharaoh's Tomb (Micro/FX / Apogee, 1990,
re-released as freeware in 2009) with Ghidra, then porting it to run natively on modern systems.

## Layout

| Path        | Contents                                                        |
|-------------|-----------------------------------------------------------------|
| `src/`      | The port: C11 + SDL2                                            |
| `game/`     | Original game files (not committed, copy them in yourself)      |
| `unpacked/` | Decompressed EXEs produced by `tools/unlzexe.py` (not committed) |
| `tools/`    | Python helpers: unpacker, Ghidra dump/rename, graphics and level extractors |
| `analysis/` | Ghidra function names (`PTOMB1.names.txt`)                      |
| `docs/`     | Reverse engineering notes (`notes.md`)                          |

## The port

The port contains no game assets. At startup it reads the sprites and level layouts out of
your own `PTOMBn.EXE`, packed or unpacked, and reimplements the game logic in C on top of a
Graph3-style 320x200 CGA framebuffer.

So far it's a **level viewer**: it draws any level exactly as the game's StartLevel does and
runs the 18.2 Hz timer the game uses for pacing. No gameplay yet.

### Build (Windows, MSYS2 mingw64)

Needs the MSYS2 packages `mingw-w64-x86_64-gcc`, `-cmake`, `-ninja` and `-SDL2`. From a shell
with `C:\msys64\mingw64\bin` on `PATH`:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That produces `build/ptomb.exe` with `SDL2.dll` copied next to it. On Linux/macOS the same
commands work with SDL2 installed through your package manager.

### Run

```
build/ptomb.exe game/PTOMB1.EXE --level 1
```

| Key | Action |
|-----|--------|
| Left / Right, PgUp / PgDn | previous / next level |
| F1 | CGA palette 0 (green/red/brown, default) or 1 (cyan/magenta/white, the original) |
| F2 | low / high intensity |
| F3 | 4:3 aspect correction on/off |
| F12 | save a screenshot PNG in the current directory |
| Esc | quit |

`--palette 0|1`, `--bright` and `--screenshot out.png` (render one level to a file and exit)
are also available on the command line.

## Reverse engineering workflow

The shipped `PTOMBn.EXE` files are compressed with LZEXE 0.91, so Ghidra only sees the
decompression stub. Unpack them first:

```
python tools/unlzexe.py game/PTOMB1.EXE unpacked/PTOMB1.EXE
```

Then import `unpacked/PTOMB1.EXE` into Ghidra (format: Old-style DOS Executable (MZ)), run
auto-analysis, and apply the known names with
`python tools/ghidra_rename.py analysis/PTOMB1.names.txt` (needs GhidraMCP running).
