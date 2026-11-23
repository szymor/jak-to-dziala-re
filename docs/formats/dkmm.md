# TWTW.PNG: compiled DKMM page file

Status: **the whole file decodes exactly**: header, pool, template section, index table and all 1668 object records, each consumed to the last byte. Only the two base-template headers are kept raw. Field names come from the data and, for everything the port uses, from TWTW.EXE's loaders (`docs/re/TWTW_EXE.md`); a few constant fields stay unnamed.

```bash
python3 -I tools/dkmm.py summary work/cd/DKCODE/TWTW.PNG
```

```bash
python3 -I tools/dkmm.py object work/cd/DKCODE/TWTW.PNG 2 989
```

```bash
python3 -I tools/dkmm.py dump work/cd/DKCODE/TWTW.PNG work/dkmm/objects.txt
```

```bash
python3 -I tools/dkmm.py index work/cd/DKCODE/TWTW.PNG
```

The EXE calls this file the *Scrunge File*: the authoring tool compiled ("scrunged") text page and template definitions into it. All integers are **big-endian**.

## Layout

| Offset | Content |
|---|---|
| 0x00 | 6 × `str`: `DKMM` (product), `11` (country), `Windows`, `PP`, `PO`, `Jak to działa?`. `str` = u16 length + CP1250 bytes. |
| 0x2b | u16 0, u16 1, char[2] `OF` |
| 0x31 | u32 `pool_size` (0xE1C1), counted from 0x39 |
| 0x35 | u32 `objtab_off` (0x13532) |
| 0x39 | **string pool**: u16 count (3813), count × `str` |
| 0xE1FA | **directory block** up to `objtab_off`: the template section (0xE1FA–0xE5C0), then the index table (0xE5C0–0x13532). Both parse exactly. |
| 0x13532 | **object table**: u32 N (1668), N × u32 offsets relative to `obj_base` = end of the table (0x14F46) |
| 0x14F46 | N object records, packed back to back to the end of the file |

At startup the original reads the pool, the directory block and the object table, and reads records on demand (Wine relay trace).

**References.** String refs are u16, **1-based**, with 0 meaning none. Object refs are the **0-based index into the object table**, stored as u32 in actions and links. Separately, each object and element has a u16 *id*, a third number space used for named elements.

## Index table

`u16 n` (1320), then n entries:

```
u16 text_ref   u16 strlen(text)   u32 0   u16 page   u16 k   k × u32 popup
```

`page` is the object index of the article's main page, and the pop-ups are opened on top of it. Examples: `Akumulator samochodowy - bateria` → page 104 (`BATT0m`) with pop-up 274 (`BATT2z`). `Addytywne mieszanie barw` → page 303 (`QLIT0m`) with pop-ups 1186 and 1198. An entry with no pop-ups just opens the page. This drives the Index screen ("Indeks"), and it also maps subject codes to their Polish titles.

## Template section

```bash
python3 -I tools/dkmm.py templates work/cd/DKCODE/TWTW.PNG
```

The section is byte-packed: colours are 3-byte RGB, so u16 fields are not aligned. The parser walks it from 0xE1FA and must end exactly at the index table. Only the two base-template headers are kept raw.

```
u16 version (1)  u16 screen_w (640)  u16 screen_h (480)  u16 nBase (2)
nBase × base template:  u16 kind, u16 name, header (raw: 129 / 44 bytes), panel list, u16 0
derived templates (no count; the list ends where the style list begins), each:
    kind 1/3: u16 kind, u16 name, u16 1 6 1, 8×00, u16 a b, 12×00, u8 u8, u16 c d, 4×00,
              u16 n, n × u16 panel name, TAIL, 2×00
    kind 2:   u16 kind, u16 name, 38 bytes (u16 0 0 w h …), TAIL, 4×00
    TAIL = u16 0 1 1 1 0, rgb background, u16 w h x y
u16 nStyles, nStyles × { u16 name, u16 ref, 4 × rgb, u16 value (0xE1), 8×00, u16 len, face[len], u16 0 }
u16 0
panel list = u16 n, n × { u16 kind, u16 name, u16 has_obj, [u32 object], rgb, u16 w, h, x, y }
```

**Window layout** (base template 1; the rgb values look like the author's debug colours):

| Panel name | Object | Bitmap | Rect (x, y, w, h) |
|---|---|---|---|
| 0x0BE9 | 989 | navigation bar template | (0, 0, 94, 459) |
| 1 | – | **page area**: pages and pop-ups go here | (94, 4, 540, 450) |
| 0x0827 | 563 | `nav\frametop` | (94, 0, 544, 4) |
| 0x0825 | 562 | `nav\framesid` | (634, 4, 4, 450) |
| 0x0823 | 561 | `nav\framebot` | (94, 454, 544, 5) |

Base template 0x045D (A–Z) is the same, except for an extra page panel 0x045E at (94, 4, 540, 450). Its panel 1 is empty (0×0).

**Object placement.** Object header words 5–7 (`1, kind, panel`) name the panel the object is shown in: `1, 1, 1` (all objects but one) = panel 1, the page area. The A–Z page uses `1, 2, 0x045E`. Pop-up `x, y` are relative to that panel. Header word 1 is the object's template: 0x09CB (79 objects), 0x045D (A–Z), 0x0BF2 (3 title screens), and 6 for all other pages. Value 6 matches no template name; the default template is named 0, so 6 probably refers to it some other way (to confirm).

**Derived templates:** 0 and 0x0BE1/0x09CB = panel 1 + nav bar + frames (normal pages); 0x0BFA/0x0BFB = panel 1 + frames, no nav bar (full-screen views); 0x0BF2 (kind 2) = a 638×459 screen with no panels (title screens). Background colour cream `FFFBF0`.

**Text styles** (used by the Index screen's edit field and list): 0x0BFC and 0x0BE3, both `Times New Roman CE`, with foreground/background colour sets (white/black/navy and white/black/yellow) and value 0xE1. That value is probably a size or weight, but it isn't confirmed.

The first base header (129 bytes) also contains a named list `0x0BF4` with 5 entries (names 0x0BF5–0x0BF9 = values 1–5). It's probably a global variable with named states, but that's not confirmed.

## Object record

```
u16 type
type 0 (template / frame):  u16 name_id
type 1, 2, 3:               u16 template, u16 x, u16 y, u16 dir_ref, u16 w5, u16 1, u16 1|2, u16 v
core:                       u16 nchildren, u16 1, u16 2, u16 id, u16 0, u16 0, u16 0, u16 width, u16 height
children…                   (nchildren slots)
trailer                     (type 0 has its own, see below)
```

| type | count | meaning |
|---|---:|---|
| 0 | 4 | templates: the three frame pieces and the 640×480 navigation bar (989) |
| 1 | 1263 | pop-up panels (N, Z, X, J, P, R, G, the Options box). `x, y` = panel position. |
| 2 | 399 | full pages (article/principle/inventor/timeline/A–Z main pages), 540×450 at (0, 0) of the page area |
| 3 | 1 | the Index screen (812) |

`width, height` equal the size of the object's background bitmap in **all 1663 records** that have one (checked against the DIB headers). `dir_ref` is the directory, e.g. `\ARTICLE1\AIRC\`. File names in children are relative to it, unless they carry their own relative path (`nav\…`).

### Children

TWTW.EXE names the class numbers (an enum table at 1048:5770): 1 `Bitmap`, 2 `AnimationControl`, 3 `Hotspot`, 4 `AZHotspot`, 5 `WordBox`, 6 `NavHotspot`, 7 `AnimHotspot`, 8 `CancelButton`, 9 `ScrollBitmap`, 10 `ScrollBar` (spelled `ScrollBaa`), 11 `AZAnimHotspot`, 12 `EditBox`, 13 `Collage`, 14 `GroupColl`, 15 `CollButton`, 16 `Button`, 17 `ClipRect`, 18 `SpriteObject`. The same table names the actions (0 `NULL_ACT`, 1 `OPENPAGE`, 2 `SENDMESSAGE`, 3 `CHANGEPAGE`, 4 `PLAYWAVE`, 5 `STOPWAVE`, 6 `SEEKWAVE`, 7 `PLAYPICS`, 8 `PLAYPICSNOLOAD`, 9 `BACKPAGE`, 10 `PLAYNONQPICS`, 11 `STOPPICS`, 12 `TWTWOPTION`, 13 `Closepage`, 14 `SEEKPICS`), the events (1 `on_open`, 2 `on_activateapp`, 3 `on_close`, 4 `on_click`, 5 `on_preopen`, 6 `on_finish`, 7–11 `on_select1..5`), the panels (`Display`, `AZDial`, `NavPanel`), the page kinds (`POPUP_PAGE`, `BASE_PAGE`, `INDEX_PAGE`, `CHILD_PAGE`) and the transitions (`none`, `zoom`, `wipeleft`, `wiperight`).

Every child starts with `u16 class`. Most continue with the common **hotspot body**:

```
u16 sub_or_name  u16 id  u16 flags  u16 x0 y0 x1 y1  u16 cursor_ref  u16 1 0 0  events  u16 0
events = u16 count, count × { u16 event, u16 nactions, actions }
```

| class | body | meaning |
|---|---|---|
| 3, sub 1 | `0 0 0 0 file_ref` | **bitmap**: background picture of the object, an opaque rectangle |
| 3, sub 2 | hotspot + `0 4` | **hotspot** (≈ 9 500): `flags` 4 almost everywhere, 5 on the film pages, 1 on the title and credits (see *Hotspot flags*); usually cursor `Hand` |
| 3, sub = name | hotspot + `0 4` | named hotspot (title screen, full-screen click area) |
| 4 | hotspot + `0 4 n` | A–Z dial region: `n` 0–23 = letter, 24/25 = left/right arrows (selection −1/+1, wrapping), 26 = OK. Handled by the EXE's `AZDial` panel (code in `docs/re/TWTW_EXE.md`). Opening the page plays `\AZAZ\DIAL.WAV`. The panel is rebuilt on every visit, so the selection starts at A (verified). Letters set the selection, the arrows step it with wrap-around; the dial then shows its needle on the selected letter: the letter's dial-table animation name with `.DIB` (`AZAZ0MAA.DIB` … `AZAZ0MXA.DIB`, 221×231) at page (161, 101); A's picture is the page's own (verified pixel-exact). The **OK lamp blinks**: every second it alternates EXE bitmap 172 (yellow) and 173 (red) at page (334, 350), starting yellow after 1 s (verified). OK stops the lamp, draws EXE bitmap 352 (the pressed button) at (320, 331), plays the selected letter's `azaz0m?a.ani` from the dial table, then opens the table's target (verified: B → right → right → left → OK opens `AZ020M`). |
| 5, sub 2 | hotspot | inert region, no events; one at the end of many pages |
| 7, sub 2 | hotspot + `anim_ref 1 0 0 0 0 0 107` | **animation element** (`AnimHotspot`, vtable 1058:141e): plays `anim_ref` and fires event 6 when it ends (FUN_1010_66cc). `flags` 36 (0x20 auto-play + 4) = plays when the page opens; 20 (0x10 rewind + 4) and 4 = plays when clicked, then event 6 (Workshop objects, principles hub → principle page, timeline → period page; all verified); 4 is pre-loaded when the page opens. Flag 0x10 rewinds the animation when it ends or its click stops it; all flag-4 and flag-36 elements navigate on event 6, so only the flag-20 ones without events (e.g. the calculator keys) show the rewind |
| 2 | hotspot + `flags x0 y0 x1 y1 anim_ref cursor_ref autoplay autorewind px py rx ry bx by channel` | **animation control** (`AnimationControl`; code in `docs/re/TWTW_EXE.md`): a 74×22 VCR panel at the rectangle, EXE bitmap 101, with a rewind button at (3, 3) (115 at the start, 117 past it), a play/pause button at (20, 3) (116 / 119) and a progress bar at (39, 7) (118, shown over `pos/frames` of its 30 px). The animation itself is page-relative (full-page frames). Clicking Play starts it, pauses, resumes; at the end it stops and rewinds if `autorewind` (1 on most pages, 0 on the principle film pages, which keep the last frame), and Play does nothing at the end until Rewind. Rewind stops and restores the page. `autoplay` (0 in all data) would play on open. `channel` (107) is the message id the control answers: action 2 with it = Play. Verified in the original on the calculator page. |
| 6 | hotspot + `anim_ref 1 0 0 0 0 0 107` | **navigation button**: grow animation `nav\navb0mNa` |
| 10 | fixed 29 words | **scroll bar** (`ScrollBar`): `id, x0 y0 x1 y1, cursor, ?, target_id, u32 min, u32 max, ?, ?, 8 × bitmap ref, 4 × step word, flag, u32 page` (32-bit values big-endian word pairs). Bitmaps from the EXE (`@` = EXE resource): LINEUP, LINEDOWN, THUMB, THUMBTRACK, then the pressed ALT set. Help bars: max 0 (taken from the content), page 1; Index bar: max 1308, page 10 (the list overrides both at run time). See `docs/re/TWTW_EXE.md`. |
| 9 | fixed 15 words | **scrolled bitmap content** (`ScrollBitmap`, help pages): `id, view x0 y0 x1 y1, …, u32 min, u32 max, …, file_ref`; `max` is the number of scroll positions (3 for `Helpamt0`, up to 107); position `p` shows the bitmap at offset `round(p · (height − view_h) / max)` |
| 15, 16 | hotspot + `out_img in_img …` (8 / 5 words) | **bitmap buttons**: drawn from the EXE bitmaps they name (`@OKOUT/@OKIN`, `@CANCLOUT/@CANCLIN`) at the hotspot rectangle, over the pop-up bitmap (verified pixel-exact). Push buttons (FUN_1010_204c/2210/210c): the `in` image while held with the pointer on it; the click event fires on release over the button |
| 12 | hotspot + `0 0 0 style` | **text edit field** (Index search box, `IBeam` cursor, text style 0x0BE3) |
| 13 | hotspot + `rgb rgb u16 pstr u8 pstr style` | **list box** (Index list): white/black colours, data source `SimpleLBData` (= the index table), face `Regular`, text style 0x0BE3 |
| 14 | fixed 22 words | **list controller**: links the list box, edit field, scroll bar and OK button (Index screen) |

### Events

| event | count | meaning |
|---|---:|---|
| 1 | 22 | on object open (starts the page's animation element) |
| 4 | 9506 | click |
| 6 | 191 | **animation finished** (class-7 elements). Verified: `QEMG1m` plays its film animation, then event 6 → action 3 → page 1120 (`QEMG2m`, "KONIEC"). |

### Actions

| type | payload (words) | count | meaning |
|---|---|---:|---|
| 1 | `u32 object, u16` | 2095 | `OPENPAGE`: open the object in a **new window** (FUN_1018_b578 → FUN_1010_344a): nothing happens if it is already open, or if its template is single-instance and a window of that template is open. Used page → pop-up and pop-up → pop-up; the new window is **stacked** over the old one, owned by it (verified: a glossary pop-up opened from the battery zoom pop-up leaves the zoom pop-up visible around it; `OPTI01` merely covers `OPTI00`) |
| 2 | `u16 element_id, u16 107` | 99 | `SENDMESSAGE`: message 107 to the class-2 control with this id (all use 0x565 = 1381, always in the same object): the control acts as if its Play button was clicked (FUN_1048_d95a → mouse-up FUN_1018_7454 at the button). Used by picture hotspots (a click toggles play/pause) and by the principle film pages' open event (autoplay) |
| 3 | `u32 object, u16 flag` | 6889 | `CHANGEPAGE` (FUN_1028_2a42): show the object **in the open window of its template**: a type-2 target replaces the page in the base window (verified: mammoth story item → page 990); a type-1 target replaces the pop-up in place, keeping the window's position (pop-up → pop-up links always stay within one template; verified: glossary `Genr000g` → "Patrz też" → `Gcon000g` replaces it, and the help pages). The flag is 1 for glossary-to-glossary links. |
| 4 | `str, -1, -1, 1, 1, 0, 1` | 294 | **play sound**: the name is always a `…s` WAV (e.g. `BALO0m0s`). Sound only: no picture changes (verified: the Workshop's hammer `WWWW0m1s` and saw `WWWW0m2s` leave every pixel of the original unchanged while they play; the trumpet and the spanner work the same way) |
| 7 | `str anim, -1, -1, -1, 1, 0, 0, 1, 0, 0` | 440 | `PLAYPICS`: **play animation** in place; always an ANI, always the same parameters (e.g. `REFR0m0a` on the refrigerator page). The action object (FUN_1048_4cda, base FUN_1010_9bdc; loader FUN_1010_9d44) reads the name and then +0x56, +0x58, +0x5c (−1), +0x5a (1; 0 or −1 is made 1), +0x5e, +0x62 (0), +0x60 (1, also the constructor default) and an empty list. All 440 use the same values; their individual meanings (start/end frame, count) don't affect the port. When it ends the window is repainted without it (see *Port notes*) |
| 12 | `u16 n` | 3 | select option `n` (Options screen radio buttons) |
| 13 | `u32 object` | 1 | close that object (the Options pop-up's Anuluj button names the pop-up itself) |

All 8985 object references in actions fall inside the table.

### Trailer (types 1–3)

```
events (object-level, e.g. event 1 = on open)
u16 0
u16 nTable [nTable × u32 target, nTable × anim_ref, u16 extra]   -- only the A–Z page (253): 24 letters → page + letter animation
u16 nLinks, nLinks × 4 × u32 object                              -- all 149 machine articles: (principles, A-Z, inventor, timeline)
```

The 4 links are the article's targets for the **context-sensitive navigation buttons** Działy nauki, Urządzenia, Wynalazcy and Historia. Verified: on the battery page (104), Historia opens `TT050m` (link 4), not the default timeline page.

Type 0 (template) trailer: `0 0`, then 9 button ids, then 9 u32 default targets. The targets are in **function order**, not paired with the id list: 984 Workshop, 985 principles hub (`QQQQ0m`), 253 A–Z, 776 inventors (`Iiii0m`), 986 timeline (`TTtt0m`), 987 Options, 988 mammoth stories, 812 Index, 676 Help. Entries 1–4 are the defaults that an article's links override. Verified by clicking each button: Warsztat → 984, Urządzenia → 253, Działy nauki → 985, Historia → 986, Wynalazcy → 776, Indeks → 812, Opcje → 987. Wróć (back) has no target. The mapping is fixed in the code (FUN_1048_ebfc): a clicked button's id is looked up in the id list, and its **position** there selects the handler — slot 0 targets[0]; slot 1 article link 1 or targets[2]; slot 2 link 0 or targets[1]; slot 3 link 3 or targets[4]; slot 4 link 2 or targets[3]; slot 5 Back; slot 6 targets[7]; slot 7 targets[8]; slot 8 targets[5] (after closing targets[5] and [6] if open). With this data the slots are Warsztat, Urządzenia, Działy nauki, Historia, Wynalazcy, Wróć, Indeks, Pomoc, Opcje. See `docs/re/TWTW_EXE.md`.

## Runtime verification (Wine, headless)

Checked against the running original with `tools/orig.sh` and pixel matching of the decoded bitmaps in screenshots. The app's client area starts at window offset (3, 29), below the Win3.1 caption.

| Prediction from the data | Observed |
|---|---|
| nav bar (`nav\nav`) at client (0, 0) | exact (100% pixels) |
| pages at page-area origin (94, 4): Workshop, A–Z, principles, timeline, inventors, `QEMG1m`/`2m`, `TT050m` | exact |
| pop-up at `(94, 4) + (x, y)` | **x exact, y + 8**: each pop-up is a window with an 8-pixel title strip (system-menu box) painted by the program. Its bitmap starts below the strip. Checked for Options (987), Index (812), `OPTI01` (988) and `BATT2z` (274). |
| index entry "Akumulator samochodowy - bateria" → page 104 + pop-up 274 | exact |
| action 1 opens a window, action 3 changes the page in place, event 6 = animation end, links = context navigation | as described above |

Pop-ups therefore need an 8-px title strip in the port, or the bitmap must be drawn at `y + 8`. A black line is drawn under the strip, over the bitmap's first row. They also cast a 4-px drop shadow to the right and below: black on every other pixel of a checkerboard (black where x + y is even), with the image beneath showing through the rest. The two 4×4 corners outside the shadow bands also show the picture beneath. Wine paints these pixels white; the code shows real Windows keeps the picture (`docs/re/TWTW_EXE.md`, *Shadow*). The shadow width comes from the template record (4 for both pop-up templates in use).

**The title strip is painted by TWTW.EXE.** The pop-up is a plain `WS_POPUP` window (0x80000000, the window object's default style, 1028:c84f; Wine's `+win` trace shows style 80000000 and a client rectangle equal to the window rectangle), so it has no non-client area. The template's strip height (+0x1c → window object +0x76, 9) reserves the rows, and FUN_1028_d588 paints them:
- a `Rectangle` with the black pen and a solid brush of `GetSysColor(COLOR_ACTIVECAPTION)` for the active pop-up or `COLOR_INACTIVECAPTION` for the others (window slots +0xf0 / +0xf4), from x = 11 (`max(height, 12) − 1`) to the pop-up's width and over rows 0–8; row 8 lies over the bitmap's first row;
- the system-menu box over it (FUN_1028_df40, `ExtTextOut(ETO_OPAQUE)` fills): `COLOR_WINDOWFRAME` lines on rows 0 and 8 and columns 0 and 12, a face of `GetNearestColor(C0C0C0)` over (1, 1)–(12, 8), and the bar — 808080 at (4, 4)–(11, 7), black at (3, 3)–(10, 6), white at (4, 4)–(9, 5).

The port uses Windows 3.1's default colours (active caption navy, inactive caption white, window frame black). Under Wine the shape matches pixel for pixel; only these system colours differ (Wine: light blue / grey).

## Port notes

- Load the pool, the object table and the index table at startup, and parse records lazily, as the original does. The C parser follows `tools/dkmm.py` one-to-one.
- Rendering a page is: background bitmap at the page origin (94, 4), then any pop-ups (type 1): an 8-px title strip at `origin + (x, y)` and the bitmap below it. Hotspots are rectangles in the object's local coordinates. Animations play at their element rectangle.
- **Index screen (812)**, verified in the original: typing does an incremental, case-insensitive search. The list selects (yellow) the **first entry whose sort key is ≥ the typed text** and scrolls just enough to show it (on the last visible row when it moved down); an empty field selects the first entry (verified). The controller searches on every edit (FUN_1050_8bda case 1 → FUN_1050_1c42 → the data source's find callback) ("z" → "Zakład Tłoczniczy w Dover", "zeg" → "Zegar", "ż" → "Żagiel - łódź żaglowa", "qx" → "Radar"). The index table is already sorted under this key (0 inversions in 1320 entries): lower-case the text, order letters by the Polish alphabet `a ą b c ć d e ę f g h i j k l ł m n ń o ó p q r s ś t u v w x y z ź ż`, and put other characters (space, `-`, `,`, digits) before letters by code point. Enter = OK, which opens the entry's page and pop-ups. List rows are 12 px apart.
- **Media and clicks**, verified with `waveOut` relay traces in the original:
  - **Hotspot flags** (the hotspot word at +0x1e in the EXE; FUN_1010_6f0a for hotspots, FUN_1010_6420 for animation elements and nav buttons): every hotspot of the clicked window gets the click before the hit test. One with **flag 4** stops all playing media (FUN_1010_bd50). If that stopped anything, the hotspot under the pointer still runs its action only with **flag 1**; otherwise the click is swallowed. Flag 0x10 rewinds the element's animation, 0x40 stops it when it ends, 0x20 plays it when the page opens.
  - So while a sound or a clicked animation plays, a click on an ordinary (flag 4) hotspot only stops it (verified with traces). Stopping never fires event 6.
  - **Auto-play "skip"** is done by the data, not by the engine: the film pages (990–1011) cover the screen with flag-5 hotspots whose click repeats the animation's finish target (film area) or returns to the principle page (frame); the title and credits (1586, 1587) have one full-screen flag-1 hotspot with the finish target and no flag-4 hotspot, so the click goes through and the page change stops the animation. Verified in the original: a click on the frame of `QEMG1m` goes to `QEMG0m` (page 101), not to `QEMG2m`.
  - When nothing is playing, a click starts its media, so a second click on the same hotspot toggles it off. A page change stops the sound. At most one sound or animation plays at a time.
  - **There is no idle animation loop.** The Workshop and the principles hub played nothing in 45 s / 26 s untouched (no ANI file opened). Pages pre-open some of their WAV and ANI files when loaded.
- **Clicks outside the top pop-up** close it, and so on down the stack, until the click lands inside a pop-up or none is left; then the click runs as usual (verified with a glossary pop-up over a zoom pop-up).
- **Back (Wróć)** goes to the previous base page. The history holds base pages only: the frame records the base page after every CHANGEPAGE, skipping repeats; pop-ups never enter it (code; verified: `AZ020M` → A–Z → Workshop; at the bottom it stays put).
- **Nav buttons → functions:** by the id's slot in the template trailer, see *Trailer* above.
- **Cursors:** an element's cursor name is matched case-insensitively against the EXE's table `Normal`, `Arrow` (both EXE cursor 157, the game's own arrow), `Hand` (EXE cursor 107, hotspot (6, 0)), `IBeam`, `Watch` (system cursors). The data uses `Hand` (9716 elements) and `normal` (156).
- **Animations end by repainting their window** (verified for action 7, the class-2 control's rewind, and a click that stops an animation): what an animation drew is gone after it ends or is stopped, unless a control without auto-rewind keeps its last frame. The port also keeps a playing or paused animation across its own repaints by re-applying the frames shown so far.
