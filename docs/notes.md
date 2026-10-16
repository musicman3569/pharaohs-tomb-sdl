# Reverse engineering notes

## Executables

- `PTOMB1..4.EXE` (one per volume) are packed with **LZEXE 0.91**. `tools/unlzexe.py` unpacks them.
- PTOMB1 unpacked: 128,816-byte image, 1,399 relocations, entry `0000:8473`, stack `280E:BB80`.
- All four have the same relocation count and almost the same size, so they are likely the same
  engine with different level data built in.

## Compiler: almost certainly Turbo Pascal

- Entry point: a chain of `call far` to unit initializers (`9A .. .. .. ..` x5), then
  `push bp; mov bp,sp`. This is how a Turbo Pascal main program begins.
- Strings are length-prefixed (Pascal `string[N]`), e.g. `!PRESS A BUTTON WHEN THIS IS DONE,`
  where `!` (0x21 = 33) is the length byte.
- The far-call target segments are the units (System, Crt, Dos, game units). The most-referenced
  segment, `0x1714`, is probably the System unit (RTL).

## Files next to the EXE

| File          | Size  | Guess                         |
|---------------|-------|-------------------------------|
| `PTOMB.CFG`   | 1     | config flag (joystick/sound?) |
| `PTOMBn.HS`   | 1310  | high score table              |
| `PTOMBn.MNU`  | 5722  | menu / title screen data      |
| `PTOMBn.SAV`  | 17    | saved game                    |
