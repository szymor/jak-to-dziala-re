# TWTW.EXE: static analysis notes (Phase 3)

The Ghidra project is `work/twtw.gpr` (git-ignored; recreate it by importing `work/cd/DKCODE/TWTW.EXE`). Addresses below are Ghidra `segment:offset`; segment 1000 = NE segment 1, 1008 = 2, and so on.

## Ghidra setup that matters

Without these settings the decompiler drops every call argument in this 16-bit code:

- Analysis options: **Decompiler Parameter ID** on, its **Prototype Evaluation** set to `__cdecl16far`, and **Stack.Create Param Variables** on. Then re-run auto-analysis.
- WinG imports named from Wine's `wing.dll16.spec` (`WinGCreateDC` = 1001 … `WinGBitBlt` = 1010). The other system DLLs are named by Ghidra's own win16 export tables.
- `1000:02B6` is MSVC's `_chkstk` (AX = frame size), with its prototype set to `void _chkstk(void)`.
- Remaining noise: 32-bit returns in DX:AX show up as `in_DX`, and the C++ virtual calls `(*(code*)*puVar)()` need class structures. Constants pushed as segment values sometimes decompile as bogus `s_M6109__MATH…` string offsets.

## Program structure

- Compiled with MSVC 1.5x as C++. The EXE exports mangled names (`?PRINTABORTPROC@@…`). It isn't MFC: there are no `Afx` strings. It's DK's own framework.
- `entry` (1000:001A) is standard MSVC startup and calls `WinMain` (1000:53FA). That forwards to 1048:6952, the framework's main loop: `app = 1028:C188()`, then `app->vtbl[+4](cmdline)` (init), a loop over `app->vtbl[+0xC]()` (run step), and `app->vtbl[+8]()` (exit).
- The EXE contains the authoring runtime: a text page parser (1038:3330 dispatches on keyword strings), a "Scrunge" compiler, an object property editor (dialogs `Object Properties`, `Object Event Procedure`), and a script expression evaluator (1030:8D7C: typed int/bool/string/rect/point/double values with `== > < != >= <= + - * / %`). The port only needs the compiled data (docs/formats/dkmm.md).

## Function map (named in the Ghidra project)

| Address | Name | Role |
|---|---|---|
| 1000:53FA | `WinMain` | forwards to the framework main loop 1048:6952 |
| 1000:02B6 | `_chkstk` | MSVC stack probe |
| 1000:D838 | `ErrorBoxById` | message box with a string-table error (e.g. 0x465 "invalid resource") |
| 1000:AE96 | `RleDecode` | ANI/sprite RLE decoder, 32-bit asm (see below) |
| 1000:AF0A | `RleDecodeStride` | same, writing rows with a destination stride |
| 1000:B2E6 | `RleDecodeKeyed` | same, skipping pixels equal to a key index (transparency) |
| 1000:B36E | `RleDecodeKeyedStride` | keyed and strided |
| 1028:BB20 | – | frame blitter: creates a WinG bitmap for the frame rectangle and picks one of the four decoders |
| 1008:469E | `PanelRepaint` | repaints a panel; starts a transition when the panel's transition field is set |
| 1008:6B26 | `PanelTransition` | transition effects with WinG (below) |
| 1008:601A | – | the same transitions with plain GDI `BitBlt` (fallback) |
| 1018:A714 | – | palette fade step (`AnimatePalette`), used by transitions 15/16 |
| 1028:6CEA, 1028:615A, 1028:7280 | – | WAV streaming player: `mmio` reads into 10 rotating `waveOut` buffers |
| 1030:8D7C | – | script expression evaluator |
| 1038:3330 | – | text page-file parser (keyword dispatch) |
| 1050:8BDA | – | list control notification handler (13 cases) |
| 1010:42C4 | `PageFile_Open` | opens TWTW.PNG: header, string pool (`PageFile_ReadStringPool` 1010:46CA), directory block into a memory stream (parsed by 1020:5618), object table |
| 1020:1F28 / 200A / 20B4 / 2050 | `Stream_ReadByte` / `Stream_ReadU16` / `Stream_ReadU32b` / `Stream_ReadU32` | big-endian stream readers; the deserialisers reach them mostly through virtual calls |
| 1028:9C6C / 9CB0 / 9E72 | `File_Seek` / `File_HRead` / `File_HRead2` | file wrappers (the 0317:9CF3 / 9EB0 return addresses in the Wine trace) |
| 1028:407A | – | "play animation" handler; errors 0x412/0x413 |
| 1028:B29A | – | ANI player step: read the next chunk (1028:B81E), draw the frame (1048:A464 / A4DC), count frames; called once per tick |

## ANI playback findings

- The RLE decoder confirms the format in docs/formats/ani.md. A run writes `n` bytes as one byte (if `n` is odd) plus `n/2` 16-bit pairs. The end check (`dst == end`) only happens after each command, which explains frames that decode one byte past their size.
- **Transparency exists in the engine but is not visibly used by this title.** `RleDecodeKeyed` leaves pixels equal to a key colour index unwritten. When the frame blitter's transparent flag is set, it first copies the background into the frame bitmap, then decodes keyed. The key is read from the display object (field +0x70). Checked against 25 fps screen recordings of the original: an action-7 animation (`BATT0m0a` on the battery page) and the Workshop's auto-playing class-7 animations (`WWWW0MEA/FA/GA/JA`) match an **opaque** decode of their frames over the page bitmap pixel for pixel. The only differences were the mouse pointer. **The port can draw all ANI frames opaque.**
- Timing measured in the running original: content advances at the header's 5 fps. Under Wine the original showed only every second frame (a new picture every 0.4 s, two frames on), which is consistent with frames being skipped to stay in sync with the sound.

## Transitions

`PanelTransition(panel, duration?, type, …)` handles types 1–19. Type 0 means none:

| Type | Effect |
|---|---|
| 1 | 11-step blit (plain reveal) |
| 2–5 | wipe in 64-px strips: left, right, up, down |
| 6–9 | timed scroll/push in 4 directions (speed calibrated with `timeGetTime`, global at DS:0x11C) |
| 10, 11 | checkerboard dissolve (two pattern-brush passes) |
| 12 | box-in through black: 24 shrinking `FillRect` frames, then reveal |
| 13, 14 | slide with a duration parameter (16-px steps) |
| 15, 16 | fade out to white (15) or black (16) with `AnimatePalette` on palette entries 10–236, step 5, then fade back in |
| 19 | split from the centre outward, 10-px steps |

The type is a **property of the panel** (panel object +0x60, with a "pending" flag at +0x78). It probably comes from the template section's per-template fields (values 4, 8 and 9 occur there). `PanelRepaint` also skips transitions when a global check at 1050:EFAC is set.

**No transitions occur in practice.** In 25 fps recordings of the original (no `twtw.ini`, i.e. default settings), every change happened from one captured frame to the next: the title → Workshop switch, nav-bar page changes, the Index → page + zoom pop-up (page in one frame, pop-up in the next), and closing a pop-up. **The port can switch instantly.** Implementing the effects is optional polish; the table above documents them.

## Resource types

The data segment at 10B8:014F holds the engine's resource file-extension table, in order: `.wav .pag .png .ani .pan .pin .mic .sbm .pbk .txt .dib .trs .avi`, followed by the default cursor names `Arrow` and `Hand`. The text page writer's formatting strings (`{\r\n`, `\t}\r\n`, tabs) sit just before it. Only `.png` (the compiled page file), `.ani`, `.wav` and `.dib` occur on this CD. The rest (panels, sprite bitmaps `.sbm`, text, transition files `.trs`, AVI) belong to the authoring setup or other titles built on the engine. The index in this table is probably the resource-type code used by actions (to confirm).

## Open items for the next Phase 3 pass

Phase 3 is closed for the port's needs. Not traced (not needed): the action dispatcher code (reached through C++ virtual calls), the ANI tick source, the transparent-flag and key source, and the template field → transition mapping.

## Options screen functions

- **Kopiuj aktywne okno** (copy active window), 1028:76D2: captures the current view and puts it on the clipboard as a bitmap and a palette. On Windows 3.x it also adds a DIB (format 8). The port can use `SDL_SetClipboardData` with a BMP/PNG of the 640×480 frame.
- **Drukuj aktywne okno / Ustawienia wydruku** (print / print setup), 1028:800E and 1028:8A4A: the standard `PrintDlg`, then render to the printer with `PRINTABORTPROC` / `PRINTABORTDLG` (the "Drukowanie… n%" dialog). A modern port can replace this with "save image" or the platform's print dialog; it isn't a core feature.

## Behaviour notes from the runtime sessions

- A click on a page hotspot outside an open pop-up first closes the pop-up (in one frame), then runs the hotspot's action.
- The title screen ignores clicks for its first few seconds (probably until the intro animation ends); after that, a click goes to the Workshop.
- The Workshop's class-7 animations loop on their own while the hub is idle.
