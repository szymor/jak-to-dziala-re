# "Jak to działa" (DK *The Way Things Work*, PL, 1996): analysis plan for a C/SDL3 port

## 0. What we already know (first look at the ISO)

| Item | Finding |
|---|---|
| Product | Dorling Kindersley *The Way Things Work* (David Macaulay), Polish edition. Volume ID `DKMMTWTW`, ISO mastered 1999-01, files dated 1994–1996. |
| Main program | `DKCODE/TWTW.EXE`, 797 KB, **Win16 NE** (Windows 3.1). 12 code segments (~640 KB of code) and 74 data segments, some of them 64 KB "huge" segments. |
| Toolchain | Microsoft C/C++ 16-bit (MSVC 1.5x era): MS name mangling (`?PRINTABORTDLG@@...`), CRT `initseg.asm`. The code is probably mostly C with a little C++. |
| Imports | `KERNEL, USER, GDI, MMSYSTEM, WING, COMMDLG, WIN87EM`. Graphics go through **WinG**. Sound/video go through **MMSYSTEM/MCI**. Uses an x87 emulator (floats). |
| Engine | Data-driven "DKMM" engine. The EXE contains parser error messages for a script language: *panel*, *template*, *event*, *action*, *resource*, keywords, and the types Rect/Point/Size/RGB/Bool/Long. There is also a "Munging..." step and version, CD and data-path checks. |
| `DKCODE/TWTW.PNG` | **Not a PNG.** 650 KB `DKMM` container. It starts with **big-endian** length-prefixed strings (title, "Version 0.1 (28/3/94)", cursor name "Arrow", then the alphabetical index entries in CP1250). Big-endian data hints at a Mac-originated authoring pipeline. It probably also holds the page graph: hotspots, links, layout. |
| `*.DIB` (1703) | Standard Windows BMP, 8-bit, **RLE8**, 256-colour palette. **The article text is baked into the bitmaps**: `xxxx0M` is the main page and `xxxxNn` are the callout text boxes. So we don't need to lay out body text. |
| `*.WAV` (100) | RIFF WAVE, **MS-ADPCM**, 22050 Hz mono. SDL3 decodes this natively. |
| `*.ANI` (497) | **Custom DK animation format.** Magic string `funky!`, the header mentions 22050 (audio rate?) and 8 bpp. Probably interleaved RLE/delta frames plus audio. This is the main format to reverse engineer. |
| Content sections | `ARTICLE1-3` (machines, about 180 subjects), `PRINCIPL` (principles), `INVENTOR`, `TIMELINE`, `AZAZ` (A–Z index pages), `GLOS` (glossary), `RELA` (related "see also" panels), `WORKSHOP` (intro and workshop), `NAV`, `TITLE`, `OPTIONS`, `HELP`, `INDEX`. |
| Naming | `SSSSnXk[a]` where SSSS is a 4-letter subject code, n is the index and X is the role: `M` main, `N` note, `J`, `Z` zoom, `X`, `P` related, `R`, `G`. The suffix is `A` for animation and `S` for sound, for example `AIRP0M2A.ANI`. |
| Not relevant to the port | `WINGDRV/` (old video drivers), most of `DKCODE/*.??_` (redistributable VfW, OLE2 and WinG runtime, compressed), `DKSETUP/` (MS Setup scripts). These are still useful as references: `DKSETUP/SETUP.INF` and `*.INC` show the install layout and INI keys. |

Phase 1 results: [inventory.md](inventory.md).

**Main conclusion:** this is a fixed-function multimedia player driven by data: bitmaps, animations, sounds and a page/hotspot graph. The best port is a **clean reimplementation of the engine in C on SDL3**, built from a precise spec of the data formats and runtime behaviour. Machine-translating the decompiled 16-bit code would be worse. Ghidra pseudocode serves as the reference and fills in details.

---

## 1. Phase 1: Workspace and inventory

1. Extract the ISO into `work/iso/` (git-ignored; no assets go in git).
2. Write `tools/manifest.py`: list every file with path, size, type, BMP dimensions and compression, WAV format, and ANI header fields, and write it out as CSV/JSON.
3. Build a table of subject codes from file names and directories, and match each code to the index strings in TWTW.PNG.
4. Decompress the `*.??_` files (MS `COMPRESS.EXE` "SZDD" format; I can write a small extractor) for reference only.

**Deliverable:** `docs/inventory.md` and a machine-readable manifest.

## 2. Phase 2: Asset format reverse engineering (offline, in parallel with phase 3)

Each format gets a Python reader/dumper in `tools/`, then a written spec in `docs/formats/*.md`, then the matching C loader in the port.

| Format | Work | Difficulty |
|---|---|---|
| DIB (RLE8 BMP) | Verify palettes. Check whether all pages share one palette (WinG identity palette, 20 reserved system colours) or carry their own. Find which colour index is transparent for the N/J/Z overlays. | Low |
| WAV MS-ADPCM | Confirm `SDL_LoadWAV` handles all 100 files. | Low |
| **ANI (`funky!`)** | Map the header (frame count, size, fps, audio params, palette). Find the frame chunk layout, the compression (likely RLE/delta on 8-bit), audio interleave, keyframes and loop points. Check it by writing an ANI → PNG sequence + WAV converter and comparing against the running original. | **High** |
| **TWTW.PNG (DKMM)** | Map the container: header, string tables, record types. Find the page/panel/template records, hotspot rectangles, link targets, which ANI/WAV belongs to which hotspot, cursor IDs and index lists. Cross-reference against the EXE loader code (phase 3) so we don't guess blindly. | **High** |
| Resources inside TWTW.EXE | Extract cursors, icons, dialogs, string tables, menus and any embedded bitmaps/fonts from the NE resource table. Check whether the 64 KB data segments hold static tables such as hotspots or layout. | Medium |
| Fonts | Find out what draws runtime text (the A–Z index list, glossary, search?). It is GDI with some system or embedded font. We will need either a bitmap font or SDL3_ttf with a matching font. | Medium |

## 3. Phase 3: Static analysis of `TWTW.EXE` in Ghidra

1. Import the NE into Ghidra (MCP server available) and run auto-analysis.
2. **Name the imports:** NE imports are by ordinal. Generate ordinal→name maps for KERNEL/USER/GDI/MMSYSTEM/WING/COMMDLG from Wine's `.spec` files and apply them with a script.
3. Identify the MSVC 16-bit CRT (startup, `malloc`/`_fmalloc`/`halloc`, string, stdio, x87 emulator calls) so it can be skipped.
4. Use string xrefs (the parser messages, file-not-found, MCI errors, CD and version checks) as anchors to find the subsystems:
   - `WinMain`, window class, message loop, timer
   - configuration: INI file, data path, CD detection
   - **DKMM / TWTW.PNG loader** and record structs. Define them as Ghidra structures as they are understood.
   - **script/panel/template parser.** Find whether it parses text scripts at runtime or only in a debug/authoring mode ("Munging").
   - page model: pages, panels, hotspots, events (click, hover, timer) → actions (go to page, play ANI, play WAV, show note, back, …)
   - renderer: WinG DIB section, palette management, blitting with transparency, page transition effects
   - **ANI decoder/player** (the key to phase 2), plus audio sync through `waveOut`/MCI
   - navigation: history/back, "see also" (Patrz też), A–Z index, glossary, timeline, inventors, principles, workshop
   - cursors and hover feedback, printing (`PrintDlg`/`AbortDlg`), options screen, help, quitting
5. Keep a running document `docs/re/TWTW_EXE.md` covering the function map, structs, globals, the event/action enums, and open questions. Record the Ghidra project file location in the doc (outside git, or exported as XML).

## 4. Phase 4: Dynamic analysis (reference behaviour)

1. **Run the original under Wine. This is verified to work:** Wine 11.22 (WoW64) runs the 16-bit `TWTW.EXE` and shows the title screen. [`tools/wine-setup.sh`](../tools/wine-setup.sh) reproduces the setup. It needed two workarounds:
   - The extracted ISO is mapped as drive `D:` with drive type `cdrom` and volume label `DKMMTWTW__CDFS_`, because the app requires MSCDEX to report a CD-ROM drive.
   - The **original WinG 1.0 DLLs from the CD** are used instead of Wine's built-in `wing.dll16`, because the built-in one doesn't fill in the `BITMAPINFO` in `WinGGetDIBPointer` and the app page-faults.
2. A Windows 98 SE VM in QEMU stays as a **fallback only**, for cases where Wine's behaviour may be wrong (timing, palette, MCI). It isn't needed to start.
3. Record a **reference catalogue**: screenshots and videos of every screen type, transitions, animation timing, hover/cursor behaviour, sound triggers, keyboard shortcuts and the options screen.
4. Confirm the ANI and DKMM hypotheses with Wine tooling:
   - `WINEDEBUG=+relay` gives a full trace of 16-bit API calls (file opens, reads, seeks and MCI) with the caller's `seg:off`, which maps straight to Ghidra addresses.
   - `winedbg` gives breakpoints and memory dumps, for example of decoded frame buffers.
4. **Deliverable:** `docs/behaviour/*.md`, one spec per screen/feature, with the screenshots stored outside git.

## 5. Phase 5: Port architecture (C17 + SDL3)

Proposed layout:

```
src/
  main.c            SDL3 app callbacks (SDL_AppInit/Iterate/Event/Quit)
  platform/         window, renderer, 640x480 8-bit framebuffer -> scaled texture,
                    palette handling, timing, audio stream mixing
  vfs/              asset access: extracted directory OR read directly from the ISO
                    (small ISO9660 reader), case-insensitive 8.3 lookup
  formats/          bmp_rle8.c, ani.c, dkmm.c (TWTW.PNG), wav (SDL), ne_res.c (cursors/fonts)
  engine/           page graph, panels, hotspots, event->action dispatcher,
                    navigation/history, animation & sound scheduling
  ui/               cursors, text rendering (index/glossary lists), options, help
  text/             CP1250 -> UTF-8, Polish collation for the A–Z index
tools/              Python format dumpers/validators (not shipped)
```

Key decisions:
- **Rendering:** keep an internal 8-bit indexed 640×480 framebuffer, which is faithful and handles palette effects. Convert it to RGBA for an `SDL_Texture` every frame, and use `SDL_SetRenderLogicalPresentation` for integer/aspect scaling.
- **Audio:** `SDL_AudioStream`. Sound effects and ANI audio are mixed in software; we don't need SDL_mixer.
- **Time:** run a fixed-rate logic tick that matches the original timer, so animations play at the same speed.
- **Data:** the port reads the user's original ISO or files. Assets are never redistributed.
- **Endianness and portability:** use explicit LE/BE readers and no packed-struct casts, so it works on ARM, macOS and Windows.
- **Possible extra dependencies:** SDL3_ttf, only if runtime text can't use a bitmap font taken from the original. No others are expected.
- **Dropped or replaced:** printing (could become "export page as PNG"), CD/version checks, WinG/VfW installer logic.

## 6. Phase 6: Implementation milestones

1. Asset viewer: browse every DIB and play WAV files. This validates the VFS and palettes.
2. ANI player: plays every `.ANI` with sound in sync. Compare against the phase 4 videos.
3. DKMM dump to JSON: the full page graph is readable, and every referenced file exists.
4. Engine MVP: title → navigation map → one article with working hotspots, notes, animations and "see also".
5. All sections: machines, principles, inventors, timeline, A–Z, glossary, workshop, help, options.
6. Polish: transitions, cursors, keyboard support, settings persistence, packaging for Linux, Windows and macOS.
7. Verification: a screenshot regression harness comparing port frames against the reference captures for chosen pages.

## 7. Risks and open questions

- **ANI codec and DKMM layout** are unknown until we RE them. They are the critical path, so phases 2 and 3 should run side by side.
- Ghidra's decompiler is weak on 16-bit segmented code (far pointers, `huge` arithmetic, x87 emulator interrupts). Expect manual cleanup.
- If the script language exists only in source form (not on the CD), the behaviour must be inferred from the compiled records alone.
- Palette tricks, such as WinG identity palettes and palette animation, may need exact emulation.
- Legal: keep only original code, specs and tools in this repository. Never commit the ISO or extracted assets (`.gitignore` already excludes the ISO).

---

## 8. Software

**Installed and verified:** Ghidra (+ MCP), radare2, Python 3 with **Pillow 13 and numpy 2.5**, **Wine 11.22 (Win16 works, see phase 4)**, QEMU (i386), ffmpeg/ffprobe, ImageMagick, 7-Zip (it also unpacks the MS `SZDD` `*.??_` files), GCC/Clang, CMake/Meson/Ninja, GDB, **SDL3 3.4.20, SDL3_ttf 3.2.2, SDL3_image 3.4.8**, and the `pl_PL.UTF-8` locale.

**Fetched:** Wine sources, sparse checkout in `third_party/wine-src/` (git-ignored). The `.spec` files there name the KERNEL/USER/GDI/MMSYSTEM/WING/COMMDLG/WIN87EM ordinals.

**No longer needed:** a Windows 98 SE image (kept only as an optional fallback) and DOSBox-X.

**Please provide (optional, small):**

1. **`xorg-server-xvfb` and `xdotool`** (`sudo pacman -S xorg-server-xvfb xdotool`). These let me run the original in an isolated virtual X display, take screenshots and click through screens automatically. Without them I can only grab your real screen with Spectacle. That works, but it captures your whole desktop and fights with your mouse. I can't install them myself because they need sudo.
2. *(Nice to have)* An **English** edition of *The Way Things Work* (1994/1996 DK CD), or another DK title on the same engine, for comparison. Diffing them speeds up format RE, and English strings help label code.
