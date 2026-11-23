# Jak to działa? — C/SDL3 port

A reimplementation of the player for DK's *The Way Things Work* (Polish edition *Jak to działa?*, 1996) in C17 on SDL3. The port reads **your own copy** of the original CD (the ISO image or an extracted copy). No game data is included in this repository.

Status: milestones 1–6 of [the plan](docs/ANALYSIS_PLAN.md) are done — every section, the Index, help and Options screens play, with the original's rules taken from its code (`docs/re/TWTW_EXE.md`) and checked against the running original. The file formats and the original's behaviour are documented in [`docs/`](docs).

## Build

Needs CMake ≥ 3.20, a C17 compiler and SDL3.

```bash
cmake -S . -B build -G Ninja
```

```bash
cmake --build build
```

## Run

```bash
build/twtw "Jak To Działa.iso"
```

`DATA` can be the `.iso` file or a directory with the extracted CD. It is remembered: later, `build/twtw` alone starts with the same data, and if no data was ever given, a file dialog asks for the CD image. Options (also remembered): `--fullscreen` / `--windowed`, `--integer-scale` (whole-pixel scaling) / `--smooth-scale`. The settings live in SDL's preferences folder (`~/.local/share/twtw/Jak to dziala/settings.cfg` on Linux).

Mouse as in the original. The original has no keyboard shortcuts apart from the Index screen (typing, caret keys, Up/Down/Page Up/Page Down/Home/End, Enter); the port adds Backspace = Wróć, Esc = stop sound/animation or close the top pop-up, and F11 or Alt+Enter = fullscreen.

```bash
build/twtw --viewer "Jak To Działa.iso" AIRP0M1A
```

opens the bitmap/animation browser instead (Left/Right, Page Up/Down, Home/End, Space replay). For testing, `--page N` starts the game at object `N` of TWTW.PNG, and `SDL_LOGGING=app=debug` logs page, pop-up and media changes.

## Install and packages

```bash
cmake --install build --prefix ~/.local
```

installs `twtw`, `twtw-tool`, the README, the license and (on Linux) a desktop entry.

```bash
cd build && cpack
```

makes `.tar.gz` / `.zip` archives of the same files (they need SDL3 and SDL3_ttf on the system). No game data is ever installed or packaged.

## Checks

```bash
build/twtw-tool check-vfs "Jak To Działa.iso" work/cd
```

compares the built-in ISO 9660 reader with an extracted copy (all 2534 files must match).

```bash
build/twtw-tool dump-dibs "Jak To Działa.iso" work/dibdump
```

```bash
python3 -I tools/check_dibs.py work/cd work/dibdump
```

decode all 1703 bitmaps with the port's decoder and compare pixels and palettes with Pillow.

```bash
build/twtw-tool dkmm-dump "Jak To Działa.iso" > work/dk_c.txt
```

```bash
python3 -I tools/dkmm.py canonical work/cd/DKCODE/TWTW.PNG > work/dk_py.txt
```

The two page-file dumps (strings, templates, index, all 1668 objects) must be identical: `cmp work/dk_c.txt work/dk_py.txt`.

```bash
build/twtw-tool ani-hash "Jak To Działa.iso" | sort > work/ani_c.txt
```

```bash
python3 -I tools/ani.py hash work/cd | sort > work/ani_py.txt
```

Frame and audio hashes of all 495 animations must be identical (the two damaged `QLVR` files fail in both).

```bash
build/twtw-tool res-dump "Jak To Działa.iso" work/res_c
```

writes the EXE's 41 UI bitmaps (byte-identical to `tools/ne_resources.py` output) and the hand cursor.

`build/twtw-tool selftest "Jak To Działa.iso"` checks every reference in the page data and opens every page and pop-up through the engine.

`tools/port.sh` drives the game headless (Xvfb, dummy audio) for screen tests: `start [--page N]`, `click X Y`, `shot FILE.png`, `stop`.

### Regression against the original

`tools/regress.py` plays a fixed list of screens (`python3 -I tools/regress.py list`) in the original program under Wine (`tools/orig.sh`, set up once with `tools/wine-setup.sh`) and in the port, and compares the pictures. It needs the extracted CD in `work/cd` and Xvfb, xdotool, ImageMagick, Wine.

```bash
python3 -I tools/regress.py capture-orig
```

captures the references once (a few minutes: every screen starts from a fresh launch and goes through the title).

```bash
python3 -I tools/regress.py run
```

captures the port and compares; the exit status is non-zero if any screen differs. Masked out are only the places where Wine itself differs from Windows: pop-up title strips and the white squares Wine paints in pop-up shadows, plus per-screen masks for the blinking A–Z lamp and font-rendered Index text. Results go to `work/regress/` (`report.txt`, and `diff/NAME.png` with differences in magenta, masks in blue). The shots are derived from the game's pictures, so they are never committed.

## Layout

| Path | Content |
|---|---|
| `src/vfs/` | read-only file system over the ISO (own ISO 9660 reader) or a directory |
| `src/formats/` | file format readers: DIB (RLE8, 1/4/8-bit), ANI, TWTW.PNG (DKMM), NE resources of TWTW.EXE |
| `src/platform/` | the 640×480 indexed screen buffer (shown: the original's 638×459 working area), its SDL texture, the settings file |
| `src/engine/` | the game engine (`game.c`: pages, pop-ups, actions, media, nav) and the ANI player |
| `src/main.c`, `src/viewer.c` | the SDL3 application: game mode and viewer mode |
| `src/tool.c` | `twtw-tool`, headless checks and dumps |
| `tools/` | Python reference readers and analysis scripts |
| `docs/` | analysis, format specs, reverse-engineering notes |

## License

The port's code, tools and documentation are free software: you can redistribute them and/or modify them under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE).

The license covers this repository only. *Jak to działa?* / *The Way Things Work* and its content (pictures, animations, sounds, texts and the original program) belong to their copyright holders and are not part of this project; the player needs your own copy of the CD.
