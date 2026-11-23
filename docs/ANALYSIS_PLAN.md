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
- **Pop-ups:** a stack of panels. Each pop-up has an 8-px title strip above its bitmap (the original used tiny child windows). Action 1 stacks a new pop-up, action 3 changes the page in place (the base window, or the pop-up of the same template), a click outside the top pop-up closes pop-ups down the stack until it lands in one, and action 13 closes one.
- **Transitions:** none (the original shows none). Optional later; the 19 effects are documented.
- **Animation and sound sync:** the audio clock is the master. The ANI's PCM goes into an `SDL_AudioStream`, and the displayed frame is `floor(played_samples × fps / rate)`, corrected for the device latency SDL reports. Frames are never dropped on a modern machine. See the note below on frame rate.
- **Sound effects:** WAV through an `SDL_AudioStream`. One medium (sound or animation) at a time; while one plays, a click on an ordinary hotspot only stops it (the hotspot flags decide; see docs/formats/dkmm.md), so no mixing is needed.
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
3. ✅ **Engine MVP:** title → Workshop → nav bar → one article with hotspots, pop-ups, animations, sounds, "Patrz też", Back. Verified headless against screenshots of the original: Workshop and the battery page + zoom pop-up are pixel-identical (apart from the deliberately Windows-3.1-styled pop-up title strip); the title sequence, context nav (Historia → `TT050m`), Back, outside-click and media-stop rules behave as recorded. Found on the way: pop-ups cast a 4-px checkerboard shadow (black on every other pixel, the image shows through). Class-7 "shape" (later found to be a flags word): 36 = autoplay on open; 20 and 4 = play when clicked. Corrected after user testing: there is no idle animation loop (verified with traces). Corrected later from the code: a click on an auto-play page moves on through the page's flag-1 hotspots, not by firing event 6.
4. ✅ **All sections:** principles, inventors, timeline, A–Z dial, glossary links, mammoth stories, context nav buttons. Done: A–Z dial (rules verified in the original), principles hub, timeline, inventors, glossary links, mammoth stories and context nav all run from the data. `twtw-tool selftest` resolves every reference of all 1668 objects and opens all 399 pages and 1265 pop-ups. Pixel checks against the original: timeline, principles and inventors hubs identical; A–Z dial identical (its OK lamp later turned out to blink, see `docs/formats/dkmm.md`); Options and mammoth-stories pop-ups identical apart from Wine's window frame. Fixed on the way: animations with less sound than pictures never finished (nav buttons); EXE button bitmaps and the line under the pop-up strip.
5. ✅ **Widgets:** help scroll views (class 9 + 10), Index screen (list box with Polish-collation lower-bound search, edit field, scroll bar, OK/Anuluj, Enter), Options (copy to clipboard, print → save PNG). The widget rules come from TWTW.EXE's code (`docs/re/TWTW_EXE.md`, "Object classes and the widget code"): position/pixel rounding, page steps (1 on help pages, trunc(rows × 0.9) in the list), a 300 ms first repeat then 100 ms while the pointer stays on the part, the two-part track, a 2-px caret. Pixel checks: help scrolling (positions 0–3 and a track click) identical apart from Wine's frame; the Index list band, rows and edit-field caret line up with the original (the glyphs differ only because Wine substitutes the font).
6. ✅ **Polish**, each item from the code first: the nav-button mapping (FUN_1048_ebfc: the id's slot in the template picks the handler) and Back (base pages only); the original's keyboard handling (none outside the Index: the edit box's caret keys with Shift selection, the list's Up/Down/Page/Home/End and Enter), with Esc, Backspace = Wróć and F11/Alt+Enter as marked port additions; the EXE's named cursors (the game's own arrow for `normal`); a settings file (data path, fullscreen, integer scaling) with a file dialog on first run; `cmake --install` and CPack archives.
7. ✅ **Regression harness:** `tools/regress.py` plays 13 fixed screens (Workshop, the four hubs, A–Z dial, Options, mammoth stories, battery + zoom pop-up, a stacked glossary pop-up, help contents, a help page and its scrolled state, the Index with typed text) in the original under Wine and in the port, from a fresh launch through the title each time, and compares the client area. Masks cover only Wine's own differences (pop-up title strips, the shadow areas Wine erases white — the code shows the original keeps the picture there) and per-screen items (the blinking A–Z lamp, font-rendered Index text). Result: 13/13 identical; a deliberately wrong shot fails. Found on the way: the "first click after the title is swallowed" note was the harness clicking during the credits, which play over the Workshop picture.

## 7. Risks and open questions (current)

Resolved since the first revision: the Index screen (all classes decode; search = Polish-collation lower bound, verified), sound behaviour (one medium at a time; a click in a window with a flag-4 hotspot stops it, from the code and `waveOut` traces), the nav-button mapping (from the code), and the class-10 scroll bar layout.

- **Font metrics (low):** the Index list and edit field use `Times New Roman CE`, rows 12 px apart. A metric-compatible serif (Liberation Serif / TeX Gyre Termes) at that size will look close but not identical. The user can supply their own TNR.
- **Constant fields (low):** the action 4/7 parameters and the two raw base-template headers never vary in the data; the port ignores them.
- **Harness coverage (low):** the 13 screens are static; animations, sounds and transitions in time are checked by the earlier traces, not by the harness.
- **Wine-only observations (low):** a note pop-up opened from the Index once painted blank in Wine; the original Win3.1 rendering is assumed to be normal. Animation frame skipping seen under Wine isn't reproduced by design.
- **Legal:** the repository holds only code, specs and tools. The ISO, extracted assets and the Ghidra database stay out of git.

## 8. Software

Installed and used: Ghidra (+ MCP), Python 3 with Pillow and numpy, Wine 11.22, Xvfb, xdotool, ImageMagick, ffmpeg, 7-Zip, GCC/Clang, CMake/Ninja, SDL3 3.4.20, SDL3_ttf 3.2.2. Wine sources (sparse) are in `third_party/` (git-ignored). Nothing else is needed for the port. Still nice to have: an English edition for comparison.
