# TWTW.PNG: compiled DKMM page file

Status: **the whole file decodes exactly**: header, pool, template section, index table and all 1668 object records, each consumed to the last byte. Only the two base-template headers are kept raw. Field names are inferred from the data and checked against it. The meanings of some constants are not confirmed yet.

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

Every child starts with `u16 class`. Most continue with the common **hotspot body**:

```
u16 sub_or_name  u16 id  u16 shape  u16 x0 y0 x1 y1  u16 cursor_ref  u16 1 0 0  events  u16 0
events = u16 count, count × { u16 event, u16 nactions, actions }
```

| class | body | meaning |
|---|---|---|
| 3, sub 1 | `0 0 0 0 file_ref` | **bitmap**: background picture of the object, an opaque rectangle |
| 3, sub 2 | hotspot + `0 4` | **hotspot** (≈ 9 500): `shape` 4 = rectangle (5 and 1 also occur); usually cursor `Hand` |
| 3, sub = name | hotspot + `0 4` | named hotspot (title screen, full-screen click area) |
| 4 | hotspot + `0 4 n` | A–Z dial region: `n` 0–23 = letter (select; plays the needle animation `azaz0m?a` from the dial table), 24/25 = rotate left/right arrows, 26 = OK (go to the dial table's target for the selected letter). Verified: letter 2 + OK → `AZ020M`. |
| 5, sub 2 | hotspot | inert region, no events; one at the end of many pages |
| 7, sub 2 | hotspot + `anim_ref 1 0 0 0 0 0 107` | **auto-play animation** element: plays `anim_ref` (e.g. `QEMG1m0a`, the Workshop hub animations) and fires event 6 when it ends (verified) |
| 2 | hotspot + `0 x0 y0 x1 y1 anim_ref cursor_ref 0 1 20 3 3 3 39 7 107` | **animation** element: an ANI file placed at the rectangle. Started by action 2 or by the object's open event. |
| 6 | hotspot + `anim_ref 1 0 0 0 0 0 107` | **navigation button**: grow animation `nav\navb0mNa` |
| 10 | fixed 29 words | **scroll bar**: rectangle, 8 bitmaps from the EXE (`@LINEUP`, `@THUMB`, … where `@` means EXE resource), target element id, step (1 px on help pages, 10 in the Index list) |
| 9 | fixed 15 words | **scrolled bitmap content** (help pages): view rectangle and the tall content bitmap (`Helpamt0`), scrolled by the class-10 bar that targets its id |
| 15, 16 | hotspot + `out_img in_img …` (8 / 5 words) | **bitmap buttons** using the EXE's `@OKOUT/@OKIN`, `@CANCLOUT/@CANCLIN` |
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

| type | payload (words) | count | meaning (provisional) |
|---|---|---:|---|
| 1 | `u32 object, u16` | 2095 | open object, **replacing** the current pop-up (verified: Options → "Historie z życia mamutów" closes `OPTI00` and opens `OPTI01`) |
| 2 | `u16 element_id, u16 107` | 99 | (re)start the page's animation element with this id (all class-2 elements use id 0x565) |
| 3 | `u32 object, u16 flag` | 6889 | open object: a type-1 target opens as a pop-up (zoom, glossary link, "Patrz też" entry), a type-2 target navigates to that page (verified: mammoth story item → page 990). The flag is 1 for glossary-to-glossary links. |
| 4 | `str, -1, -1, 1, 1, 0, 1` | 294 | **play sound**: the name is always a `…s` WAV (e.g. `BALO0m0s`) |
| 7 | `str anim, -1, -1, -1, 1, 0, 0, 1, 0, 0` | 440 | **play animation** in place; always an ANI, always the same parameters (e.g. `REFR0m0a` on the refrigerator page) |
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

Type 0 (template) trailer: `0 0`, then 9 button ids, then 9 u32 default targets. The targets are in **function order**, not paired with the id list: 984 Workshop, 985 principles hub (`QQQQ0m`), 253 A–Z, 776 inventors (`Iiii0m`), 986 timeline (`TTtt0m`), 987 Options, 988 mammoth stories, 812 Index, 676 Help. Entries 1–4 are the defaults that an article's links override. Verified by clicking each button: Warsztat → 984, Urządzenia → 253, Działy nauki → 985, Historia → 986, Wynalazcy → 776, Indeks → 812, Opcje → 987. Wróć (back) has no target. How the id list maps buttons to functions is still unexplained.

## Runtime verification (Wine, headless)

Checked against the running original with `tools/orig.sh` and pixel matching of the decoded bitmaps in screenshots. The app's client area starts at window offset (3, 29), below the Win3.1 caption.

| Prediction from the data | Observed |
|---|---|
| nav bar (`nav\nav`) at client (0, 0) | exact (100% pixels) |
| pages at page-area origin (94, 4): Workshop, A–Z, principles, timeline, inventors, `QEMG1m`/`2m`, `TT050m` | exact |
| pop-up at `(94, 4) + (x, y)` | **x exact, y + 8**: each pop-up is a small child window with an 8-pixel title strip (system-menu box). Its bitmap starts below the strip. Checked for Options (987), Index (812), `OPTI01` (988) and `BATT2z` (274). |
| index entry "Akumulator samochodowy - bateria" → page 104 + pop-up 274 | exact |
| action 1 replaces the pop-up, action 3 opens a pop-up or navigates, event 6 = animation end, links = context navigation | as described above |

Pop-ups therefore need an 8-px title strip in the port, or the bitmap must be drawn at `y + 8`. They also cast a 4-px drop shadow to the right and below: black on every other pixel of a checkerboard (black where x + y is even), with the image beneath showing through the rest. Wine paints the in-between pixels white, which real Windows doesn't (confirmed from memory of the original on Windows); the port follows Windows.

## Port notes

- Load the pool, the object table and the index table at startup, and parse records lazily, as the original does. The C parser follows `tools/dkmm.py` one-to-one.
- Rendering a page is: background bitmap at the page origin (94, 4), then any pop-ups (type 1): an 8-px title strip at `origin + (x, y)` and the bitmap below it. Hotspots are rectangles in the object's local coordinates. Animations play at their element rectangle.
- **Index screen (812)**, verified in the original: typing does an incremental, case-insensitive search. The list selects (yellow) the **first entry whose sort key is ≥ the typed text** and scrolls just enough to show it on the last visible row ("z" → "Zakład Tłoczniczy w Dover", "zeg" → "Zegar", "ż" → "Żagiel - łódź żaglowa", "qx" → "Radar"). The index table is already sorted under this key (0 inversions in 1320 entries): lower-case the text, order letters by the Polish alphabet `a ą b c ć d e ę f g h i j k l ł m n ń o ó p q r s ś t u v w x y z ź ż`, and put other characters (space, `-`, `,`, digits) before letters by code point. Enter = OK, which opens the entry's page and pop-ups. List rows are 12 px apart.
- **Media and clicks**, verified with a `waveOut` relay trace: while a sound (action 4) or an animation (action 7 / class 2) is playing, **any click (hotspot, other media, nav button) only stops it**, and the clicked action is not run. Stopping an animation rewinds it. When nothing is playing, a click starts its media, so a second click on the same hotspot toggles it off. A page change stops the sound. At most one sound or animation plays at a time. Pages pre-open their WAV and ANI files when loaded.
- **Back (Wróć)** is a page history stack (verified: `AZ020M` → A–Z → Workshop; at the bottom it stays put).
- **Cursors:** only `Hand` (9716 elements, EXE cursor resource 107, hotspot (6, 0)) and `normal` (156, the system arrow) occur; the Index text field uses an I-beam.
- Still open: the meaning of the constant action 4/7 parameters (-1 fields: probably position/loop defaults).
