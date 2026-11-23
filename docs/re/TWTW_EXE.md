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

## Status

Phase 3 is closed for the port's needs. Not traced (not needed): the action dispatcher code (reached through C++ virtual calls), the ANI tick source, the transparent-flag and key source, and the template field → transition mapping.

## Object classes and the widget code

The child factory FUN_1048_3dc2 maps class numbers to C++ constructors (the numbers are compared with globals filled from the enum table at 1048:5770, see `docs/formats/dkmm.md`). Constructors and vtables (`tools/ne_vtables.py` lists the vtables after applying the segment fixups):

| class | name | constructor | vtable |
|---|---|---|---|
| 2 | AnimationControl | 1048:d582 | 1048:dabc |
| 3 | Hotspot | 1010:69f8 | 1058:1570 |
| 9 | ScrollBitmap | 1048:4ffc | 1048:5f5e |
| 10 | ScrollBar | 1048:b434 | 1048:c99a |
| 12 | EditBox (`DKCEditBox`) | 1050:22c8 | 1050:3ebe |
| 13 | Collage (list box) | 1050:57be | 1050:69d8 |
| 14 | GroupColl (list controller) | 1050:7f38 | 1050:97bc (second table) |

**Scroll bar** (vtable 1048:c99a; the scrollable base class shared with the content is in 1010:03ba–0730):

- Load (FUN_1048_b20a): 8 bitmap names, then four step words (+0x14e, +0x150, line +0x152, acceleration +0x154), a flag (+0x156) and the 32-bit page (+0x78). All data has line 1 and no acceleration.
- Position: a 32-bit value clamped to [min, max] (SetPos, 1048:bec6 and 1010:04de). LineUp/LineDown move by `line`, PageUp/PageDown by `page`. The bar calls the same method on its target (the content or the list), which applies its own ±1 / ±page (1010:065e…06fc).
- Linking (1010:0560) copies the content's range into the bar. The Index list sets the bar at run time (1050:83c0): max = items − visible rows, page = max(1, trunc(visible rows × 0.9)) (constant 0.9 at DGROUP:0x0adc), so 12 for the 14-row list.
- Geometry (1048:b9c4, 1048:bec6): span = h − 47 (track between the 16-px arrows, minus the 16-px thumb, plus 1); thumb top = 16 + round(pos · span / range). Content offset = round(pos · (bitmap height − view height) / range). Both use FUN_1010_0732, `(2·pos·span / range + 1) >> 1`.
- Mouse down (FUN_1048_bfce) hit-tests by y: line up (above the track), page up (above the thumb), thumb (starts a drag), page down, line down. The track is two parts, above and below the thumb, each drawing THUMBTRACK from its own top, so the checkered pattern restarts under the thumb.
- Auto-repeat (FUN_1048_bb3a, a 100 ms timer through the timer manager FUN_1010_3a8c): the first repeat comes on the 3rd tick (300 ms), then one every tick, and only while the pointer is still over the pressed part.
- Drag (FUN_1048_bd74): pos = round((y − 16 − 8) · range / span), so the thumb is centred on the pointer; the target follows.
- Mouse up (FUN_1048_bc88): kills the timer and ends the drag.

Verified against the original's help screenshots: positions 0–3 and a track click from 3 (to 2) are pixel-identical apart from Wine's window frame.

## Events and actions

FUN_1048_4314 fills the enum globals from the name table: actions 1–14 at 1060:001e–0038, events 1–11 at 1060:003c–0050, object classes at 1060:000c and 0054–0074. Each action type is its own class whose constructor tags it with its enum value (e.g. OPENPAGE 1018:b2a8, CHANGEPAGE 1028:2624, PLAYWAVE 1020:e318, BACKPAGE 1050:6b17, TWTWOPTION 1050:011d, Closepage 1028:091c); slot 11 (+0x2c) executes it.

- Firing an event (FUN_1010_81d2) runs the sender's own actions for that event (FUN_1010_807a) and calls the listeners registered for the event type with the sender's id (FUN_1010_8c90 → FUN_1010_993e).
- Hotspot click: FUN_1010_6f0a (class 3) and FUN_1010_6420 (classes 6 and 7). The hotspot flags word (+0x1e) is tested with `flags & mask` (FUN_1048_0aa2): 4 = stop all media (FUN_1010_bd50 walks the playing-media list), 1 = act even after stopping media, 0x10 = rewind, 0x40 = stop at end, 0x20 = auto-play. Animation end: FUN_1010_66cc fires `on_finish` (event 6).
- OPENPAGE (FUN_1010_344a): no-op if the target is open; for single-instance templates (+0x96 of the template record) no-op if a window of that template is open; else a new window (FUN_1010_3640).
- CHANGEPAGE (FUN_1028_2a42): finds the open window whose template matches the target's and replaces its page in place (MoveWindow keeps it on screen); an error box if there is none. Both page actions look up a per-target kind (FUN_1008_7e8a) and, for kinds 0x11/0x12, play an animation or a sound named in a table at DGROUP:0x150 first (not seen in the data's use so far; not ported).

## Windows, buttons and the A–Z dial

- Windows (FUN_1048_8de6 → `CreateWindow`): the main window has style 0x02CA0000 (caption, system menu, minimise box, clip children); panels are `DKPenge` children (0x50000000); pop-up windows take their style from the window object (FUN_1028_cc1c → FUN_1030_661e), sized bitmap + template shadow. The framework window procedure (1048:cb24) handles create, destroy, paint, timer, mouse move/down/up and private messages; everything non-client goes to `DefWindowProc`. Pop-ups are plain `WS_POPUP` windows (the window object's default style 0x80000000, set at 1028:c84f; confirmed with Wine's `+win` trace), so they have no non-client area: **the title strip is painted by the program** — FUN_1028_d588 (a black-pen `Rectangle` filled with `GetSysColor(COLOR_ACTIVECAPTION)` for the active pop-up, `COLOR_INACTIVECAPTION` otherwise; slots +0xf0/+0xf4 = FUN_1058_6382/63a2) and FUN_1028_df40 (the system-menu box: `COLOR_WINDOWFRAME` frame, C0C0C0 face, black/white/808080 bar). Strip height from the template (+0x1c → +0x76); details in docs/formats/dkmm.md. (An earlier note said Windows drew it.)
- Pop-ups are owned by the window that was active when they opened (FUN_1058_0c8e, FUN_1008_1b64); closing one (Closepage, FUN_1028_0a84 → FUN_1048_63d6) re-activates its owner.
- Shadow: FUN_1028_dc3e. It shrinks the pop-up by the shadow width (FUN_1028_dedc, `MoveWindow` with repaint) and lets the windows below repaint (FUN_1028_de6a, `UpdateWindow`), then grows it back with `MoveWindow(..., FALSE)`, so the added strip keeps the picture underneath, and darkens it with `PatBlt` PATAND (0x00A000C9) and a checkered pattern brush. The light squares and the two 4×4 corners outside the bands show the picture beneath; Wine erases them with the class brush (white). Width from the template record.
- Main window size (FUN_1010_24f0, after the page file is open): width = the page file's screen width (640), height = `GetSystemMetrics(SM_CYCAPTION)` + screen height (480) − 20, centred on the screen with `MoveWindow`. With the window's style (caption, thin border, no sizing frame) and Windows 3.1 metrics that is a 640×480 window with a **638×459** client area, which the layout templates fill exactly. The port shows that area (under Wine, with a thicker frame, only 634×448 of it is visible).
- Window classes (FUN_1028_c67a): `DKPenge` (window procedure 1048:7186, arrow cursor, WHITE_BRUSH) for the main window, panels and pop-ups; `Video` (1048:8818, NULL_BRUSH).
- Bitmap buttons (`Button` 1058:0ab4, `CollButton` 1050:53ea): mouse down shows the `in` image (and with flag 4 stops media), moving shows it only while over the button, mouse up fires the click event if released over it.
- A–Z dial (`AZDial`, vtable 1048:e4e2; constructor at 1048:dcc8 clears selection, playing and the picture flag): open (FUN_1048_e1c8) plays a sound and starts a 1000 ms timer; each tick (FUN_1048_e302) draws EXE bitmap 172 or 173 at (334, 350) and flips the phase; a dial region click (FUN_1048_e5a2 → FUN_1048_dcfa) sets the selection and draws the dial with the needle on it (FUN_1048_dfc2: the letter's dial-table name + `.DIB` at page (161, 101), skipped once OK was pressed, +0x18a) or, for OK, kills the timer, draws EXE bitmap 352 at (320, 331) and plays the letter's needle animation.
- **Animation control** (class 2, `AnimationControl`, vtable 1048:dabc; base class in 1018:6eb2–7d2a): set-up FUN_1048_d5b4 loads EXE bitmaps 116/120/119 (play, unused, pause), 117/115 (rewind past / at the start), 118 (progress bar), 101 (panel) and creates three actions: PLAYNONQPICS (+0xa8, the animation), STOPPICS (+0xac) and SEEKPICS (+0xb0). Paint FUN_1018_76d6: panel, play button (pause image while playing), rewind button (bright once the position is past 0), progress bar `width × pos / frames`; each part only if the previous one was drawn, and the pressed images (+0x72/+0x7a/+0x92) are never loaded. Mouse down FUN_1018_7354 marks the button; mouse up FUN_1018_7454: Play pauses while playing, resumes when paused, does nothing at the end, else plays from the start unless FUN_1010_bd50 stopped another medium; Rewind stops and seeks to 0 when there is anything to rewind. End notification (1018:7c3x): stop, and seek to 0 if the record's auto-rewind word (+0xc0) is set. The message handler FUN_1048_d95a answers message `channel` (+0xdc, 107 in the data) by calling the mouse-up handler at the Play button, so SENDMESSAGE is "press Play". On open (FUN_1018_71ac) it plays if +0xbe (autoplay) is set; 0 in all data. Pixel values of these bitmaps are palette indices (118 has a stale palette; its index 11 shows as the game palette's 24,57,115 in the original).
- **PLAYPICS** (action 7): FUN_1048_4cda / base PLAYNONQPICS FUN_1010_9bdc; loader FUN_1010_9d44 (fields in docs/formats/dkmm.md); the media object comes from the media cache FUN_1010_bab6 (list at 1138:0004, which FUN_1010_bd50 also walks; media classes 1058:5c8c and 1058:5bce, factory FUN_1048_3b1c). Stopping a playing medium in FUN_1010_bd50 calls its reset (+0x14). In the original, a stopped or finished animation leaves its window repainted (verified: WIATR, the calculator control).

## Navigation, keyboard and cursors

- **Nav buttons.** When a button's grow animation ends (FUN_1048_e7f6), FUN_1048_ebfc looks up the button's id in the nav template's id list (+0x78…+0x88 of the panel object) and dispatches by its position: slots 0–4 take a target from the template (+0x8a… as u32) or, for slots 1–4, the current base page's article link (page +0x52…+0x5e, when it has links); slot 5 is Back (FUN_1048_ed80); slots 6 and 7 drop an open window of the target and "go to" it (FUN_1048_edb0, FUN_1048_ee1e); slot 8 closes Options and the mammoth stories and runs an OPENPAGE for Options (FUN_1048_ee8c). "Go to" (FUN_1010_53d2) runs a CHANGEPAGE for a base page and an OPENPAGE otherwise.
- **History.** The frame object (FUN_1048_97da) keeps a list at +0x56; its listener (1048:2746) reacts to CHANGEPAGE notifications and appends the base window's page unless it repeats the last entry. Back (FUN_1048_965c with 1) pops the current entry and "goes to" the previous one; with one entry it does nothing.
- **Keyboard.** The main loop (FUN_1048_6898) is a plain GetMessage/TranslateMessage/DispatchMessage pump, and the framework window procedure (1048:cb24, default handler in FUN_1048_8818) handles no key messages, so the original has no shortcuts. Keys reach two objects: the edit box (FUN_1050_397a: characters inserted over the selection, Backspace/Delete, Esc/Tab/Enter ignored; FUN_1050_2fc0: Left, Right, Home, End, Ctrl+Left/Right = Home/End, Shift extends the selection) and the Index list controller (FUN_1050_91ce: Enter = OK, Up/Down, Page Up/Down by the visible rows, Home/End).
- **Cursors.** FUN_1048_13b6 builds the named cursor table at 1048:2370 — `Normal` and `Arrow` (EXE cursor 157), `Hand` (EXE cursor 107), `IBeam`, `Watch`; FUN_1030_27ba matches an element's cursor name with `_fstricmp`. FUN_1048_8ac0 / 8cbe show and restore the hourglass during loading.

## Options screen functions

- **Kopiuj aktywne okno** (copy active window), 1028:76D2: captures the current view and puts it on the clipboard as a bitmap and a palette. On Windows 3.x it also adds a DIB (format 8). The port can use `SDL_SetClipboardData` with a BMP/PNG of the 640×480 frame.
- **Drukuj aktywne okno / Ustawienia wydruku** (print / print setup), 1028:800E and 1028:8A4A: the standard `PrintDlg`, then render to the printer with `PRINTABORTPROC` / `PRINTABORTDLG` (the "Drukowanie… n%" dialog). A modern port can replace this with "save image" or the platform's print dialog; it isn't a core feature.

## Behaviour notes from the runtime sessions

- A click outside the top pop-up closes it, and so on down the stack, until the click lands inside a pop-up or none is left; then it runs as usual (verified with a glossary pop-up over a zoom pop-up).
- A click on the title moves on through its full-screen flag-1 hotspot, not by "skipping" the animation (stopping never fires event 6); see docs/formats/dkmm.md for the click rules. (Earlier notes claimed the title ignored clicks, the Workshop looped idle animations, and a click skipped auto-play with event 6; all three were wrong.)
- There is no idle animation loop on the hub pages (verified with traces).
