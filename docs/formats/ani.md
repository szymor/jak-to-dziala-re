# ANI ("funky!") animation format

Status: **solved**. `tools/ani.py` parses and decodes all 495 valid files on the CD (`info`, `export` to PNG frames and a WAV). The two `PRINCIPL/QLVR` animations aren't valid (see the last section).

All values are little-endian. There's no relation to the Windows `.ani` cursor format or to AVI.

## File header (0x1e bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u16 | version, always 1 |
| 0x02 | u16 | frame count `N` |
| 0x04 | u16 | 0 |
| 0x06 | u16 | frames per second (5, 4, 3, 2, 1) |
| 0x08 | u16 | audio lead in seconds `L` (2, 1 or 0) |
| 0x0a | u32 | size of the following format block, always 16 |
| 0x0e | PCMWAVEFORMAT | `tag=1 (PCM), channels=1, rate=22050, byte_rate=22050, align=1, bits=8` |

## Chunks

After the header comes a sequence of chunks to the end of the file. There are exactly `N + L·fps` chunks.

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u16 | chunk kind, always 1 |
| 0x02 | u32 | `A`: audio bytes in this chunk (`rate / fps`; 0 once the sound has ended) |
| 0x06 | u16 × 4 | update rectangle `x0, y0, x1, y1`; right and bottom are exclusive |
| 0x0e | u32 | `R`: decoded pixel bytes = `stride × (y1 − y0)`, with `stride = (x1 − x0)` rounded up to even |
| 0x12 | u32 | `C`: compressed video bytes; 0 means no picture change |
| 0x16 | char[6] | `funky!` |
| 0x1c | u8[A] | audio: unsigned 8-bit PCM, 0x80 = silence |
| | u8[C] | video: RLE-compressed 8-bit pixels |

### Audio/video interleave

The first `L·fps` chunks carry audio only. The sound is stored `L` seconds ahead of the pictures, so the player can buffer it while reading the CD. Chunk `k ≥ L·fps` carries video frame `k − L·fps`.

For playback, all audio chunks are one continuous PCM stream starting at t = 0. Video frame `j` is shown at `t = j / fps`, which is audio sample `j · rate / fps`. When a file has no sound, the audio bytes are silence (0x80), not missing.

### Video RLE

Byte-oriented:

```
b = next byte
if b & 0x80:  copy the next (b & 0x7f) bytes literally
else:         repeat the next byte b times
```

The decoder stops after `R` bytes. 10 files have frames that decode to `R + 1` bytes; the extra byte is dropped. Rows are stored **top-down**, with `stride` bytes per row; pad bytes beyond `x1 − x0` are discarded.

### Compositing

- Each frame is an **opaque rectangle pasted over the previous picture**. There's no XOR delta, and this title uses no transparent colour (the engine has a keyed mode, unused here; see `docs/re/TWTW_EXE.md`). A frame with `C = 0` leaves the picture unchanged.
- The pixel values are indices into the single global game palette (the same as every DIB, see [inventory](../inventory.md#dib-1704-files-solved)).
- Coordinates are relative to the animation's placement origin. For the main-page animations (`xxxx0MnA`) that is the page bitmap origin: `AIRP0M1A.ANI` composited over `AIRP0M.DIB` at (0, 0) lines up exactly. For the navigation bar animations (`NAV/NAVB0MnA`) it's the top-left of the 96×400 navigation column. Where each animation is placed comes from the DKMM data (see [dkmm.md](dkmm.md)).
- Frame 0 of article animations usually repaints a large area, and later frames repaint only the moving part. The last frame of a navigation button animation repaints the whole column, which restores the resting state.

## The two QLVR files

`PRINCIPL/QLVR/QLVR.ANI` and `QLVR0M0A.ANI` have a normal file header, but the 22 bytes after the first chunk's audio size are uninitialised memory, with no magic and nonsense rectangle and size fields. They are also not referenced by `TWTW.PNG`, so they are leftovers and the port ignores them.

## Port notes

- The decoder is about 30 lines of C. Each frame is one `SDL_UpdateTexture` of the dirty rectangle into an 8-bit surface (or an RGBA conversion through the palette) at the placement origin.
- Audio: feed the concatenated chunks to an `SDL_AudioStream` (U8, mono, 22050 Hz) and derive the displayed frame from the stream's playback position, so audio and video stay in sync.
- Files are at most 3.3 MB. Reading the whole file into memory is fine, and the port doesn't need the original's streaming.
