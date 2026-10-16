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
- Sprites: Graph3 `GetPic` format, header `word mode=2, word w, word h`, then 2 bpp rows.
  16x16 sprites, 4 animation frames each, stored in 0x9C-byte slots. 40 sprite sets live in
  DS:0000-6180 and are copied into BSS at startup by `InitSprites_A/B`.
- Full-screen images (`PTOMBn.MNU`) are **PCX** files (v5, 320x200, 2 bpp), loaded with the Genus toolkit.
- **Collision reads the screen.** `GameTick` calls `Graph3_GetDotColor(x, y+2)` to decide whether the player
  falls. The port must keep a real 320x200 2 bpp framebuffer that game logic can read back.

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
    E/Space/Enter -> PlayGame            + -> RestoreSavedGame then PlayGame
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
  keys: Space jump, P pause, S save, R restore, E sound on/off, Q/Esc quit prompt
  if ticks[7f6e] > 0: animate x4
  probe 9 points around the player box (CheckPointHit)
```

## Key variables (DS)

| Address   | Meaning (so far)                        |
|-----------|-----------------------------------------|
| 74DC/74DE | player X / Y (pixels)                   |
| 75CE      | volume number (1..4)                    |
| 75E2      | ammo? (starts at 2, fire needs > 0)     |
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
