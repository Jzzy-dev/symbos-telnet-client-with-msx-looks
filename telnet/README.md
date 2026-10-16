# SymTerm — ANSI/BBS telnet client for SymbOS

A telnet client for SymbOS that uses the MSX2's full 512×212 16-colour mode:
an **80×25 text screen with a 6×8 CP437 font (480×200 pixels) in 16 colours**,
so BBS menus, ANSI art and box-drawing UIs look the way they do on a native
80-column MSX terminal.

## Running

Copy `symterm.exe` to your SymbOS disk. A network daemon must be running.
SymbOS must be in a 16-colour screen mode (MSX2/2+/turbo R: 512×212×16).

```
symterm                      opens the window and asks for Host[:port]
symterm bbs.example.org      connects straight away (port 23)
symterm bbs.example.org 2323
symterm host:2323 -u -p
```

| Option | Meaning |
| ------ | ------- |
| `-r`   | raw TCP, no telnet negotiation |
| `-u`   | start in UTF-8 mode (default is CP437, the BBS standard) |
| `-p`   | exact ANSI palette (see below) |
| `-lNN` | number of text rows, 10–25 (default 25; reported to the BBS via NAWS) |

### Keys

| Key | Action |
| --- | ------ |
| Alt+X | hang up (back to the `Host>` prompt; Enter reconnects) |
| Alt+U | toggle UTF-8 / CP437 decoding |
| Alt+P | toggle exact ANSI palette |
| Alt+E | toggle local echo |
| Cursor keys, Home, End, Ins, F1–F10 | sent as ANSI/VT sequences |

### Colours

The default SymbOS palette has no dark cyan, dark magenta or dark grey, so by
default those ANSI colours share an entry with their bright versions. With
**Alt+P** (or `-p`) SymTerm reprograms the 16 hardware palette entries to the
real ANSI/VGA colours while its window has focus, and restores your desktop
palette when it loses focus or exits.

### Screen height

25 rows need 200 pixel lines. With the title bar and taskbar the window may be
a few lines shorter than that on a 212-line screen; the window then scrolls
automatically to keep the cursor row visible. Use `-l24` if you prefer the
whole screen to fit.

## What's supported

* Telnet: ECHO, SGA, BINARY, TTYPE (reports `ANSI`), NAWS (80×rows); IAC escaping; Enter sends CR NUL
* ANSI/VT100: cursor movement (`A B C D E F G H f d a e` `` ` ``), erase (`J K X`), insert/delete
  lines and characters (`L M @ P`), scroll (`S T`), scroll regions (`r`), save/restore
  cursor (`s u`, `ESC 7/8`), repeat (`b`), `ESC D/E/M/c`, `?25` cursor visibility, `?7` autowrap,
  `ESC[2J` homes the cursor like ANSI.SYS
* SGR: bold/bright, reverse, conceal, 16 foreground/background colours, `90–97`/`100–107`,
  and 256-colour / true-colour (`38;5`, `38;2`, `48;…`) mapped to the nearest of the 16
* Device status (`ESC[6n`, `ESC[5n`) and device attributes (`ESC[c`) replies, used by BBSes to detect ANSI
* Full CP437 character set, including box drawing, blocks and shades drawn to tile seamlessly in the 6×8 cell

## How it works

* The screen is 25 separate 1944-byte canvases in **banked memory** (≈48 KB, outside the
  app's 64 KB space), each shown by two `C_IMAGE_EXT` controls (252 + 228 pixels wide).
* Text and attributes are kept in an 80×25 buffer. Changed cells are rendered by a Z80
  routine (`render.s`) into a page-aligned scratch row, then copied into the row's canvas
  with `Bank_Copy`, and only the changed rectangle is redrawn.
* **Scrolling doesn't move pixels**: the controls are re-pointed at the canvases in rotated
  order, and only the new bottom row is drawn. Scrolls are batched per network chunk.
* The renderer uses an expanded font (24 bytes per glyph, "snake" line order) and a
  per-attribute colour table, so each pixel pair is one table lookup: about 1,000 T-states
  per character (≈22 ms for a full 80-column row at 3.58 MHz).

## Building

Requires the [SymbOS C Compiler](https://github.com/danielgaskell/scc).

```
./build.sh          # Linux/macOS (set SCC=/path/to/scc)
build.bat           # Windows, from SCC's bin folder
```

### Files

| File | |
| ---- | - |
| `symterm.c` | terminal, ANSI parser, telnet, window and display engine |
| `render.s` | Z80 glyph renderer |
| `font.h` | generated 6×8 CP437 font + Unicode table |
| `icons/` | app icons (4- and 16-colour) |
| `tools/gen_font.py` | font generator (from the public-domain X11 misc-fixed 6×9 font) |
| `tests/` | host simulation and Z80 renderer test |

### Tests

`tests/asmtest.c` runs the real `tr_render` machine code from `symterm.exe` in a Z80
emulator ([superzazu/z80](https://github.com/superzazu/z80)) and compares it with a C
reference. `tests/hostsim.c` compiles `symterm.c` against stubbed SymbOS calls, emulates
banked memory and the desktop's control redraws, plays back a scripted BBS session
(`tests/make_session.py`) and checks the displayed pixels against an ideal render.

```
gcc -I path/to/superzazu-z80 tests/asmtest.c path/to/z80.c -o asmtest && ./asmtest symterm.exe symterm.exe.map
gcc -Itests/host -I. tests/hostsim.c -o hostsim
python3 tests/make_session.py session.bin && ./hostsim session.bin shot.ppm
```

Font glyphs are derived from the public-domain X11 misc-fixed font.
