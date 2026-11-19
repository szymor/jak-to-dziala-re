# TWTW.PNG: compiled DKMM page file

Status: **1667 of 1668 object records and the index table decode exactly**: every byte of each record is consumed. The template section at the start of the directory block (about 1 KB) and one singleton object (the Index screen's list box) are still open. Field names are inferred from the data and checked against it. The meanings of some constants are not confirmed yet.

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
| 0xE1FA | **directory block** up to `objtab_off`: the template section (0xE1FA–0xE5C0), then the index table (0xE5C0–0x13532) |
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

## Template section (not decoded yet)

The template section is byte-packed, not word-aligned. It starts `0001 0280 01E0` (version, 640×480 window) and defines the page templates that object headers refer to (header word 1: 6, 0x09CB, 0x0BF2, …). Each template lists panels. A panel record looks like `0001, template-name, 0001, 0000, object index, 3 bytes, w, h, x, y`. The window frame pieces decoded so far:

| Panel object | Bitmap | Size | Position |
|---|---|---|---|
| 563 | `nav\frametop` | 544×4 | (94, 0) |
| 562 | `nav\framesid` | 4×450 | (634, 4) |
| 561 | `nav\framebot` | 544×5 | (94, 454) |

So **the 540×450 page area sits at (94, 4)**, with the navigation template (object 989, 640×480) behind it. The section also holds text styles (`Times New Roman CE`, with colours) used by runtime text.

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
| 4 | hotspot + `0 4 n` | A–Z dial letter region; `n` is the letter number |
| 5, sub 2 | hotspot | inert region, no events; one at the end of many pages |
| 7, sub 2 | hotspot + `media_ref 1 0 0 0 0 0 107` | timeline hotspot with an extra animation (e.g. `TT070m0a`) and cursor resource 107 |
| 2 | hotspot + `0 x0 y0 x1 y1 anim_ref cursor_ref 0 1 20 3 3 3 39 7 107` | **animation** element: an ANI file placed at the rectangle. Started by action 2 or by the object's open event. |
| 6 | hotspot + `anim_ref 1 0 0 0 0 0 107` | **navigation button**: grow animation `nav\navb0mNa` |
| 10 | fixed 45 words | **scrolling bitmap view** (help pages): scroll bar rectangle, 8 scroll bar bitmaps from the EXE (`@LINEUP`, `@THUMB`, … where `@` means EXE resource), view rectangle, content bitmap (`Helpamt0`). It takes **2 child slots** (its content counts as a child). |
| 15, 16 | hotspot + `out_img in_img …` (8 / 5 words) | **bitmap buttons** using the EXE's `@OKOUT/@OKIN`, `@CANCLOUT/@CANCLIN` |
| 12, 13 | | text entry (`IBeam` cursor) and list box (inline Pascal strings `SimpleLBData`, `Regular`) in the Index screen only, not decoded |

### Events

| event | count | meaning |
|---|---:|---|
| 1 | 22 | on object open (starts the page's animation element) |
| 4 | 9506 | click |
| 6 | 191 | timeline hotspots: probably mouse-over or a second click mode (to confirm at runtime) |

### Actions

| type | payload (words) | count | meaning (provisional) |
|---|---|---:|---|
| 1 | `u32 object, u16` | 2095 | go to page / open object (callout notes `N`, pages) |
| 2 | `u16 element_id, u16 107` | 99 | start the animation element with this id |
| 3 | `u32 object, u16 flag` | 6889 | open pop-up (zoom, glossary link, "Patrz też" entry); the flag is 1 for glossary-to-glossary links |
| 4 | `str, -1, -1, 1, 1, 0, 1` | 294 | play media (sound or animation) |
| 7 | `str anim, -1, -1, -1, 1, 0, 0, 1, 0, 0` | 440 | play animation in place (e.g. `REFR0m0a` on the refrigerator page) |
| 12 | `u16 n` | 3 | select option `n` (Options screen radio buttons) |
| 13 | `u32 object` | 1 | cancel / close back to object |

All 8985 object references in actions fall inside the table.

### Trailer (types 1–3)

```
events (object-level, e.g. event 1 = on open)
u16 0
u16 nTable [nTable × u32 target, nTable × anim_ref, u16 extra]   -- only the A–Z page (253): 24 letters → page + letter animation
u16 nLinks, nLinks × 4 × u32 object                              -- 85 pages: 4 object links, probably navigation targets (to confirm)
```

Type 0 (template) trailer: `0 0`, then for each class-6 button its id, then each button's u32 target object. The navigation bar maps its 9 buttons to objects 984, 985, 253 (A–Z), 776, 986, 987 (Options), 988, 812 (Index), ….

## Port notes

- Load the pool, the object table and the index table at startup, and parse records lazily, as the original does. The C parser follows `tools/dkmm.py` one-to-one.
- Rendering a page is: background bitmap at the page origin (94, 4), then any pop-ups (type 1) at `origin + (x, y)` with their bitmap. Hotspots are rectangles in the object's local coordinates. Animations play at their element rectangle.
- Still to confirm at runtime (Phase 4): the exact screen origin of pop-ups, event 6, the action 4/7 parameters, and the meaning of the 4 links.
