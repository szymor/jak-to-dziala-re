# Resources and strings inside TWTW.EXE

Extract with:

```bash
python3 -I tools/ne_resources.py work/cd/DKCODE/TWTW.EXE work/res
```

The output goes to `work/res`: `.bmp`/`.cur`/`.ico` files, `STRING.tsv`, `DIALOG.txt` and `index.tsv`.

## What's there (85 resources)

| Type | Count | Notes for the port |
|---|---:|---|
| BITMAP | 41 | UI chrome: scroll bar parts (`LINEUP`, `LINEDOWN`, `THUMB`, `THUMBTRACK` and their `…ALT` versions), `OKOUT`/`OKIN`/`CANCLOUT`/`CANCLIN` buttons, numbered pieces 101–174, 1002–1012, 1951/1952, and one 52 KB bitmap (1908). The port loads them from the user's `TWTW.EXE` (`src/formats/ne_res.c`); none are shipped. |
| CURSOR / GROUP_CURSOR | 2 | 32×32 monochrome. Group 107 has its hotspot at (6, 0) (the hand), group 157 at (0, 0) (the game's arrow). The page data names cursors, not numbers; the EXE's cursor table maps `Hand` to 107 and `Normal`/`Arrow` to 157 (see `docs/re/TWTW_EXE.md`, *Cursors*). |
| ICON | 1 | App icon (group 1900) |
| PALETTE | 1 | Resource 1907, 1088 bytes: a LOGPALETTE (probably the identity palette the DIBs use) |
| DIALOG | 11 | Polish runtime dialogs: CD drive selection (`Wybierz napęd`), printing (`Drukowanie`), About (3011). Authoring/debug dialogs: `Object Properties`, `Object Event Procedure`, `Scrunging...`, `Go to page...`, `Resource Monitor`. |
| STRING | 25 tables, 213 strings | Error messages (Polish for users, English for authors), INI keys, property names |
| VERSION | 1 | |

## Insights from the string table

- **Engine model.** The EXE contains the authoring runtime: text page files are parsed (`Invalid action/event/keyword… in file '%s', line %d`) and compiled ("Scrunge", "Munge") into the binary page file `twtw.png`. Templates and panels are named (`Couldn't find panel definition '%s' in template definition '%s'`). Objects have properties: `Left`, `Top`, `Width`, `Height`, `Highlight`, `Disabled`, `AlwaysActive`, `Preload`, `LoadOnDemand`, `AutoPlay`, `AutoRewind`, `PlayInChild`, `FixedPosition`, `PageCloseIfClicked`, `Name`, `File`, `OnClick()`, `Transparent`, `PolyRegion`, `Value`, `Z Order`, `Visible`, `Cursor`, `Text`.
- **Polygon hotspots exist** (`PolyRegion`; `Invalid number of vertices specified for the polygon region`).
- **Files:** `twtw.png` (pages), `twtw.ini` (settings: `path`, `mmapp`, `Memory`, `Logging`, `Display`, `CanScrunge`, `CanMunge`, `CanEdit`, `UncompressedDibs`, `IncrementalScrunge`), `twtw.pdf` (page definitions in text mode, not shipped).
- **Bitmaps must be 8 bpp, compressed, with the right palette** (strings 1035, 1036, 1053), which confirms the phase 1 DIB findings.
- **Text flow:** `Authoring Error - Not enough space to flow text past picture` shows the engine can lay out text at runtime, but the shipped pages use pre-rendered text.
- **Tours:** `This control requires a name when recording a tour` and `Tour load error`: a recorded-tour feature, probably unused in this title.
