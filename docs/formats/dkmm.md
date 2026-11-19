# TWTW.PNG: compiled DKMM page file

Status: **container solved; object grammar about 87% decoded.** `tools/dkmm.py` reads it:

```bash
python3 -I tools/dkmm.py summary work/cd/DKCODE/TWTW.PNG
```

```bash
python3 -I tools/dkmm.py object work/cd/DKCODE/TWTW.PNG 2 3
```

The EXE calls this file the *Scrunge File*. The authoring tool compiled ("scrunged") text page and template definitions into it. The text form is still parseable by the EXE's script parser, but isn't on the CD. All integers are **big-endian**.

## Layout

| Offset | Content |
|---|---|
| 0x00 | 6 × `str`: `DKMM` (product code), `11` (country code), `Windows` (platform), `PP`, `PO`, `Jak to działa?` (title). `str` = u16 length + CP1250 bytes. The product/country check messages are EXE strings 1250–1257. |
| 0x2b | u16 0, u16 1, char[2] `OF` |
| 0x31 | u32 `pool_size` (0xE1C1): byte size of the string pool, counted from 0x39 |
| 0x35 | u32 `objtab_off` (0x13532): offset of the object table |
| 0x39 | **string pool**: u16 count (3813), then count × `str` |
| 0xE1FA | **directory block**, 0x5338 bytes, up to `objtab_off`. Not decoded yet. Probably the page list, templates and global settings; it starts with `00 01 02 80 01 E0` (version 1, 640×480). |
| 0x13532 | **object table**: u32 `N` (1668), then N × u32 offsets, relative to `obj_base` = end of this table (0x14F46) |
| 0x14F46 | N object records, packed back to back to the end of the file. The offsets are unique, so each record's length is known exactly. |

At startup the original reads exactly this: seek to 0x39, `_hread` of `pool_size`, then the directory block, then the object table (verified with a Wine relay trace of `KERNEL._HREAD/_LLSEEK`). Object records are read later, on demand.

### String references

Records point into the pool with u16 indices that are **1-based**; 0 means no string. Pool strings are interned, so one entry such as `Hand` (cursor name) or `\ARTICLE1\AIRC\` (directory) is shared by many records.

### Object references

Actions refer to other objects by their **0-based index in the object table**. Checked: glossary entry `Ghea000g` (heat) links to object 471 (`Gtem000g`, temperature), `Gwei000g` (weight) to 614 (`Ggra000g`, gravity), and `Gden000g` (density) to 594 (`Gmas000g`, mass). The AIRC "Patrz też" panel links to objects 0 (REFR, refrigerator) and 1 (COOL, cooling).

## Object record

1664 of the 1668 records share this 18-word header. The other 4 have a different header and aren't decoded yet.

| Word | Meaning | Values |
|---|---|---|
| 0 | object type | 1 = pop-up panel (N/Z/X/J/P/R/G pages), 2 = full page (M), 3 = one record |
| 1 | ? | 0 (popups), 6 (pages), 2507 and a few others (looks like a template reference) |
| 2, 3 | x, y of the panel on screen (pages: 0, 0) | e.g. J panels at (151, 137), glossary at (121, 113) |
| 4 | directory string, e.g. `\ARTICLE1\AIRC\` | |
| 5 | 0 (4 records have a string here) | |
| 6–8 | 1, 1, 1 | |
| 9 | number of child elements | |
| 10, 11 | 1, 2 | |
| 12 | object id (a separate number space from strings and indices) | |
| 13–15 | 0, 0, 0 | |
| 16, 17 | width, height | **equal to the first bitmap's size in all 1445 decoded records** (540×450 article pages, 237×132 J panels, …) |

Then come the child elements, then a short trailer (see open questions).

### Child elements

Each child starts with a class word. Decoded so far:

- **class 3, sub 1: bitmap.** `0003 0001 0000 0000 0000 0000 file`, where `file` is the bitmap stem in the object's directory. This is how a page or pop-up gets its picture; the panel is a plain opaque rectangle of that bitmap at (x, y).
- **class 3, sub 2: hotspot.** `0003 0002 id shape x0 y0 x1 y1 cursor 1 0 0 nEvents events… 0 0 4`
  - `shape`: 4 = rectangle (most), 5 = also seen.
  - `cursor`: cursor name string (`Hand`, `normal`, …).
  - Each event is `type nActions action…`. Event type 4 is used for plain clicks, probably "OnClick"; type 6 also occurs.
- **class 5, sub 2**: a hotspot with no region and no events. It appears once at the end of many pages.
- **class 7, sub 2**: a hotspot with a 10-word tail holding an extra media string (e.g. `TT070m0a`) and the value 107 (= the EXE's `GROUP_CURSOR` resource 107).

Actions are a type word plus a fixed-length payload:

| Type | Payload words | Seen as | Meaning (provisional) |
|---|---:|---|---|
| 1 | 3 | `u32 object, u16` | open object (page) |
| 2 | 2 | `str, u16` | ? (str 0x565) |
| 3 | 3 | `u32 object, u16 flag` | open pop-up object (glossary links, "Patrz też" entries) |
| 4 | 7 | `str, -1, -1, 1, 1, 0, 1` | play media? |
| 7 | 10 | `str (animation, e.g. REFR0m0a), -1, -1, -1, 1, 0, 0, 1, 0, 0` | play animation |
| 12 | 1 | `u16` | ? (radio/list selection) |
| 13 | ? | | not decoded |

### Open questions (next step)

- Class 2 (no subtype word; buttons with text and colour fields), 4, 10 (scroll list: 9 item ids, the A–Z/index lists), 12 and 16 children: 223 records aren't decoded yet.
- The fixed tail words after hotspot events (`0 0 4` for class 3, `0 0` for class 5) and the object trailer (`0 0 0 0`, `0 0 0`, or a count followed by u32 object indices, e.g. the pop-ups owned by a page) don't fit one consistent framing yet. The parser accepts them but doesn't validate them.
- The directory block.
- Event and action numbering. The EXE also contains a script expression evaluator (`1030:8D7C`: typed values int/bool/string/rect/point/double with comparison and arithmetic operators). Some actions may carry expressions.

The binary reader in the EXE hasn't been located yet. Ghidra's decompiler handles this 16-bit far code poorly (call arguments are lost), so the work so far is data-driven, checked against the exact record lengths. Finishing it needs the reader found in Ghidra (Phase 3) or traced at runtime (Phase 4).
