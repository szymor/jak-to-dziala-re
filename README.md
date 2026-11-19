# Jak to działa? — C/SDL3 port

A reimplementation of the player for DK's *The Way Things Work* (Polish edition *Jak to działa?*, 1996) in C17 on SDL3. The port reads **your own copy** of the original CD (the ISO image or an extracted copy). No game data is included in this repository.

Status: milestone 1 of [the plan](docs/ANALYSIS_PLAN.md) — data access and a bitmap viewer. The file formats and the original's behaviour are documented in [`docs/`](docs).

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

`DATA` can be the `.iso` file or a directory with the extracted CD. An optional second argument picks the first bitmap, e.g. `AIRP0M` or `ARTICLE1/AIRP/AIRP0M.DIB`.

Keys: Left/Right previous/next bitmap, Page Up/Page Down ±20, Home/End, F11 fullscreen, Esc quit.

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

## Layout

| Path | Content |
|---|---|
| `src/vfs/` | read-only file system over the ISO (own ISO 9660 reader) or a directory |
| `src/formats/` | file format decoders (so far: DIB/BMP RLE8) |
| `src/main.c` | the SDL3 application (currently the bitmap viewer) |
| `src/tool.c` | `twtw-tool`, headless checks and dumps |
| `tools/` | Python reference readers and analysis scripts |
| `docs/` | analysis, format specs, reverse-engineering notes |
