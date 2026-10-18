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
  (MSB = leftmost pixel, 4 bytes per 16-px row), **bottom row first**. That matches PutPic's
  Y being the bottom row. (Reading them top-first, as the first versions of the tools and port
  did, turns every sprite upside down; the giveaway is the score signs reading "12K / 50K"
  where the code awards 15,000 / 20,000.) **All 160 frames are 16x16.** Each frame sits in a
  0x9C-byte slot (room for 24x24) padded with spaces.
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

Frame n of a set is at BSS + n*0x9C. Sets 0-3 are the player (an explorer in a green hat);
4-19 are mostly tiles and items, including the score signs (10K = 12/1, 15K = 14/2,
20K = 14/3), the "16 TONS" weight (10/3, enemy type 5), the hanging spike comb (10/1, type 4),
17 = the invisible player (just the hat brim) and 19 = the death skull; 20-39 are enemies
and effects.
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
| 02 | `d` | **cyan keyhole** | needs `[7961]`; uses the key, erases itself, then **opens the exit door**: every code-04 cell slides 16 px right (a black column drawn at its left edge, then the brick 1 px further, 17 steps of Delay(1)) and every code-05 cell slides left (GetPic of the 17x17 area k px to the right, PutPic back at the cell, k = 1..6, Delay(2)), with random-pitch speaker noise, and both become code FF (exit) | 500 |
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
| 13 | `i` | **hidden invisibility trap** | erase, sound; the **player turns invisible** for 300 timer ticks (~16.5 s): `[795E]`=1, timer `[75F2]`. Only the hat brim (set 17) is drawn, floating where the player is. Purely visual: enemies and spikes still kill. Level 17 starts with the player invisible | |
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
| 7 | `q`, starts going up | 12/2 | **Vertical hazard**, taller hit box than 4/5. Drawn twice at load: at y and 2 px higher, and spawned at y-2. | yes | no |
| 8 | `B` | set 18, animated | **Bat that crawls along a ceiling.** Moves while there are pixels at `y-17` above both its edges (a ceiling) and nothing 2 px ahead at `y`, within x = 3..317; otherwise turns. | yes | yes |
| 9 | `P` | 15/0 | **Dart trap, fires right.** When the player is on its row it shoots a dart (a plotted dot) moving +4 px per tick of `[7F72]` until it hits a wall, the screen edge, or the player. | dart kills | no |
| 10 | `O` | 15/1 | Dart trap, fires left (`[7F70]`, -4 px). | dart kills | no |

- `[7968]` freezes all enemies. It's set by the `F` freeze pickup (tile code 15), counts up in
  `[75EA]`, and clears after 3000 steps. The count goes up once per slot per MoveEnemies pass,
  so 10 per tick of `[7F6C]`: about 16.5 seconds. Frozen enemies stop moving and aren't
  redrawn, but walkers and hazards still kill on touch, and pushable blocks still move.

### MoveEnemies details (1000:1FA0, from the disassembly)

- Each pass first clears "on a platform" `[796D]`, then for each slot computes
  `nx = x + speed*dx`, `ny = y + speed*dy` (just x / y when dx / dy is 0).
- **Free slots run as type 1** parked at (-32, -32); harmless, but kept.
- **TouchesPlayer(left, bottom, right, top)** (1000:1CB0) is true if any of the player's points
  `(x|x+8|x+15, y-15)`, `(x|x+15, y-8)`, `(x|x+8|x+15, y-1)` is inside the rectangle. Hit boxes:
  walker `(x+2, y, x+13, y-15)`, vertical hazards `(x, y+2, x+15, y-15)`, tall hazard
  `(x, y+2, x+15, y-17)`, bat `(x+2, y, x+13, y-14)`.
- **EnemyBlocked(self, left, right, y)** (1000:1E47): blocked by a type 2/3/8 enemy (set at
  CS:1E27) on the same row covering either probe column, by no floor at `y+2` or a wall at `y`
  or `y-15` under either column, or by `left < 2` / `right > 318`. Walkers probe `x-4` and
  `x+18`; pushable blocks `x-8` and `x+22`.
- **Spear vs walker/bat**: the spear is tested 8 px ahead of where it is (`x+-8`), and that
  look-ahead is undone even when no spear is flying. A hit erases the spear, scores 250, plays
  set 20 for 4 frames with Delay(1), blanks the enemy and frees the slot.
- Platform (1): reverses when the player overlaps its next position or there's a pixel at
  `(nx-2 | nx+17, ny-15)`. A player standing on it (`y - 16 == player y`) moves with it unless
  a wall pixel is in the way, in which case the player is moved the other way.
- Pushable block (3): pushed 2 px when the player stands beside it on its row, facing it
  (`Between(px, x, x+18)` facing left, `Between(px+18, x, x+15)` facing right). It erases its two
  trailing columns and clicks the speaker at `Random(200)` Hz. Freeze doesn't stop it.
- Vertical hazards (4, 5): move while `(x+8, ny+1)` and `(x, ny-16)` are black (and not above
  y = 12), else reverse and nudge up 1 px. Tall hazard (7): `(x, ny+1)` and `(x, ny-18)`.
- Elevator (6): moves while `(nx, ny+1)` is black and `ny > 36`, carrying a player inside its
  box, else reverses and snaps a rider to `y-17`. It kills a player in its columns whose box
  contains its bottom+2 while it's moving down. It leaves a vertical line below as it rises
  (its bottom row isn't black); unverified against the original.
- Dart traps (9, 10): one dart per direction for the whole level ([796F]/[7600]/[7602] right,
  [7970]/[7604]/[7606] left). Armed when the player's y is within the trap's `y-15..y+1`. Fired
  from `x+16` (right) or `x-1` (left) at `y-7`, plotted as a dot moving 4 px per tick of
  `[7F72]` / `[7F70]`. It kills inside `px-4..px+19`, `py-17..py`, and stops past the screen edge
  or at a non-black pixel 4 px ahead. **DS:7606 (left dart y) is also glint entry 0's x.**
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

## Text and fonts

The game writes text with Crt (GotoXY, TextColor, Write) while in CGA mode 4, so the BIOS
draws each character as an opaque 8x8 cell on a 40x25 grid: ink in `TextColor & 3`, the rest
colour 0. In graphics modes the BIOS takes characters 00h-7Fh from its ROM font and 80h-FFh
from the table INT 1Fh points at.

**The game ships five fonts of its own** in the data segment (128 glyphs x 8 bytes, rows top
first) and points INT 1Fh at one directly (`mov es:[007Ch], offset/segment`, not SetIntVec):

| Routine | Table | Used for |
|---------|-------|----------|
| 1854:1078 | DS:7180 | DrawFrame border pieces (Greek-key corners and edges, 80h-89h) and more decorative tiles. Only 108 glyphs are stored; the rest is BSS |
| 1854:10A3 | DS:6180 | **in game** (StartLevel): A-Z at 82h-9Bh, status icons 9Ch life, 9Dh spear, 9Eh treasure, 9Fh key, A0h T, digits B4h-BDh, plus pre-drawn "PRESS ENTER TO CONTINUE" / "ANY KEY" |
| 1854:10CE | DS:6580 | menus, game over |
| 1854:10F9 | DS:6980 | title, high scores |
| 1854:1124 | DS:6D80 | another lettering style |

Two tables carry leftover text from the tool that made them: an author credit (a
Western Washington University address) and "VER 2.0 FEB 1986".

The text routines **remap characters into those fonts**: DrawTextAt (1854:1385, `(col, row, s)`)
and DrawTextCentered (1854:1446, `(color, row, s)`, column `(40 - len) div 2 + 1`) add 41h to
A-Z (set at 1854:1365). DrawNumber (1854:1520, `(value, col, row)`) prints Str(Trunc(value))
with '1'-'9' + 83h and '0' + 8Dh (set at 1854:1500). Everything else, including spaces,
punctuation and a lowercase `x`, is printed as-is from the ROM font. ClearStatusLine
(1854:1330) writes 39 spaces at (1, 25), one short of the cell that would make the BIOS scroll.

**DrawFrame(color, left, top, right, bottom)** (1854:114F) selects the frame font and draws
80h / 81h... / 83h along the top, 84h / 85h down the sides, and 86h / 87h... / 89h along the
bottom. StartLevel draws (5,10)-(35,14) in colour 3 around the level name, and game over draws
(2,2)-(38,24) in colour 1.

**Status bar** (DrawStatusBar, 1000:0671, row 25):

| Columns | What | Colour |
|---------|------|--------|
| 1- | score (DrawNumber) | 2 |
| 9-13 | 9Ch x 5 lives | 1 for each life you have, else 2 |
| 17 / 18 | 9Fh magenta / cyan key | 2 / 3 if held, else 0 (invisible) |
| 21, 22, 23- | 9Eh treasure, `x`, count | 3, 2, 2 |
| 27-31 | A0h x 5 T collectibles | 1 / 2 |
| 35-39 | 9Dh x 5 spears | 1 / 2 |

Prompts (QUIT (Y/N)?, GAME PAUSED - PRESS ANY KEY, SOUND EFFECTS ON/OFF) are
ClearStatusLine + DrawTextCentered(1, 25, ...). Level names: ShowLevelTitle (1854:06A5) picks one
of four 20-name tables by comparing the base file name with 'ptomb1'..'ptomb4' and prints it with
DrawTextCentered(2, 12, name) inside StartLevel's frame. Game over: menu font, colour 2,
DrawTextAt(12, 12, "G A M E   O V E R"), then the big frame and Delay(36).

The port's `text.c` emulates the BIOS cell output and ports these routines. `gamedata.c` reads
the five fonts and this volume's level names from the EXE by pattern. `font8x8.c` is the port's
own original font for 20h-7Eh, drawn as ASCII art in the source.

## Sound effects

Everything goes through Crt `Sound(hz)` / `NoSound` (PC speaker, PIT channel 2). Crt ignores
`hz <= 18`, so a `Sound(0)` keeps the previous tone playing.

**1854:15F0 Sweep(lo, hi, gap, hold, step, up, staccato, enabled)** (8 words, `RETF 10h`):
if `enabled` (the sound-on flag `[7994]`), start at `lo` (up) or `hi` (down) and repeat
`Delay(gap); freq +-= step; Sound(freq); Delay(hold); if staccato NoSound` until
`freq >= hi` (up) or `freq <= lo` (down), then `NoSound`. Delays are timer ticks; the last
step's tone is cut off at once unless `hold > 0`.

| Caller | Effect | lo | hi | gap | hold | step | dir | staccato |
|--------|--------|----|----|-----|------|------|-----|----------|
| 1854:58D2 SfxPickup | keys, spears, switches, T | 500 | 2000 | 0 | 0 | 10 | up | no |
| 1854:592F SfxBonus | spears full, extra life, 100 treasure, 5 Ts | 10 | 3000 | 1 | 1 | 750 | up | yes |
| 1854:595F SfxDeath | KillPlayer | 100 | 1000 | 1 | 1 | 200 | down | no |
| 1854:598F SfxUnlock | keyhole, magenta lock | 1500 | 3000 | 1 | 0 | 200 | up | no |
| 1854:5900 SfxEnemyKilled | spear hits a walker / bat | 100 | 8000 | 1 | 0 | 2000 | up | no |
| 1000:00B9 SfxArtifact | artifacts (codes 1A-1D) | 100 | 5000 | 2 | 0 | 500 | up | no |
| PlayerFire (1000:4ED5) | spear throw | 500 | 3000 | 1 | 0 | 1000 | up | no |
| TouchTile (1000:4222, 43DA) | crumble / build bricks | 1000 | 3000 | 0 | 0 | 500 | down | yes |
| TouchTile (1000:44D9) | invisibility trap | 100 | 5000 | 1 | 1 | 1000 | up | yes |
| TouchTile (1000:461F) | freeze | 1000 | 10000 | 1 | 1 | 1000 | down | yes |

Other sounds are direct Sound/NoSound calls: footsteps (50 Hz click, 1000:380B), jump
(`[75F4]` x 1000 Hz rising, first 10 steps), treasure counting (8000 Hz, Delay(1) each), hidden
bonus count-up (`i*100 + 1000` Hz), door slides (`Random(100)` Hz), block pushes
(`Random(200)` Hz), dart trap (500 Hz for 1 tick, 1854:58A7).

**Sweeps with gap = hold = 0** (pickup, crumble) last only as long as the CPU takes: about 600
cycles per step, ~125 us on a 4.77 MHz 8088, so the pickup chirp was ~19 ms on an IBM PC
and shorter on faster machines. The port queues those steps to the audio thread at 125 us
each instead of blocking. All other sweeps run in real time and block, as in the original.

Verified with `--record-audio`: recordings of `--sound-test` and a level-16 run show the
frequencies, step lengths (1 tick = 55 ms), staccato gaps and cut-off last steps above.

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
| 757E-758E | save record (17 bytes, see Save and restore) |
| 7590-75A0 | SnapshotForSave copy taken by StartLevel |
| 75F8/75FA/75FC | T count / treasure / spears restored by StartLevel |
| 796B      | hidden extra life taken                 |
| 796E      | restored: StartLevel keeps the counters |
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
| `PTOMBn.SAV`  | 17    | saved game (see Save and restore)          |

## The port (src/)

| File | Replaces |
|------|----------|
| `cga.c` | CGA mode 4 video memory: packed 2 bpp, 80 bytes/row (linear, no even/odd banks); palettes; PNG screenshots |
| `graph3.c` | Graph3 unit: GraphColorMode, PutPic (Y = bottom row, opaque, clipped), GetPic, GetDotColor (-1 off screen), Plot, Draw |
| `timer.c` | INT 1Ch TimerISR: six counters at 18.2065 Hz, driven from SDL's performance counter |
| `tp.c` | TP `Random` (LCG 134775813; sequence not yet checked against 2714:08D5) |
| `lzexe.c` / `gamedata.c` | reads the user's EXE (packed or not), finds DS and the level rows by pattern |
| `level.c` | StartLevel / DrawLevelRow / SpawnEnemy / DrawLevelMonster, with the tile table as C data |
| `game.c` | GameTick, PlayerWalk, PlayerMoveHorizontal, PlayerJumpStep, PlayerFire, MoveShot, TouchTile, KillPlayer, AddTreasure, status-bar bonuses, ShowLevelTitle |
| `platform.c` | keyboard ISR flags, BIOS key buffer (ReadKey / KeyPressed), shift flags, Delay, PC speaker, SDL window, `--script` test input |
| `main.c` | options and the game loop (GameTick at a fixed 70 Hz) |
| `render.c`, `png.c` | port only: the display layer (smooth movement, custom sprites); see "The display layer" |

Verified: `--screenshot` renders from the packed originals match `tools/render_levels.py`
pixel for pixel, except the random `X` frames and the `q` tiles (the Python tool doesn't do the
second, 2 px higher draw). Scripted runs on volume 1 show falling, walking, jumping, picking up
a gem (+100, treasure 1), being blocked by solid pixels, dying on spikes (lives 4 -> 3, treasure
restored to its level-start value) and changing level. (Those first runs used upside-down
sprites; after the row-order fix the renders still match each other, the pyramid slopes are
smooth, and the level-16 spear kill still scores 250. Collision results differ, as they should
with the corrected shapes.)

### Player mechanics (from the disassembly)

- **Collision is all screen pixels.** PlayerWalk(dx) probes `(x+dx, y)`, `(x+15+dx, y)`,
  `(x+dx, y-15)`, `(x+15+dx, y-15)`, `(x+dx, y-8)`, `(x+15+dx, y-8)`. Those columns include the
  player's own sprite, so it only works because the player art is black there. Enemies'
  pixels block too.
- Walking is 2 px per tick of `[7F6A]` (36 px/s). A press against your facing only turns you.
  The turn frame (set 3 frame 1) is gated on `[75E0]`, which only ResetGameState resets, so it
  shows only on the first turns of a game. `PlayerWalk(0)`, used after picking up keys, turns
  you to face left.
- Gravity (GameTick): if not on a platform and `(x, y+2)` and `(x+14, y+2)` are black, fall
  1 px and enter the jump state already falling (`[75D6]` = 22h).
- Jump: `[75D6]` counts steps; below 22h each JumpStep goes up 1 px, from 22h down 1 px. There
  are 4 steps per tick of `[7F6E]` (72 px/s), and a rising-pitch beep for the first 10 steps.
  The first hit (ceiling or floor) pauses 10 steps and switches to falling; the second ends the
  jump.
- Keyboard ISR (1000:0000): only Left (4Bh) and Right (4Dh) make/break codes, into `[7975]` /
  `[7976]`, then chains to the BIOS so ReadKey still works. Space, fire and the command keys
  come through ReadKey; any extended key becomes `|`.
- Wind (levels 10 and 16): PushPlayer(+-2) every move tick; a held direction key is also pushed
  once more in its own direction.
- Spear: an 8-px line at `y-8`, moving 4 px per tick of `[7F6C]`, stopped by any non-black pixel
  or the screen edge.
- **PlayerFire's check is lopsided** (1000:4DF0). Facing left the spear starts at `x-8` and the
  throw is cancelled if any of `x-7..x` (its own pixels, nearest the explorer) isn't black.
  Facing right it starts at `x+16` but tests `x+25..x+32` (1000:4E62 adds 8): the 8 pixels *past
  its tip*. So facing right, anything 9-16 px ahead of the hand silently cancels the throw (the
  key is used up, no spear spent). Found by the user on volume 1 level 1: walk right off the
  top ledge, land at (152, 80) beside the shaft, and F does nothing, because the shaft's right
  wall starts at x = 176. It also means a walker 9-16 px to your right can't be speared, while
  one on your left can. The port keeps it in classic; `gameplay.spear_far_check = off`
  (enhanced) tests the spear's own pixels facing right too (`x+16..x+23`, the mirror of the
  left-hand test). Measured: at (152, 80) enhanced throws (the spear stops at the wall at once);
  standing at (160, 144) with level 1's walker at x = 189, classic refuses and enhanced kills it
  (+250).
- Losing a life restarts the level, and StartLevel restores spears, T count and treasure to
  their values when the level began (score and lives stay).
- `ShowLevelTitle` (1854:06A5) prints the level's name and waits `48 - level` ticks or a key.

Enemies (MoveEnemies) are ported too. Scripted runs on volume 1 show platforms, walkers,
vertical hazards, elevators and bats moving (levels 2, 3, 5, 9). On level 16, a spear kills the
right-hand walker (+250) and the left one kills the player. Freeze and the dart traps are ported
but haven't been exercised in a scripted run yet.

Not done yet: the text-mode exit screen (1854:60C8), and the Genus transition effects (approximated).
The glint off-by-one (entry 0 at (0, 0)) is kept on purpose.

## Title screen, menus and high scores

**Startup** (main, 1000:8473): ... LoadHighScores, LoadConfig, then the entry code's near calls
are ResetGameState, InstallKeyboardISR, **TitleScreen (1000:6227)**, `[75FE]` = 1 (start
level), **MainMenu (1000:5A9F)**, RestoreKeyboardISR, then ExitTextScreen and Halt.

**The title picture**: LoadPcxScreen (1000:027E) loads `<base>.mnu` (a 320x200 2 bpp PCX)
into an off-screen buffer with the Genus PCX Toolkit, dropping to a text-mode error and Halt
if that fails. 1000:04D1 shows it with one of 18 transitions picked by `Random(18)`, through
1000:048D -> Genus PCX Effects (`20FF:000F` type 0-9, 9 = random; `20FF:00DD` delay;
`20FF:01FB` run, a jump table of effect routines). The 18 settings are (variant, delay, type)
= (4,0,3) (8,0,3) (2,0,3) (1,0,3) (1,4,0) (2,4,0) (4,4,0) (8,4,0) (2,0,8) (1,0,8) (2,4,6)
(1,4,6) (1,4,1) (2,4,1) (6,0,7) (5,0,7) (10,0,7) (9,0,7). The variants look like a direction mask
(1/2/4/8, pairs = diagonals). The effect routines themselves aren't decoded; the port
approximates them as wipes (`pcx.c`).

**TitleScreen**: picture + effect; menu font; one of four volume headers
(DrawTextCentered(1, 16, ...), chosen by `[75CE]`); title font; credits on rows 18 / 20 / 21 / 22
("BY GEORGE BROUSSAD", sic); "THIS GAME IS NOT SHAREWARE OR PD!" on row 23 unless the base
name is 'ptomb1'; an anti-piracy message that only prints if the base name is 'impossible'
(never); then the **walking demo** (1000:5D18): ResetGameState, player at (160, 98), walking
2 px per tick for `Random(30) + 25` ticks then jumping (JumpStep -1 for 16 steps, then +1 until
back at y = 98, 10 ms Crt Delay per step), turning round each time, within x 44..267. It uses the
real PlayerWalk / PlayerJumpStep, so the picture's pixels are its floor. It stops on a key or
after 300 steps.

**MainMenu**: each pass: GraphColorMode, ResetGameState, picture + random effect, title font,
"VER: 3.0   MAIN MENU     VOL: " + Str(volume) centred on row 11 in colour 3, menu font, four
label lines on rows 16-22 in colour 2 with their hot-key letters redrawn by DrawTextAt in
colour 1, two bats (1000:552E: set 18 at y = 60, x 30..80 and 220..270, one step per Delay(1)).
It then **flushes the keyboard** (1000:5C05), which is what drops the key that ended the title
demo: neither the demo nor the picture transition consumes it (the Genus delay routine only
reads keys if 20FF:1349 enabled that, and the game never calls it).
It then loops `Random(32767)` and the bats until a key in the set at CS:5A7F
(Enter Space + C E H I J O Q S) is pressed:

| Key | Action |
|-----|--------|
| E / Space / Enter | PlayGame (1000:53B7): StartLevel(`[75FE]`), DrawStatusBar, FlushKeyboard, GameTick until quit or game over |
| + | ReadShiftKeys; with Scroll Lock on, cheat mode and PlayGame |
| Q | ShowOrderingInfo(volume, 0), then leave the menu (exit) |
| O | ShowOrderingInfo(volume, 1) |
| I | ShowInstructions (1854:2A20): four pages |
| S | ShowStory(volume) (1854:3A03) |
| H | ShowHighScores(1); the key that dismisses it is left in the buffer for the menu |
| C | ConfigKeyboard (1854:69D3): keys in the set at 1854:69B3; not Esc -> `[8101]`, SaveConfig (1854:0FB7) rewrites PTOMB.cfg |
| J | JoystickMenu (1000:57C4) |

The text screens (instructions, story, ordering info, config keyboard, joystick) are run by the
port's **screen interpreter** (`screenvm.c`) directly from the EXE: they only use ~30
instruction forms (immediate pushes, near/far calls, CMP + JZ/JNZ on the volume, a key or a
flag), and every call is routed to the port's text, font, key and frame routines. Their text
therefore never has to be copied into the port. The other screens are ported by hand, with their
strings found by `gamedata_text_ops` (the routine's DrawTextCentered / DrawTextAt / TextColor /
font-select / StrLoad calls with constant arguments).

**High scores**: 5 records at DS:7892 + i*0x106 (6-byte Real score, string[255] name), ascending
so record 5 is the best; exactly the `.HS` file. Defaults (no file): scores 10000*i + 35000 and
names that read down the table as "CAN YOU SURVIVE THE / PERILS OF THE GREAT / PYRAMID?  LEAD ON
/ ADVENTURER AND SEE!". ShowHighScores (1000:5403): picture + effect, title font, "H I G H   S C O
R E S" (3, 11), game font, then for i = 5..1 on rows 15, 17, ..., 23: rank (colour 2, col 5), "."
(col 6), score (colour 1, col 8), name (colour 3, col 18). A name equal to the marker `~|!+` is
blanked and its row remembered (`[75CC]`).

**GameOver** (1000:3336, called by KillPlayer when lives < 0, and after the volume ending):
GraphColorMode, set quit and game-over flags; if score > record 1 (`JA`, strictly greater): menu
font, "NEW HIGH SCORE !" (1, 11), "ENTER YOUR NAME ON THE" (3, 13), "NEXT SCREEN" (3, 14),
DrawFrame(2, 2, 38, 24, colour 1), Delay(60); record 1 = (score, `~|!+`); bubble sort (1854:6396);
ShowHighScores(0); GotoXY(18, 15 + `[75CC]`); ReadLine(max 20) (1854:570D: characters 20h-7Fh are
appended and echoed raw, so they show in the ROM font; backspace writes 8, space, 8; Enter ends;
an extended key's scancode byte gets appended too); UpCase (1854:5835); save (1854:6794).

## Save and restore

In game, **S** saves and **R** restores (GameTick, 1000:5262 / 5273, after `UpCase`). One save
per volume, `<base>.SAV`, a Pascal typed file of one 17-byte record at DS:757E:

| Offset | Type | Field | From |
|--------|------|-------|------|
| 00 | Integer | level (1..20) | [75D0] |
| 02 | Integer | T collectibles | [75EC] |
| 04 | Integer | treasure | [75E6] |
| 06 | Integer | lives | [75E4] |
| 08 | Integer | spears | [75E2] |
| 0A | Real (6 bytes) | score | [7982] |
| 10 | Boolean | hidden extra life taken | [796B] |

The user's files decode as volume 1 level 20, 3 T, 87 treasure, 1 life, 5 spears, 951,000 points,
and volume 2 level 3, 3 T, 40 treasure, 3 lives, 1 spear, 20,150 points.

- **SnapshotForSave (1000:1C1F)** is the first thing StartLevel does: it copies those seven values
  to DS:7590-75A0. **SaveGame writes that snapshot, not the live values**, so S saves the state
  the current level began with (after a death, the restarted level, so the lost life counts).
- **SaveGame (1000:5E20)**: ClearStatusLine, "SAVING GAME..." (DrawTextCentered(1, 25)),
  Delay(18), snapshot -> record, `Assign(f, base + '.SAV')`, Rewrite, Reset, Write, Close, all
  without I/O checks, then ClearStatusLine and DrawStatusBar.
- **RestoreGame (1000:5F3B)**: ClearStatusLine, "RESTORING GAME...", Delay(18), `Assign(f, base +
  '.sav')` (lower case here), Reset with `{$I-}` and IOResult. If that fails: "SAVE FILE NOT ON
  DISK. PRESS ANY KEY.", wait for KeyPressed (the key is left in the buffer for GameTick to act
  on), ClearStatusLine, and return **without redrawing the status bar**. Otherwise Close, then
  Assign / Reset / Read / Close again with I/O checks on (a short file would be a runtime error),
  copy the record into the live variables, set `[796E]` = 1, DrawStatusBar, StartLevel(level).
- `[796E]` makes StartLevel keep the restored T count, spears and treasure instead of loading the
  level-start copies `[75F8]` / `[75FC]` / `[75FA]`, then clears itself. **Those copies are not
  updated by a restore**, so dying on a restored level brings back the counters from the last
  level entered normally in this session (or ResetGameState's). Kept in the port.
- The port reads and writes whichever of `ptombN.SAV`, `ptombN.sav`, `PTOMBN.SAV` exists next to
  the EXE (SaveGame's spelling otherwise). Verified: restoring both of the user's saves and saving
  again gives byte-identical files; saving on volume 1 level 3 writes `03 00 00 00 00 00 04 00
  02 00` + a zero Real + `00`.

## Volume ending

When the exit tile takes `[75D0]` to 21 (1000:4A8D), TouchTile calls **VolumeEnding(volume)
(1854:4E01)** and then **GameOver (1000:3336)**: the high-score entry, which sets the quit and
game-over flags, so PlayGame returns to the main menu. KillPlayer's "G A M E   O V E R" frame
isn't shown on this path.

VolumeEnding is a long run of text with no animation (the ~1200 instructions are mostly pushes
for DrawTextCentered): StackCheck, then `case volume of`, each case a set of pages. A page is
GraphColorMode, menu font, "END OF THE ROAD..." on row 1, game font, the body text in colours 1-3,
then menu font, "... PRESS ANY KEY ..." on row 25 and WaitKey (1854:1043). Every case then jumps to
a shared tail (1854:56EE) that draws that prompt again and waits once more.

| Volume | Pages | Contents |
|--------|-------|----------|
| 1 | 2 | the lost tomb and the ring; an order-the-other-volumes page with the authors' names |
| 2 | 1 | the sacred cross, a rest |
| 3 | 1 | Nevada Smith's journal |
| 4 | 3 | the finale with Professor Jones, "THE END" and a 1991 teaser |

Volumes 1-3's last page has no WaitKey of its own (the tail's is the only one), but volume 4's
does, so its last page needs two key presses. Kept.

All of it fits the instruction forms `screenvm` already knew, so the port runs the routine
straight from the EXE (`menu_volume_ending`) and `--ending` shows it for testing.

## Joystick

The joystick unit (20E3, game port 201h) has Joy_Detect (20E3:0000), Joy_Button1 / Joy_Button2
(0040 / 0068) and Joy_ReadAxis(n) (00E4, 0 = X, 1 = Y), which returns a count like any PC
analog joystick.

- **Startup** (1000:84A8): `[7995]` enabled = 0, `[7996]` detected = Joy_Detect; if detected,
  the centre is read into `[75B8]` (X) / `[75BA]` (Y). So the joystick starts **off**.
- **JoystickMenu (1000:57C4)**: key '1' toggles `[7995]` and says "JOYSTICK IS NOW TURNED
  ON/OFF."; any other key calibrates: if detected, read both axes into `[75B8]` / `[75BA]` until
  either button is down, then "... CALIBRATED ...".
- **PlayerMoveHorizontal (1000:4BD5)**, when neither arrow is held and the joystick is detected
  and on: `X < centre - 50` walks left, `X > centre + 50` right (PlayerWalk +-2, plus PushPlayer
  on the windy levels), before the Shift / Ctrl / Alt layouts are checked. Y is never used.
- **GameTick (1000:5141)**: button 1 jumps if not in the air, button 2 throws if no spear is
  flying. **It doesn't check the spear count** (the keyboard path at 1000:52F5 does), so
  the count can go negative and the joystick never runs out.
- **ShowLevelTitle (1854:0ED9)**: button 1 ends the wait, like a key.

The port (`gamepad.c`) emulates all of it with SDL game controllers: axes as 0..200 with the
centre at 100, the d-pad as full travel, A = button 1, B or X = button 2. Port changes: the
joystick starts on, detection follows hotplugging, some buttons also type keys so the menus
and prompts work from the pad (see README), and button 2 checks the spear count like the
keyboard does (a deliberate fix, unlike the other quirks, which are kept).

## Pickups and collision

Pickups (keys, spear, gem, switches, T, freeze, artifacts) are drawn pixels, and the player's
movement checks read pixels, so a pickup is solid until TouchTile collects it. TouchTile only
looks at the 8-9 points inside the player's box (`y div 16 + 1`, so a tile's bottom line counts
as the row below), while PlayerWalk looks 2 px ahead and PlayerJumpStep 1 px above the head.

- Walking along the same floor, every pickup is collected first: the sprites have at least 2
  columns of margin on the probed rows (feet, middle, head).
- Jumping up into one, the head check can hit its pixels before TouchTile's point above the
  head (`x+8, y-17`) reaches the cell, because a jump makes 4 steps per tick of `[7F6E]` and
  TouchTile runs once after them. Then the jump stops there (pause, then fall), and TouchTile
  collects the pickup from the stuck position. Whether a jump is cut short depends on the
  starting height mod 4 and the horizontal offset: on volume 1 level 1's gem, 11 of 24 jumps
  (side offsets 0-10, all 4 phases) peaked at y 157-158 instead of 144.
- Some sprites (T, freeze, artifacts) also stop a player landing on them or walking into them
  at some heights, and the gem glint (set 21) adds pixels while it plays.

The port keeps this in classic. `gameplay.solid_pickups = off` (enhanced) makes the player's
own checks ignore pixels in a pickup's cell.

## Hit boxes vs pixels

Every death is a rectangle test, not a pixel test: TouchesPlayer's 8 points against the enemy
boxes above, the dart box `px-4..px+19, py-17..py`, and a TouchTile point anywhere in a code-09
cell. The boxes cover empty parts of the pictures: the bat's top 7 rows are empty but in its box,
the hazards' boxes reach 2 px below the picture (4 px below the prongs), and spike art starts
3 rows into its cell. Measured with `--script` (`state` / `enemies` every tick, the gap between
the explorer's and the killer's non-black pixels at the hit):

| Case | Classic gap | Why |
|------|-------------|-----|
| Walker, volume 1 levels 1 and 9 | 2-5 px | box x+2..x+13 against the 16-px player box |
| Vertical hazard (`l`), level 6 | 4-5 px | box to y+2 |
| Spike pit, level 12 | 4 rows | TouchTile cell vs art |

The boxes also make up for movement reading pixels. The enemies' own probes see the player's
picture: a vertical hazard reverses when `(x+8, ny+1)` hits the explorer's hat, 3 rows short of
it, and only the y+2 box kills him. A walker turns when its `x-4` / `x+18` probe at `y-15` hits
the hat crown (columns 5-10): coming from the left the box kills first, but **coming from the
right it turns around one step early and never kills a player on its own floor**.
The player's checks see enemies and spikes too, so he can stand on spike tips or a monster's
head with 2 rows of space.

`gameplay.pixel_hits = on` (enhanced) makes every death a pixel test: the explorer's picture
(walking frame, or the jump picture in the air; his visible shape even when invisible) touches
the enemy's current picture, the spike tile drawn in that cell (`level_t.tile_pic`), or a point
the dart passed through this step, touching meaning overlapping or adjacent, diagonals included.
For that to be reachable, deadly enemies (types 2, 4, 5, 7, 8), darts and the player stop seeing
each other: the player's checks ignore pixels inside a deadly enemy's 16x16 picture and in
spike cells, and those enemies' and the darts' probes ignore the player's 16x16 picture. Platforms,
elevators, pushable blocks and the spear are unchanged. Measured: every enhanced kill above
happens at gap 0 or overlap, walkers kill from both sides, and placements where classic's box
reached into empty space (a walker turning at a wall 3 px away) survive. Classic runs are
byte-identical to the build before the change (state traces and screenshots).

Drawing: PutPic is opaque, and the game needs that (a sprite's black margin erases its own trail,
and the 1-2 px moves rely on it). Once pictures can share space, the one drawn last blanks the
other with its black background (the explorer cut in half by a walker, spike tips erased for
good). So with `pixel_hits` on, during play only (the title demo is untouched), each opaque draw
of the explorer, a deadly enemy, a dart erase or a blank is followed by `restore_overlaps`, which
puts back the non-black pixels it covered: spike tiles (and pickups when `solid_pickups` is off),
then deadly enemies, then the explorer, so he stays on top. All of those pixels are already
ignored by the movement checks that could meet them (`player_dot`, `enemy_dot`), so play is
unchanged: sequential runs give identical traces with and without it.

## The display layer (smooth movement and custom sprites)

Two port enhancements change only what the window shows (`render.c`, `display.smooth` and
`display.sprites`, both on in enhanced). The 2 bpp framebuffer stays exactly what the original
would have on screen, because collision reads it back.

**Smooth movement.** Everything moves on the 18.2 Hz timer counters, 1-4 px a step (walking 2,
jumping 4 per tick of `[7F6E]`, enemies `speed` per tick of `[7F6C]`, spear and darts 4). For
each frame, game.c's `scene()` lists what moves, as it was last drawn: enemy types 1-8 (the
picture and position MoveEnemies last drew, kept in `enemy_t.shown*`, starting with the tile
DrawLevelRow drew, which isn't always the spawn position: `l` spawns 1 px low and `n` 3 px), the
spear (a 9-pixel line, Draw includes both ends), the two darts, and the explorer
(`player_shown`, last). Only while a level is up: not during the level title, game over or the
volume ending; the title demo's explorer is included. render.c checks each one is really there
(at least 3/4 of its pixels carry its tag and place, see below; a line, of its colour), takes
those pixels out of a copy of the framebuffer, and draws the pictures over it, black
see-through, at a position that glides from where it shows to where it was drawn over 1.15
timer ticks (a little over one step, so steady movement never pauses between steps). Moves over
12 px (a restart) jump. The window draws them as textures at fractional positions, so the glide
is smooth at the window's resolution.

**Poles.** The vertical hazards (types 4, 5) hang from a pole that is nothing but their top row
left behind in the framebuffer as they move down (the elevator's stands on its bottom row the
same way). Gliding, a rising hazard shows up to a step lower than where it was drawn, so it
came away from its pole by a pixel or two, flickering. `bridge_poles` finds the columns where
the pixel next to the picture's facing edge matches that edge, and stretches that background
row over the gap, under the picture.

Frames are drawn at the monitor's rate with vsync off (on Windows the compositor keeps a
window or desktop fullscreen tear-free). They are only drawn between GameTick passes, and only
if done before the next pass is due: a frame or a vsync wait inside a pass (GameTick's
KeyPressed polls) let timer ticks land between the pass's counter checks, and scripted runs then
played differently from run to run. With that, runs with the display on and off give identical
state traces.

**Custom sprites.** Every `cga_set` also stores what drew the pixel (`cga_tag`): a sprite
(set * 4 + frame + 1, found by Graph3 from the picture's address in the sprite table), text,
Plot / Draw lines, or the title PCX; and for a sprite, which of its pixels it is (`cga_tagpos`,
column | row from the top << 8). GetPic keeps both for what it copies, so PutPic of that buffer
(the sliding exit door) puts them back, and the pixel_hits overlay plots them too. With custom
art loaded (`render_load_sprites`: one PNG per set, its 4 16x16 frames side by side (64 x 16,
as `tools/sprite_custom.py` writes them) or in `extract_gfx.py`'s padded layout (24x24 cells
with a 1-px grid, 101 x 26), either times a whole zoom 1-8, told apart by their shape, read by
`png.c`), the background is built at
the largest zoom: a sprite pixel shows the matching zoom x zoom block of its custom picture,
everything else its CGA colour. Black margins included, as PutPic draws them, so a tile's whole
cell is the custom art. Moving pictures come from the same art, transparent where the file is
(or black, if it has no transparency). The background is only rebuilt when the framebuffer,
its tags or the colours change (about 1 frame in 9 in play). Taking a moving picture out by tag
and place rather than colour also removes its black margin, so art drawn into a sprite's
margin never leaves a copy behind at the game's position.

**Leftovers.** A sprite that moves 1-6 px is drawn again at its new place, and the strip it moved
off isn't drawn over, so those pixels keep the old frame's tag and place. The originals are
black there (their margins), so the game shows nothing, but custom art that reaches into the
margin (the user's wider hat brim) left a trail behind the explorer, smooth movement or not. So
where the game's pixel is black, the custom art only shows if at least half of the picture it
belongs to (the placement: sprite and top-left corner, counted once per background rebuild) is
still on screen. A stale strip is 1-6 columns or rows of 16; tiles keep their margins' art even
with the explorer standing in front of them. Non-black pixels always show their art (the poles
are made of those).

The unedited extracted pal0 files render pixel-identical to palette 0 at 4x.
