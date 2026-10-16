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

What works so far: levels are drawn exactly as the game's StartLevel does, and **the player is
playable**. You can walk, jump, fall, throw spears, and pick up or trigger every tile type
(keys, locks, exit doors, treasure, hidden bricks, switches, spikes, the exit), with lives,
score and level changes. Not yet: enemies moving (they're drawn but stand still), sound
effects (direct speaker beeps work), the status bar text (shown in the window title instead),
the menus, saving, and the joystick.

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

Game keys (the original's):

| Key | Action |
|-----|--------|
| Left / Right | walk (pressing against your facing first turns you round) |
| Ctrl / Alt | walk left / right, the alternate keys (`--keys 1`: Shift, `--keys 2`: Alt / Ctrl) |
| Space | jump |
| Z X C V B N M , . / F | throw a spear |
| P | pause |
| E | sound effects on/off |
| Q or Esc, then Y | quit (starts a new game; there's no menu yet) |
| Scroll Lock on + `+` | cheat: skip to the next level |

Port keys:

| Key | Action |
|-----|--------|
| PgUp / PgDn | previous / next level |
| F1 | CGA palette 0 (green/red/brown, default) or 1 (cyan/magenta/white, the original) |
| F2 | low / high intensity |
| F3 | 4:3 aspect correction on/off |
| F12 | save a screenshot PNG in the current directory |

Command line: `--level N`, `--palette 0|1`, `--bright`, `--keys 1|2|3`, `--cheat` (the
menu cheat: invincible, unlimited spears), `--screenshot out.png` (render one level to a file and
exit) and `--script "..."` (scripted input for testing, e.g.
`--script "wait 70; down right; wait 18; up right; shot a.png; quit"`, times in 18.2 Hz ticks).

## Reverse engineering workflow

The shipped `PTOMBn.EXE` files are compressed with LZEXE 0.91, so Ghidra only sees the
decompression stub. Unpack them first:

```
python tools/unlzexe.py game/PTOMB1.EXE unpacked/PTOMB1.EXE
```

Then import `unpacked/PTOMB1.EXE` into Ghidra (format: Old-style DOS Executable (MZ)), run
auto-analysis, and apply the known names with
`python tools/ghidra_rename.py analysis/PTOMB1.names.txt` (needs GhidraMCP running).
