# "Jak to działa" (DK *The Way Things Work*, PL, 1996): plan for a C/SDL3 port

Status (2026-11-19): phases 1–4 are done; port milestones 1–4 are done (see §6). Revised after the analysis; the original first-look version is in git history.

## 0. What we know

| Item | Finding | Spec |
|---|---|---|
| Program | `DKCODE/TWTW.EXE`, Win16 NE, MSVC 1.5x **C++** on DK's own framework (not MFC). It contains the whole authoring runtime (text page parser, "Scrunge" compiler, property editor, expression evaluator); the port needs none of it. | [re/TWTW_EXE.md](re/TWTW_EXE.md) |
| Page data | `DKCODE/TWTW.PNG` = the compiled ("scrunged") page file, big-endian. String pool, window/page templates, an index table of 1320 titles, 1668 object records. **Everything decodes byte-exactly except one list-box object.** | [formats/dkmm.md](formats/dkmm.md) |
| Bitmaps | 1703 RLE8 DIBs with **one shared palette** (WinG identity layout). All article text is baked into the pictures. | [inventory.md](inventory.md) |
| Sound | 100 WAV files, MS-ADPCM / PCM 22 kHz mono. `SDL_LoadWAV` handles both. | [inventory.md](inventory.md) |
| Animation | 495 `funky!` ANI files: chunks of 8-bit PCM plus byte-RLE dirty rectangles, audio stored 1–2 s ahead. **Decoded completely.** Drawn opaque at the header's fps (5 fps for almost all). | [formats/ani.md](formats/ani.md) |
| EXE resources | 41 UI bitmaps (scroll bar, OK/Cancel), hand cursor 107, palette, dialogs, strings. | [formats/exe_resources.md](formats/exe_resources.md) |
| Behaviour | Verified against the original running under Wine (screenshots and 25 fps recordings): layout, pop-ups, index, actions, events, dial, Back, context navigation. No transition effects are used. | [formats/dkmm.md](formats/dkmm.md#runtime-verification-wine-headless), [re/TWTW_EXE.md](re/TWTW_EXE.md) |

**Conclusion (unchanged, now backed by specs):** reimplement the player in C17 on SDL3 from the format specs. Don't translate decompiled code.

## 1–4. Analysis phases: status

| Phase | Status | Result |
|---|---|---|
| 1. Inventory | done | `docs/inventory.md`, `docs/inventory/manifest.csv`, `tools/manifest.py`, `tools/unpack_szdd.py` |
| 2. Formats | done | `tools/ani.py`, `tools/dkmm.py`, `tools/ne_resources.py`; specs in `docs/formats/` |
| 3. Static RE | done for the port's needs | `docs/re/TWTW_EXE.md`; Ghidra project `work/twtw.gpr` (git-ignored) |
| 4. Dynamic | mostly done | `tools/wine-setup.sh`, `tools/orig.sh` (headless Wine + Xvfb + xdotool); runtime checks are written into the specs. Not done: a systematic reference capture set for regression tests (moved to milestone 7), and sound behaviour (see risks). |

## 5. Port architecture (C17 + SDL3), revised

```
src/
  main.c            SDL3 app callbacks (SDL_AppInit/Iterate/Event/Quit)
  platform/         window + renderer (640x480 logical, scaled), audio device, clock
  vfs/              read the user's ISO directly (small ISO 9660 reader) or an extracted
                    directory; case-insensitive lookup
  formats/          dib.c (RLE8), ani.c, dkmm.c (TWTW.PNG), ne_res.c (bitmaps/cursor from TWTW.EXE)
  engine/           object store (lazy record parsing), panel stack (page + pop-ups),
                    hotspot hit testing, event/action dispatch, history, context links,
                    animation and sound players
  ui/               widgets that the original took from Windows: pop-up title strip,
                    scroll view (help), list box + edit field (index), bitmap buttons
  text/             CP1250 -> UTF-8, rendering for the index list/edit field
```

Decisions, updated with what the analysis showed:

- **Screen:** a 640×480 logical surface. Page area at (94, 4), nav template at (0, 0), frame strips as decoded. Scale to the window with `SDL_SetRenderLogicalPresentation`.
- **Pixels:** keep an 8-bit indexed framebuffer with the single global palette, converted to an RGBA texture on change. It's faithful and simple, and needs no palette animation (the palette fades are unused).
- **Pop-ups:** a stack of panels. Each pop-up has an 8-px title strip above its bitmap (the original used tiny child windows). Action 1 replaces the top pop-up, action 3 pushes a pop-up or navigates, a click outside closes it, and action 13 closes it.
- **Transitions:** none (the original shows none). Optional later; the 19 effects are documented.
- **Animation and sound sync:** the audio clock is the master. The ANI's PCM goes into an `SDL_AudioStream`, and the displayed frame is `floor(played_samples × fps / rate)`, corrected for the device latency SDL reports. Frames are never dropped on a modern machine. See the note below on frame rate.
- **Sound effects:** WAV through an `SDL_AudioStream`. One medium (sound or animation) at a time; while one plays, a click only stops it (verified), so no mixing is needed.
- **Text:** only the Index screen draws text at runtime (list box + edit field, style `Times New Roman CE`, plus the colours from the template section). We can't ship that font, so use **SDL3_ttf** with a metric-compatible free serif (Liberation Serif or TeX Gyre Termes), letting the user point to their own TNR.
- **EXE resources:** load the UI bitmaps and the hand cursor from the user's `TWTW.EXE` (`ne_res.c`), so no assets are shipped. The other cursor is the system arrow (`SDL_CreateColorCursor` / `SDL_SYSTEM_CURSOR_DEFAULT`).
- **Options:** "copy window" → `SDL_SetClipboardData` with a BMP/PNG of the frame. Print → "save page as PNG" (and the system print dialog only if it's ever wanted).
- **Dropped:** CD/MSCDEX/version checks, the INI data path (replaced by a command-line/config path to the ISO), WinG, and the authoring features.
- **Portability:** explicit big/little-endian readers, no packed-struct casts. CMake. Linux, Windows and macOS.
- **Dependencies:** SDL3 and SDL3_ttf (installed). Nothing else.

### Note: can the port improve sound/video sync or frame rate?

- **Sync, yes.** The original timed frames on a 1994 PC through `waveOut` buffers and the Windows timer. Under Wine it showed only every second frame (a new picture every 0.4 s), though that may come from Wine or the capture load rather than the original design; we haven't checked on real hardware. The port can show **every** frame at exactly its sample position, with latency compensation. That's equal to the original on its best day, and better than it on a slow machine.
- **Frame rate, not really.** The animations were drawn at 5 fps (a few at 1–4 fps). Frames between them don't exist in the data. Faster display wouldn't make motion smoother, only more evenly timed. The two ways to invent in-between frames don't suit this art:
  - Cross-fading between frames: cheap, but looks soft and ghosty, and needs RGBA blending instead of indexed pixels.
  - Motion interpolation (optical flow or ML): artefacts on hand-drawn 8-bit line art with dithering, plus a heavy dependency.
- **Recommendation:** be frame-exact at the authored rate as the default, and treat cross-fade at most as an optional experiment, off by default.

## 6. Implementation milestones (revised)

1. ✅ **Skeleton:** CMake, SDL3 window, VFS (directory + ISO 9660), DIB decoder. Shows any bitmap by name.
2. ✅ **Formats in C:** `ani.c` (with an audio-clock player), `dkmm.c` (a port of `tools/dkmm.py`), `ne_res.c`. Test: a C dump of TWTW.PNG matches the Python dump byte for byte, and all 495 ANIs decode to the same frames as `tools/ani.py`.
3. ✅ **Engine MVP:** title → Workshop → nav bar → one article with hotspots, pop-ups, animations, sounds, "Patrz też", Back. Verified headless against screenshots of the original: Workshop and the battery page + zoom pop-up are pixel-identical (apart from the deliberately Windows-3.1-styled pop-up title strip); the title sequence, context nav (Historia → `TT050m`), Back, outside-click and media-stop rules behave as recorded. Found on the way: pop-ups cast a 4-px checkerboard shadow (black on every other pixel, the image shows through). Class-7 "shape": 36 = autoplay on open, skipped by a click; 20 and 4 = play when clicked. Corrected after user testing: there is no idle animation loop, and a click during an auto-play animation skips it (both verified with traces).
4. ✅ **All sections:** principles, inventors, timeline, A–Z dial, glossary links, mammoth stories, context nav buttons. Done: A–Z dial (rules verified in the original), principles hub, timeline, inventors, glossary links, mammoth stories and context nav all run from the data. `twtw-tool selftest` resolves every reference of all 1668 objects and opens all 399 pages and 1265 pop-ups. Pixel checks against the original: timeline, principles and inventors hubs identical; A–Z dial idle/armed identical; Options and mammoth-stories pop-ups identical apart from Wine's window frame. Fixed on the way: animations with less sound than pictures never finished (nav buttons); EXE button bitmaps and the line under the pop-up strip.
5. **Widgets:** help scroll views (class 9 + 10), Index screen (list box with Polish-collation lower-bound search, edit field, scroll bar, OK/Anuluj, Enter), Options (copy, save PNG).
6. **Polish:** cursors, keyboard (Esc closes the pop-up, Backspace = Back), window scaling and fullscreen, settings, packages.
7. **Regression harness:** reuse `tools/orig.sh` plus the pixel matcher to capture reference frames from the original, and compare port frames against them for a fixed list of screens.

## 7. Risks and open questions (current)

Resolved since the first revision: the Index screen (all classes decode; search = Polish-collation lower bound, verified), sound behaviour (one medium at a time, any click stops the playing medium, verified by `waveOut` trace), and the class-10 scroll bar layout.

- **Font metrics (low):** the Index list and edit field use `Times New Roman CE`, rows 12 px apart. A metric-compatible serif (Liberation Serif / TeX Gyre Termes) at that size will look close but not identical. The user can supply their own TNR.
- **Constant fields (low):** the action 4/7 parameters and the two raw base-template headers never vary in the data; the port ignores them.
- **Wine-only observations (low):** a note pop-up opened from the Index once painted blank in Wine; the original Win3.1 rendering is assumed to be normal. Animation frame skipping seen under Wine isn't reproduced by design.
- **Legal:** the repository holds only code, specs and tools. The ISO, extracted assets and the Ghidra database stay out of git.

## 8. Software

Installed and used: Ghidra (+ MCP), Python 3 with Pillow and numpy, Wine 11.22, Xvfb, xdotool, ImageMagick, ffmpeg, 7-Zip, GCC/Clang, CMake/Ninja, SDL3 3.4.20, SDL3_ttf 3.2.2. Wine sources (sparse) are in `third_party/` (git-ignored). Nothing else is needed for the port. Still nice to have: an English edition for comparison.
