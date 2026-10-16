# Reverse engineering notes

Addresses are as Ghidra shows them for `unpacked/PTOMB1.EXE` (load base segment 1000).
Function names live in `analysis/PTOMB1.names.txt`; apply them with `tools/ghidra_rename.py`.

## Executables

- `PTOMB1..4.EXE` (one per volume) are packed with **LZEXE 0.91**. `tools/unlzexe.py` unpacks them.
- PTOMB1 unpacked: 128,816-byte image, 1,399 relocations, entry `1000:8473`.
- All four have the same relocation count and almost the same size, so they're likely the same
  engine with different level data built in.

## Compiler and libraries

Turbo Pascal (main program first, then units, then System, then DS). Strings are
length-prefixed. Segment map:

| Segment | Size  | Unit                         | Evidence                                          |
|---------|-------|------------------------------|---------------------------------------------------|
| 1000    | 34 KB | main program                 | entry point, menu, game loop                      |
| 1854    | 27 KB | game unit                    | high scores, config, text screens, timer ISR      |
| 1F11    | 464 B | sprite data A                | init copies 22 sprite sets out of DS              |
| 1F2E    | 400 B | sprite data B                | init copies 18 sprite sets out of DS              |
| 1F47    | 5 KB  | **Graph3** (TP3 graphics)    | GraphColorMode = BIOS mode 4, palette 1           |
| 2081    | 1.5 KB| **Crt**                      | INT 16h, PC speaker ports 42h/43h/61h             |
| 20E3    | 288 B | joystick                     | port 201h                                         |
| 20F5    | 175 B | **Dos** (only Get/SetIntVec) | INT 21h AH=35h/25h                                |
| 20FF    | 8 KB  | Genus **PCX Effects 1.04**   | copyright string, EMS, timer                      |
| 22F9    | 16 KB | Genus **PCX Programmer's Toolkit 3.56** | copyright, adapter detection, EMS, printer |
| 2714    | 4 KB  | **System** RTL               | only INT 21h                                      |
| 2825    | DS    | data segment                 | 0000-74DF initialized, then BSS                   |

## Graphics

- **CGA 320x200, 4 colors**, palette 1 (cyan/magenta/white), drawn through Graph3.
- Sprites: Graph3 `GetPic` format, header `word mode=2, word w, word h`, then 2 bpp rows
  (MSB = leftmost pixel, 4 bytes per 16-px row). **All 160 frames are 16x16.** Each frame sits
  in a 0x9C-byte slot (room for 24x24) padded with spaces.
- 40 sets x 4 slots live in DS:0000-6180 and are copied into BSS at startup by
  `InitSprites_A/B`. For enemies and effects the 4 slots are animation frames; tile sets
  (bricks, slopes, items) hold 4 unrelated tiles, and some slots are empty.
- Code refers to sprites by their BSS copy address. For example GameTick draws the player from
  8D3A (set 3) or 8FAA (set 2). See the table below.
- All four volumes have identical sprites; only the data segment base differs
  (vol1 1825, vol2 182B, vol3/4 1820, in unpacked-file segments).
- Full-screen images (`PTOMBn.MNU`) are **PCX** files (v5, 320x200, 2 bpp, 80 bytes/line,
  palette byte 0x40 = palette 1, low intensity). They decode with no trailing bytes.
- Extract everything with `tools/extract_gfx.py` (output goes to `gfx/`, not committed).
  `--palette 0` renders in CGA palette 0 (green/red/brown) instead of the game's palette 1;
  that's the preferred look for this project, so the port should offer it as an option.

### Sprite set -> BSS address

| Set | DS src | BSS  | | Set | DS src | BSS  | | Set | DS src | BSS  | | Set | DS src | BSS  |
|-----|--------|------|-|-----|--------|------|-|-----|--------|------|-|-----|--------|------|
| 0   | 0000   | 8ACA | | 10  | 1860   | 9BDA | | 20  | 30C0   | AF5A | | 30  | 4920   | CB62 |
| 1   | 0270   | 885A | | 11  | 1AD0   | 9E4A | | 21  | 3330   | B43A | | 31  | 4B90   | CDD2 |
| 2   | 04E0   | 8FAA | | 12  | 1D40   | A0BA | | 22  | 35A0   | B7E2 | | 32  | 4E00   | D042 |
| 3   | 0750   | 8D3A | | 13  | 1FB0   | A32A | | 23  | 3810   | BA52 | | 33  | 5070   | D2B2 |
| 4   | 09C0   | 810A | | 14  | 2220   | A59A | | 24  | 3A80   | BCC2 | | 34  | 52E0   | D522 |
| 5   | 0C30   | 837A | | 15  | 2490   | A80A | | 25  | 3CF0   | BF32 | | 35  | 5550   | D792 |
| 6   | 0EA0   | 85EA | | 16  | 2700   | AA7A | | 26  | 3F60   | C1A2 | | 36  | 57C0   | DA02 |
| 7   | 1110   | 948A | | 17  | 2970   | 921A | | 27  | 41D0   | C412 | | 37  | 5A30   | DC72 |
| 8   | 1380   | 96FA | | 18  | 2BE0   | ACEA | | 28  | 4440   | C682 | | 38  | 5CA0   | DEE2 |
| 9   | 15F0   | 996A | | 19  | 2E50   | B1CA | | 29  | 46B0   | C8F2 | | 39  | 5F10   | E152 |

Frame n of a set is at BSS + n*0x9C. Sets 0-3 are the player; 4-19 are mostly tiles, items
and score pop-ups (10K/12K/50K); 20-39 are enemies and effects.
- **Collision reads the screen.** `GameTick` calls `Graph3_GetDotColor(x, y+2)` to decide whether the player
  falls. The port must keep a real 320x200 2 bpp framebuffer that game logic can read back.

## Levels

- **20 levels per volume, all different** (80 in total). Each level is **12 rows x 20 columns**
  of characters, and each character is one 16x16 tile (20x16 = 320 px wide, 12x16 = 192 px tall).
- Rows are `string[20]` constants in the main program's code segment (0x15 bytes each).
  `StartLevel(level)` has one case per level that calls `DrawLevelRow(row, s)` 12 times.
  Identical rows are shared between levels (e.g. `1000:6384` is the blank row).
- `DrawLevelRow` compares each character against one-char string constants at
  `1000:09D8..0A56` (64 of them). A match does some of:
  - `PutPic` of a sprite at `x = (col-1)*16`, `y = row*16` (**Y is the bottom row** of the picture,
    so the tile covers `y = row*16-15 .. row*16`);
  - stores a tile code into the 12x20 map at DS:`7857 + row*20 + col` (cleared by StartLevel);
  - calls `SpawnEnemy_guess` (1854:64FB) and/or `DrawSpecialTile_guess` (1854:59BE).
- After the rows, StartLevel draws a line at y=189 and the status bar below it.
- `tools/render_levels.py` reads all of this straight from an unpacked EXE by byte pattern
  (no Ghidra needed) and writes `levelNN.png` + `levelNN.txt` + `levels_sheet.png`. Use
  `--table` to print the tile table below.

### Tile characters (PTOMB1; same in all volumes)

Sprite = set/frame from the sprite table above. Map = tile code written to the map ("--" = none).

| Char | Map | Sprite | Extra | | Char | Map | Sprite | Extra |
|------|-----|--------|-------| |------|-----|--------|-------|
| `.`  | 0A | 4/0  | | | `l` `L` | -- | 10/1 | enemy |
| `>`  | 0B | none | | | `m` `M` | -- | 10/3 | enemy |
| `~ ! @ #` | -- | 5/0-3 | slopes | | `n` | -- | 11/3 | enemy |
| `$ % ^ &` | -- | 6/0-3 | slopes | | `p` | 03 | 10/0 | |
| `* ( ) _` | -- | 7/0-3 | | | `q` | -- | 12/2 (x2) | enemy |
| `+` `\`   | -- | 8/0 | enemy (dir +1 / -1) | | `r` | 07 | 11/0 | |
| `\| ` `` ` `` `-` | 09 | 8/1-3 | | | `s` `S` | -- | level monster | enemy type 2 |
| `=`  | 09 | 9/2  | | | `t u v` | 1A 1B 1C | 4/1-3 | |
| `:`  | -- | 12/0 | | | `w` | 1D | 15/2 | |
| `}` `{` `[` | 04 05 19 | 4/0 (brick) | hidden? | | `x` | 18 (1 in 10) | 4/0 brick | random |
| `c`  | 06 | 10/2 | enemy spawn list | | `z` | -- | 9/3 | enemy |
| `d`  | 02 | 9/1  | | | `B` | -- | 18/0 | enemy |
| `f`  | -- | 14/1 | | | `F` | 15 | 12/3 | |
| `i`  | 13 | none | | | `P` `O` | -- | 15/0, 15/1 | enemy |
| `k`  | 01 | 9/0  | | | `R` | 08 | 11/1 | |
| `T`  | 14 | 14/0 | | | `X` | -- | random frame of 13 | decoration |
| `Y`  | -- | 1/0  | **player start** | | `Z` | FF | none | |
| `1 2 3` | 0C 0E 10 | 11/2 | | | `4 5 6` | 0D 0F 11 | none | |
| `7 8 9 0` | 12 16 17 18 | 4/0 brick | hidden? | | | | | |

`x` is the ordinary brick; one in ten gets map code 18 at random. `}`, `{`, `[`, `7`-`0` draw
as plain brick but carry their own map codes: they're hidden items and door walls (see below).

### Tile map codes (TouchTile, 1000:3B2D)

GameTick calls `TouchTile(y, x)` for 9 points around the player box. It reads
`map[y div 16 + 1][x div 16 + 1]` and switches on the code. Nothing happens if the game-over
flag `[7964]` is set. "Erase" means the cell is cleared to 0 and overwritten with the blank
16x16 picture at B6AA. Points are TP `Real` constants decoded from the code.

| Code | Chars | What it is | Effect | Points |
|------|-------|------------|--------|--------|
| 00 | (empty) | nothing | | |
| 01 | `k` | **cyan key** | `[7961]` = 1, erase | 100 |
| 02 | `d` | **cyan keyhole** | needs `[7961]`; uses the key, erases itself, then **opens the exit door**: every code-04 cell slides down 16 px and every code-05 cell slides up, with a rumble, and both become code FF (exit) | 500 |
| 03 | `p` | **spear** | spears `[75E2]` +1 (max 5), erase | 100, or 10,000 if already at 5 |
| 04 | `}` | **exit door (slides down)** | looks like brick; inert until the keyhole is used | |
| 05 | `{` | **exit door (slides up)** | same, slides up | |
| 06 | `c` | **gem / treasure** | erase, remove from the glint list, treasure +1 | 50 + 50 |
| 07 | `r` | **magenta key** | `[7962]` = 1, erase | 100 |
| 08 | `R` | **magenta lock block** | needs `[7962]`; uses the key and erases the block (a locked barrier) | 500 |
| 09 | `\|` `` ` `` `-` `=` | **spikes / hazards** | `KillPlayer` | |
| 0A | `.` | **crumbling brick** | inert until a code-0B trigger is touched | |
| 0B | `>` | **invisible trigger** | every code-0A cell crumbles away (set 16 frames 0->3) | |
| 0C/0E/10 | `1` `2` `3` | **switch** (11/2) | erase, then every cell with code+1 (0D/0F/11) **builds a brick** (set 16 frames 3->0). Bricks are solid because collision reads pixels | 100 |
| 0D/0F/11 | `4` `5` `6` | **invisible brick spots** | targets of switches 1/2/3 | |
| 12 | `7` | **brick with hidden treasure** | flashes a gem, treasure +5, brick stays | 5 x 50 |
| 13 | `i` | **hidden invisibility trap** | erase, sound; the **player turns invisible** for 300 timer ticks (~16.5 s): `[795E]`=1, timer `[75F2]`. Only the shadow line (set 17) is drawn. Purely visual: enemies and spikes still kill. Level 17 starts with the player invisible | |
| 14 | `T` | **"T" collectible** (14/0) | erase, T count `[75EC]` +1; 5 of them -> 50,000 and reset | 1,000 |
| 15 | `F` | **freeze** (12/3) | erase, `[7968]` = 1: all enemies stop for 3000 steps | 1,000 |
| 16 | `8` | **brick with hidden treasure** | treasure +10 | 10 x 50 |
| 17 | `9` | **brick with hidden treasure** | treasure +25 | 25 x 50 |
| 18 | `0`, 1 in 10 `x` | **brick with hidden bonus** | random 1..3: shows the 10K / 15K / 20K sign and counts up 10/15/20 x 1,000 with rising beeps | 10,000-20,000 |
| 19 | `[` | **brick with hidden extra life** | once per level (`[796B]`): shows a player icon, lives +1 (max 5) | 10 x 5,000 if lives full |
| 1A-1D | `t` `u` `v` `w` | **artifacts** (4/1-3, 15/2) | erase, sound | **100,000** |
| FF | `Z`, opened doors | **exit** | saves spears/T count/treasure for the next level, level `[75D0]` +1; after level 20 the volume-complete ending, otherwise StartLevel | 1,000 |

Related counters (shown by `DrawStatusBar`, 1000:0671):

- **Treasure** `[75E6]`: `AddTreasure(n)` adds n with a beep and 50 points each. At 100 you get
  +1 life (or 50,000 if at 5 lives) and it resets.
- **Lives** `[75E4]`: starts at 4, max 5.
- Shooting a walker (types 2 and 8) is worth 250.

## Enemies

`SpawnEnemy(type, x, y, speed, dx, dy, flag: byte)` (1854:64FB) fills the next of **10 slots**,
0x12 bytes each, at DS:`7EA4 + i*0x12` (i = 1..10). StartLevel marks all slots free.
`MoveEnemies` (1000:1FA0) runs from GameTick whenever timer counter `[7F6C] > 0` and has one
branch per type. Movement per step is `speed * dx` / `speed * dy` pixels.

| Offset | Field                                   |
|--------|-----------------------------------------|
| +00    | animation frame (1..4), 1 on spawn      |
| +02    | x                                       |
| +04    | y (bottom row, like PutPic)             |
| +06    | speed                                   |
| +08    | dx                                      |
| +0A    | dy                                      |
| +0C    | type                                    |
| +0E    | 0 on spawn (unknown)                    |
| +11    | free flag (1 = slot unused)             |

| Type | Chars (speed) | Sprite | Behaviour | Kills on touch | Shootable |
|------|---------------|--------|-----------|----------------|-----------|
| 1 | `+` (dx +1), `\` (dx -1), speed 2 | 8/0 | **Horizontal moving platform.** Reverses when the pixel ahead isn't black or the player blocks it; carries a player standing on it. | no | no |
| 2 | `s` (1), `S` (2) | **per-level monster** | **Walker.** Patrols left/right, turns when `EnemyBlocked`, animates 4 frames. Shot: sparkle death animation (set 20) + score. | yes | yes |
| 3 | `z`, speed 2 | 9/3 | **Pushable block.** When the player stands beside it on the same row, facing it, it slides 2 px, with a random-pitch speaker click if sound is on. | no | no |
| 4 | `l` (1), `L` (2) | 10/1 | **Vertical bouncing hazard.** Moves down/up, reverses on non-black pixels. | yes | no |
| 5 | `m` (1), `M` (2) | 10/3 | Same as type 4 with a different sprite. | yes | no |
| 6 | `n`, starts going up | 11/3 | **Elevator.** Vertical platform that carries the player, reverses at obstacles or y <= 36. Crushes a player beneath it while moving down. | when crushing | no |
| 7 | `q`, starts going up | 12/2 | **Vertical hazard**, taller hit box than 4/5. Drawn twice (x and x-2). | yes | no |
| 8 | `B` | set 18, animated | **Bird/bat walker.** Like type 2, but also turns at ledges (needs floor under it) and stays within x = 3..317. | yes | yes |
| 9 | `P` | 15/0 | **Dart trap, fires right.** When the player is on its row it shoots a dart (a plotted dot) moving +4 px per tick of `[7F72]` until it hits a wall, the screen edge, or the player. | dart kills | no |
| 10 | `O` | 15/1 | Dart trap, fires left (`[7F70]`, -4 px). | dart kills | no |

- `[7968]` freezes all enemies. It's set by the `F` freeze pickup (tile code 15), counts up in
  `[75EA]`, and clears after 3000 steps.
- `c` tiles aren't enemies. They go into a spawn-point list at `7606 + n*6`; GameTick
  occasionally (5 in 100) plays a set-21 "glint" animation on a random one.

### Per-level monster for `s`/`S` (DrawLevelMonster, 1854:59BE)

The sprite is `frame*0x9C + base[level]`. The level number is `[75D0]`, 1..20 within the volume.
Same table in all four volumes:

| Level | 1  | 2  | 3  | 4  | 5  | 6  | 7  | 8  | 9  | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|-------|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|----|
| Set   | 31 | 27 | 24 | 25 | 26 | 23 | 28 | 29 | 30 | 39 | 32 | 33 | 31 | 34 | 19 | 35 | 36 | 37 | 38 | 22 |

## Cheats

Both are gated on **Scroll Lock** being on, which `ReadShiftKeys` (INT 16h AH=02h, bit 4)
copies into `[797B]`. `+` is read through Crt `ReadKey`, so either `+` key works.

| Where | Keys | Effect |
|-------|------|--------|
| Main menu | Scroll Lock on, `+` | `[795D]` = 1, then start the game. **Invincible + unlimited spears**: `KillPlayer` takes a life and then gives it back, and `PlayerFire` does the same with the spear. Dying still plays the death animation and restarts the level. `ResetGameState` clears the flag, so it lasts for one game. |
| In game | Scroll Lock on, `+` | `CheatSkipLevel` (1000:3AD7): if level < 20, carry spears / T count / treasure over like a normal exit, +1,000 points, start the next level. Works with or without cheat mode. |

## Hardware hooks (what the port replaces)

| Hook                  | Where              | Behaviour                                                |
|-----------------------|--------------------|----------------------------------------------------------|
| INT 9 keyboard ISR    | `1000:0000`        | port 60h scancodes -> held-key flags (e.g. Left -> [7975]) |
| INT 1Ch timer ISR     | `1854:6afa`        | ++ six tick counters [7f6a]..[7f74] at 18.2 Hz           |
| Joystick              | 20E3, port 201h    | detect, read X/Y axis, buttons 1/2                       |
| PC speaker            | Crt Sound/NoSound  | sound effects (toggle with E)                            |
| Video                 | Graph3, Genus PCX  | BIOS mode 4, direct CGA memory at B800                   |

## Program flow

```
main (1000:8473)
  unit inits: System, PCX Toolkit, PCX Effects, joystick, Crt
  StrStore('ptomb1') -> [80f8]           base name for .HS/.MNU/.SAV
  Joy_Detect / Joy_ReadAxis              joystick centre calibration
  Graph3_GraphColorMode
  ResetGameState
  InitSprites_A, InitSprites_B
  InstallTimerISRAndExitProc
  InstallKeyboardISR
  LoadHighScores                         PTOMB1.HS: 5 x 262-byte records
  LoadConfig                             PTOMB.CFG: 1 byte, default '3'
  TitleScreen                            shows PTOMB1.MNU (PCX) + credits
  MainMenu loop
    E/Space/Enter -> PlayGame            + with Scroll Lock on -> cheat mode, PlayGame (see Cheats)
    I Instructions  S Story  H HighScores  C ConfigKeyboard  J JoystickMenu
    O OrderingInfo  Q quit
  RestoreKeyboardISR
  ExitTextScreen                         text mode, ordering info
  Halt

PlayGame (1000:53b7)
  StartLevel(level); draw; repeat GameTick until quit [7963] or game over [7964]

GameTick (1000:4f69), once per frame
  random enemy spawn animation
  if ticks[7f6c] > 0: MoveEnemies, ...
  gravity via GetDotColor below the player
  joystick buttons -> jump / fire
  keys: Space jump, P pause, S save, R restore, E sound on/off, Q/Esc quit prompt,
        + with Scroll Lock on -> CheatSkipLevel (see Cheats)
  PlayerMoveHorizontal: ISR arrow flags, or Shift/Ctrl/Alt (ReadShiftKeys, layout chosen by
        the PTOMB.CFG byte), or joystick; levels with [7971]/[7972] push the player +-2 (wind)
  if ticks[7f6e] > 0: animate x4
  probe 9 points around the player box (TouchTile)
```

## Key variables (DS)

| Address   | Meaning (so far)                        |
|-----------|-----------------------------------------|
| 74DC/74DE | player X / Y (pixels)                   |
| 75CE      | volume number (1..4)                    |
| 75D0      | level within the volume (1..20)         |
| 75E2      | spears (starts at 2, max 5)             |
| 75E4      | lives (starts at 4, max 5)              |
| 75E6      | treasure count (100 -> extra life)      |
| 75EC      | "T" collectibles (5 -> 50,000 points)   |
| 7961/7962 | holding cyan / magenta key              |
| 7968      | enemies frozen (F pickup)               |
| 795D      | cheat mode (invincible, unlimited spears)|
| 795E      | player invisible (300 ticks, [75F2])    |
| 7982      | score (6-byte TP Real, 7982-7987)       |
| 7977-797B | Ctrl, Alt, LShift, RShift, ScrollLock   |
| 7963      | quit-to-menu flag                       |
| 7964      | game-over flag                          |
| 7994      | sound effects on                        |
| 7995/7996 | joystick enabled / detected             |
| 7975..    | held-key flags from the keyboard ISR    |
| 7F6A-7F74 | tick counters from the timer ISR        |
| 80F8      | base file name ('ptomb1')               |
| 8101      | config byte from PTOMB.CFG              |

## Data files

| File          | Size  | Contents                                   |
|---------------|-------|--------------------------------------------|
| `PTOMB.CFG`   | 1     | config byte (default '3')                  |
| `PTOMBn.HS`   | 1310  | high scores, 5 x 262-byte records          |
| `PTOMBn.MNU`  | 5722  | title/menu screen, PCX 320x200x4           |
| `PTOMBn.SAV`  | 17    | saved game                                 |
