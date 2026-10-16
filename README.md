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
score and level changes, and **all ten enemy types move and fight**: platforms, walkers you
can spear, pushable blocks, vertical hazards, elevators, ceiling bats, dart traps, plus the
freeze pickup. **All the original PC-speaker sound effects** play (E toggles them, as in the
game). The **status bar, prompts, level titles and game-over screen** are drawn on screen as
in the original, using the game's own fonts from the EXE plus the port's own 8x8 font for
plain ASCII (standing in for the IBM BIOS font, which can't be shipped). It **starts like the
original**: the title picture and walking demo, then the main menu with instructions, story,
ordering info, keyboard setup, joystick screen and high scores (read from and saved to the
game's own `PTOMBn.HS`, so existing scores carry over). Saving and restoring games (S / R) works with the original's `.SAV` files. The end-of-volume pages run after level 20. **Game controllers**
work (see below). Not yet: the exact Genus picture-transition effects (approximated as wipes).

The port reads `PTOMBn.MNU`, `PTOMBn.HS`, `PTOMBn.SAV` and `PTOMB.CFG` from the folder the EXE is in, and
**writes** `PTOMBn.HS` (a new high score), `PTOMB.CFG` (Config Keyboard) and `PTOMBn.SAV`
(S in game) there, as the
original does.

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
build/ptomb.exe game/PTOMB1.EXE
```

Menu keys as in the original: E, Space or Enter to play, I instructions, S story, H high scores,
O ordering info, C config keyboard, J joystick, Q quit.

Game keys (the original's):

| Key | Action |
|-----|--------|
| Left / Right | walk (pressing against your facing first turns you round) |
| Ctrl / Alt | walk left / right, the alternate keys (`--keys 1`: Shift, `--keys 2`: Alt / Ctrl) |
| Space | jump |
| Z X C V B N M , . / F | throw a spear |
| P | pause |
| S / R | save / restore (the save holds the state the current level started with) |
| E | sound effects on/off |
| Q or Esc, then Y | quit to the main menu |
| Scroll Lock on + `+` | cheat: skip to the next level |

Game controllers stand in for the original's PC joystick. Most current pads (Xbox,
PlayStation, Switch Pro, 8BitDo and others) are recognised automatically and can be plugged in
at any time; for anything else, put a [`gamecontrollerdb.txt`](https://github.com/mdqinc/SDL_GameControllerDB)
next to `ptomb.exe`, and an unrecognised joystick still works through its first two axes and
buttons. Unlike the original, the joystick starts switched on; J in the main menu turns it
off or recalibrates it, as in the game.

| Control | In game | Menus and "press any key" | QUIT (Y/N)? |
|---------|---------|---------------------------|-------------|
| Left stick / d-pad | walk | | |
| A | jump | Enter | Y |
| B or X | throw a spear | B: Esc | B: N |
| Start | pause | Enter | Y |
| Back / View | quit prompt | | N |

As in the original, the joystick's fire button doesn't check your spear count, so it keeps
throwing at zero and the count goes negative.

Port keys:

| Key | Action |
|-----|--------|
| PgUp / PgDn | previous / next level |
| F1 | CGA palette 0 (green/red/brown, default) or 1 (cyan/magenta/white, the original) |
| F2 | low / high intensity |
| F3 | 4:3 aspect correction on/off |
| F12 | save a screenshot PNG in the current directory |

Command line: `--level N` (skip the title and menu and play that level, then exit),
`--palette 0|1`, `--bright`, `--keys 1|2|3` (override PTOMB.CFG), `--cheat` (the menu cheat:
invincible, unlimited spears), `--screenshot out.png` (render one level to a file and exit),
`--sound-test` (play every sound effect once, named in the title bar), `--ending` (show this
volume's ending pages), `--debug-exe` (print
what the port found in the EXE), and two testing aids: `--script "..."` (scripted input, e.g.
`--script "wait 70; down right; wait 18; up right; shot a.png; quit"`, times in 18.2 Hz ticks;
`pad_a`, `pad_b`, `pad_x`, `pad_start`, `pad_back` and `pad_left` / `right` / `up` / `down`
drive a virtual controller)
and `--record-audio out.wav` (capture everything the speaker plays).

## Reverse engineering workflow

The shipped `PTOMBn.EXE` files are compressed with LZEXE 0.91, so Ghidra only sees the
decompression stub. Unpack them first:

```
python tools/unlzexe.py game/PTOMB1.EXE unpacked/PTOMB1.EXE
```

Then import `unpacked/PTOMB1.EXE` into Ghidra (format: Old-style DOS Executable (MZ)), run
auto-analysis, and apply the known names with
`python tools/ghidra_rename.py analysis/PTOMB1.names.txt` (needs GhidraMCP running).
