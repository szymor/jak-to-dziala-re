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

## ANI playback findings

- The RLE decoder confirms the format in docs/formats/ani.md. A run writes `n` bytes as one byte (if `n` is odd) plus `n/2` 16-bit pairs. The end check (`dst == end`) only happens after each command, which explains frames that decode one byte past their size.
- **Transparency exists in the engine.** `RleDecodeKeyed` leaves pixels equal to a key colour index unwritten. When the frame blitter's transparent flag is set, it first copies the background into the frame bitmap, then decodes keyed. The key is read from the display object (field +0x70). Which elements set the flag, and which index is the key, is still open: indices 0 and 255 are both used as ordinary black and white in the art.
- Measured in the running original (25 fps screen capture): animations advance every 0.2 s, i.e. 5 fps as the header says.

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

The type is a **property of the panel** (panel object +0x60, with a "pending" flag at +0x78). It probably comes from the template section's per-template fields (values 4, 8 and 9 occur there). In the running original, a page change from the nav bar happened within one captured frame, i.e. without a visible transition. Which transitions are used where is still open.

## Open items for the next Phase 3 pass

1. The action dispatcher (semantics of actions 4, 7, 13 and the A–Z dial class 4). It hasn't been found yet; the 13/14-case switches checked so far were the list control and the GDI transitions.
2. Back/history (Wróć), and the cursor mapping (`Hand`, `normal`, `IBeam`, `zoom`, `none` → resources 107/157 or system cursors).
3. The transparent-flag source and key index for animations.
4. The template field → transition type mapping.
5. Options: copy window (clipboard), print (`PrintDlg`, `PRINTABORTDLG`).
