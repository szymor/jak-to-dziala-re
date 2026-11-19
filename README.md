# Jak to działa? — C/SDL3 port

A reimplementation of the player for DK's *The Way Things Work* (Polish edition *Jak to działa?*, 1996) in C17 on SDL3. The port reads **your own copy** of the original CD (the ISO image or an extracted copy). No game data is included in this repository.

Status: milestone 2 of [the plan](docs/ANALYSIS_PLAN.md) — all file formats readable in C, and a viewer that shows bitmaps and plays animations with sound. The file formats and the original's behaviour are documented in [`docs/`](docs).

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

`DATA` can be the `.iso` file or a directory with the extracted CD. An optional second argument picks the first item, e.g. `AIRP0M`, `AIRP0M1A` or `ARTICLE1/AIRP/AIRP0M.DIB`. Bitmaps and animations are listed together; an animation plays with its sound over its page bitmap, timed by the audio clock.

Keys: Left/Right previous/next, Page Up/Page Down ±20, Home/End, Space replay, F11 fullscreen, Esc quit.

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

## Layout

| Path | Content |
|---|---|
| `src/vfs/` | read-only file system over the ISO (own ISO 9660 reader) or a directory |
| `src/formats/` | file format readers: DIB (RLE8, 1/4/8-bit), ANI, TWTW.PNG (DKMM), NE resources of TWTW.EXE |
| `src/platform/` | the 640×480 indexed screen and its SDL texture |
| `src/engine/` | the ANI player (audio clock as master) |
| `src/main.c` | the SDL3 application (currently the bitmap/animation viewer) |
| `src/tool.c` | `twtw-tool`, headless checks and dumps |
| `tools/` | Python reference readers and analysis scripts |
| `docs/` | analysis, format specs, reverse-engineering notes |
