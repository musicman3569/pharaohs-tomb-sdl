# Implementation progress (handoff)

Handoff for a new session. Read this, then `README.md` (build/run/keys) and `docs/notes.md`
(the reverse-engineering reference: every address, table and mechanic decoded so far). Last
commit when written: `c9e614c` (title screen, menus, high scores). The working tree was clean.

## Objective

A personal learning project: reverse engineer **Pharaoh's Tomb** (Micro F/X / Apogee, 1990,
freeware since 2009; DOS, Turbo Pascal, CGA) with Ghidra + GhidraMCP, and port it to run natively
on modern systems (C11 + SDL2, Windows first). The port reimplements the game logic in C and
reads **all assets at run time from the user's own `PTOMBn.EXE`** (packed or unpacked). It aims
to be faithful, including original quirks, which are kept and commented rather than fixed.

The user is learning, so explain findings as you go. They **prefer CGA palette 0**
(green/red/brown, the port's default); palette 1 (cyan/magenta/white) is what the original
uses and stays available (F1).

## Environment (verified)

- Repo: `C:\Users\Jason\Documents\Ghidra\ptomb-port` (git, branch `master`, no remote).
  `game/` (original files), `unpacked/`, `gfx/`, `build/`, `analysis/*.disasm.txt` are gitignored.
- Build: MSYS2 **mingw64** (gcc 15.2, CMake 4.2, Ninja, SDL2 2.32.10). With
  `export PATH=/c/msys64/mingw64/bin:$PATH`: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
  then `cmake --build build`. The build is warning-free with `-Wall -Wextra -Wpedantic`; keep it so.
- Python: **no system Python**. Use `C:\Users\Jason\anaconda3\envs\ghidra\python.exe` with `-I`.
  No Pillow or capstone; the tools write PNGs with zlib only.
- Ghidra has `unpacked/PTOMB1.EXE` loaded (load base segment 1000). GhidraMCP's backend is plain
  HTTP at `http://127.0.0.1:8080/` (responses `{"success", "data"}`). `tools/ghidra_dump.py`
  dumps all disassembly to `analysis/PTOMB1.disasm.txt` (grep it rather than many MCP calls);
  `tools/ghidra_rename.py analysis/PTOMB1.names.txt` re-applies all names (131 at last run).
- Not available: DOSBox. **Nothing has been compared against the running original.**

## Completed work (git log is the detailed record)

1. Tools: LZEXE 0.91 unpacker, Ghidra dump and rename scripts, sprite/PCX extractor
   (`extract_gfx.py`, `--palette 0`), level renderer (`render_levels.py`, finds everything by
   byte pattern and works on all 4 volumes).
2. Reverse engineering (all in `docs/notes.md`): unit map, main flow, sprites (40 sets x 4 frames,
   16x16, Graph3 GetPic format **bottom row first**), levels (20 per volume, 12x20 chars), the
   64-character tile table, tile map codes and scores, 10 enemy types, cheats, sound sweeps,
   the five fonts, the status bar, menus, high scores.
3. Port (`src/`, about 4,200 lines of C):
   - `cga` packed 2 bpp framebuffer + PNG writer; `graph3` PutPic/GetPic/GetDotColor/Plot/Draw;
     `timer` 18.2065 Hz ISR counters; `tp` Random.
   - `lzexe` + `gamedata`: loads the EXE and finds DS, levels, fonts, level names, routine
     addresses and text calls by pattern.
   - `level` StartLevel drawing and the tile table; `game` GameTick, the player, TouchTile,
     MoveEnemies, KillPlayer, status bar, PlayGame, title demo; `sound` SoundSweep and effects.
   - `text` + `font8x8` (BIOS-style text with the game's fonts for 80h-FFh and the port's own
     original font for 20h-7Eh); `pcx` title picture + transitions; `hiscore` .HS file;
     `menu` title / menu / high scores / game over; `screenvm` interpreter for text screens.
   - `platform` SDL2 window, input (ISR flags, BIOS key buffer, shift flags), Delay,
     PC-speaker audio, `--script` test input, `--record-audio`.
   - Startup is now the original's: title + demo, then the main menu. `--level N` skips them.

## Architectural decisions and why

- **Assets read from the user's EXE at run time, never embedded** (sprites, levels, fonts,
  level names, screen text). This keeps copyrighted content out of the repo. It's done by byte
  pattern, not fixed offsets, because the main program differs between volumes. Exceptions,
  hardcoded on purpose: short UI strings in `game.c` prompts ("QUIT (Y/N)?" and similar), the
  high-score marker `~|!+`, and file names.
- **Units after the main program are identical in all 4 volumes, only shifted** (verified
  for the game unit; assumed for the units after it). So routines are addressed as their
  PTOMB1 segment (`SEG_GAME 0x0854`, `SEG_GRAPH3 0x0F47`, `SEG_CRT 0x1081`, `SEG_JOY 0x10E3`,
  `SEG_SYSTEM 0x1714`, image-relative) plus `g->seg_delta` (`gamedata_addr`). Main-program
  routines are located: TitleScreen and MainMenu as the 3rd and 4th near calls in the entry code,
  ShowHighScores and JoystickMenu from MainMenu's `CMP AL,'H'/'J'` dispatch, GameOver by its
  opening bytes.
- **Faithful structure, including blocking code.** The original blocks in Delay loops and
  ReadKey. The port keeps that, and every blocking platform call pumps SDL, the timer and the
  display (`plat_poll`). KillPlayer calls StartLevel from inside TouchTile/MoveEnemies, as the
  original does.
- **Collision reads the framebuffer** (GetDotColor), as in the original, so the framebuffer is
  a real 2 bpp buffer and every draw must be pixel-exact. The ported probes come from the
  **disassembly, not the decompiler**: Ghidra's decompiler often hid one GetDotColor argument.
- **GameTick at a fixed 70 Hz** (`GAME_TICK_HZ`). The original ran unthrottled, and speed
  comes from the 18.2 Hz counters; 70 Hz never misses a tick and keeps per-call randomness
  (glints) stable. This is a choice, not something verified.
- **Text screens run through `screenvm`**: an interpreter for the ~30 instruction forms those
  routines use, with calls routed to port functions. It covers ShowInstructions, ShowStory,
  ShowOrderingInfo, ConfigKeyboard, VolumeEnding (game unit) and JoystickMenu (main program). It **fails
  closed** on anything unknown. Chosen so the long text stays in the EXE.
- **Hand-ported** TitleScreen, MainMenu, ShowHighScores and GameOver (they use strings, Reals
  and arrays). Their constant text comes from `gamedata_text_ops`, which scans a routine's
  DrawTextCentered / DrawTextAt / TextColor / font-select / StrLoad calls.
- **Own 8x8 font only for 20h-7Eh**: the game draws letters, digits and icons from its own five
  fonts (80h-FFh, via INT 1Fh) after remapping. Only punctuation and lowercase came from the IBM
  ROM, which can't be shipped.
- **Zero-delay sound sweeps** (pickup, crumble) are queued to the audio thread at 125 us per step
  (an estimate for a 4.77 MHz 8088), because their length was CPU speed.
- Original quirks deliberately kept: the glint `Random(n+1)` off-by-one (entry 0 at (0, 0));
  DS:7606 aliasing (the left dart's y is glint entry 0's x); the turn animation only on the first
  turns of a game; `PlayerWalk(0)` turning the player left; free enemy slots running as type 1;
  the spear look-ahead undone even when idle; extended-key scancodes appended in ReadLine.

## Rejected approaches

- Emulating the whole game (DOSBox-style) or recompiling it: not a port, and it hides the learning.
- Pattern-scanning screen routines in byte order: ShowStory and ShowOrderingInfo branch on the
  volume or a flag. That's what led to the interpreter.
- Running MainMenu / TitleScreen through the interpreter: too many instruction forms (string
  ops, Real Str, arrays).
- Porting the Genus PCX Effects library exactly: third-party, a jump table of effect routines
  (`20FF:01FB`). The transitions are approximated as wipes instead.
- Testing by injecting keys into the window from PowerShell (`keybd_event`): the keys never
  arrived. `--script` replaced it.

## Verified vs assumed

Verified (by scripted runs, screenshots, recordings or byte comparison):
- C level renders equal the Python renderer pixel for pixel (except the random `X` frames and the
  `q` second draw), for packed originals of volumes 1 and 3.
- Player: falls, walks, jumps, picks up a gem (+100), blocked by pixels, dies on spikes (lives
  -1, counters restored). Enemies move on volume 1 levels 2, 3, 5, 9; on level 16 a spear kills a
  walker (+250) and a walker kills the player.
- Sounds: `--record-audio` recordings match the decoded sweep parameters.
- Text: status bar, prompts, level title frame, game over. Menus: title + effect + demo, main
  menu, 4 instruction pages, story page 1, ordering info, config keyboard, high scores (reading
  the user's real `PTOMB1.HS`). E plays, Q+Y returns to the menu, Q exits.
- High-score entry and save on a **temp copy**: the file round-trips as 5 x 262-byte records.
- Save/restore on temp copies: R loads the user's `PTOMB1.SAV` (level 20, 951,000) and
  `PTOMB2.SAV`; S then rewrites them byte-identically; a missing file shows the original's
  message. Not exercised: dying after a restore (the stale level-start counters quirk).
- Volume ending: `--ending` screenshots of every page on all four volumes. Not exercised: reaching
  it through level 20's exit in play (the call sequence there is two lines).
- Locators (`--debug-exe`) work on all four volumes; the game unit's offsets match in all four.

Assumed / unverified (check in DOSBox when possible):
- `tp_random` sequence vs System 2714:08D5; "Randomize" being 2714:095C.
- Crt Sound ignoring hz <= 18; BIOS teletype details (no scroll emulation; the game never needs it).
- GetDotColor returning -1 off screen (only reachable at screen edges).
- The elevator's vertical trail and other enemy visuals; freeze and dart traps (ported, never
  exercised in a run); story pages 2+ and the joystick screen (not screenshotted).
- The effect parameter meanings (direction mask, type = grain); the 125 us step estimate;
  the 70 Hz GameTick rate; DS variable addresses being the same in every volume.
- Volumes 2-4 gameplay (only renders and locators were checked).

## Outstanding issues and known gaps

- Not ported: the joystick; the text-mode exit screen (1854:60C8); StartLevel's 2714:095C
  Randomize (approximated by reseeding).
- The port **writes into the EXE's folder** (`PTOMBn.HS` on a new high score, `PTOMB.cfg` from
  Config Keyboard, `PTOMBn.SAV` on S), as the original does. Test against a copy (see below).
- Delay is `plat_delay` (ticks, U:0000), which can't be named in Ghidra because no function is
  defined there.

## Next steps (suggested order)

1. ~~Save/restore~~: done (`notes.md`, "Save and restore"); restores the user's own saves.
2. ~~Volume ending~~: done; `screenvm` runs 1854:4E01 from the EXE (`--ending` shows it).
3. Optional: decode the Genus effect jump table for exact transitions; joystick via SDL
   GameController; verify the assumptions above in DOSBox if it gets installed.

## Enhancement Ideas
1. Fix an inconsistent movement glitch where sometimes collision with a collectible like coins 
   blocks movement (e.g. interrupts a jump mid-flight) and other times not.
2. Higher frame rate and smoother movement.

## How to work here (lessons from this session)

- **Don't write C containing escapes (`\x9F`, `'\0'`, `'\\'`) through Bash heredocs or inline
  Python.** The tool layer collapsed backslashes three times, producing NUL bytes and broken
  literals. Use the Write and Edit tools for source. (Python heredocs are fine for analysis.)
- Test with `--script` (ticks are 18.2/s; commands `wait N; down KEY; up KEY; tap KEY;
  shot FILE.png; quit`; keys `left right up down space esc enter pgup pgdn` or one character).
  `--level N` skips the menus. Output paths are relative to the working directory, so run from
  a temp folder.
- For anything that saves, run against a copy: `cp game/PTOMB1.EXE game/PTOMB1.MNU
  game/PTOMB.CFG $TEMP/somedir/`, plus a crafted `.HS` if needed (the scores are TP Reals).
  The user's real `game/PTOMB1.HS` holds their own score; don't overwrite it.
- When decoding: use Ghidra's decompiler for structure, the **disassembly for exact arguments**,
  and the dump file to grep. Pascal push order is parameter order (the far-pointer segment goes
  before the offset); Ghidra lists parameters in reverse push order.
- Keep `docs/notes.md` and `analysis/PTOMB1.names.txt` updated with each decode, and commit in
  small steps with the `Co-Authored-By` line.
