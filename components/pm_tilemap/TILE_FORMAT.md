# Pisces Moon OS — Map Tile Pack Format

`pm_tilemap` renders offline slippy-map tiles read straight off the SD card.
To keep SD access under the app's SPI Bus Treaty mutex (and to avoid depending
on an LVGL PNG decoder / filesystem driver being enabled), tiles are stored as
**raw RGB565**, not PNG.

## On-card layout

```
/sd/tiles/{z}/{x}/{y}.bin
```

- `{z}` — zoom level (the renderer clamps to 2..18; 16 is the wardrive default)
- `{x}` / `{y}` — standard OSM/XYZ "slippy map" tile indices (Web Mercator)

This is the same directory shape every standard tiler emits — only the file
extension and encoding differ.

## File format

Each `.bin` is exactly **131072 bytes**: a 256×256 tile, 2 bytes per pixel,
**little-endian RGB565**, row-major (top row first, left to right).

```
256 * 256 * 2 = 131072 bytes
```

No header, no palette, no compression. A short or missing file is treated as a
blank tile and rendered as empty space (this is the normal "no tile pack yet"
state — the map area is just dark and the GPS overlay carries the info).

### Byte order

This board builds LVGL with `CONFIG_LV_COLOR_DEPTH_16=y` and
`CONFIG_LV_COLOR_16_SWAP=n`, so pixels are native little-endian RGB565 with
**no byte swap**. The descriptor the renderer hands LVGL is
`LV_COLOR_FORMAT_RGB565` with `stride = 512` (256 px × 2 bytes).

## Generating a pack from OSM PNG tiles

Any XYZ PNG tile set works as the source (downloaded with your tool of choice,
respecting the tile server's usage policy). Convert each PNG to the raw format
with ImageMagick:

```sh
# one tile
convert in.png -resize 256x256! -depth 8 \
  -define png:color-type=2 RGB565:out.bin   # if your IM build supports RGB565

# portable fallback (any ImageMagick): emit raw RGB then pack to 565 yourself,
# or use the Python helper below.
```

Portable Python converter (Pillow), walks a PNG tree and writes the `.bin` tree:

```python
import os, struct, sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]   # e.g. tiles_png  /Volumes/SD/tiles
for root, _, files in os.walk(src):
    for f in files:
        if not f.endswith(".png"):
            continue
        rel = os.path.relpath(os.path.join(root, f), src)
        out = os.path.join(dst, rel[:-4] + ".bin")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        im = Image.open(os.path.join(root, f)).convert("RGB").resize((256, 256))
        buf = bytearray(256 * 256 * 2)
        i = 0
        for (r, g, b) in im.getdata():
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)  # RGB565
            struct.pack_into("<H", buf, i, v)   # little-endian, no swap
            i += 2
        with open(out, "wb") as fh:
            fh.write(buf)
```

Copy the resulting `tiles/` tree to the SD card root. With a pack present and a
GPS fix, the wardrive center panel shows the map, your live track polyline, and
a cyan current-position marker; the "GPS" action button toggles follow-mode and
recenters on the fix; the +/- buttons change zoom.

## Sizing note

Raw tiles are larger than PNG (~128 KB each vs ~10–50 KB). A city-sized pack at
zoom 16 runs to a few hundred MB — fine for a wardrive SD card. The renderer
holds at most ~40 tiles (~5 MB) in PSRAM at once and reads new tiles one at a
time, releasing the SPI mutex between each so the CSV/SQLite writer never
starves.
