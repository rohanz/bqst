# Crops the manual's figures out of BqstEditorSnapshot renders.
#   uv run --with pillow python figures.py <raw-dir> <img-dir>
# <raw-dir> holds full.png (865 wide, pixelScale 3), full6.png (same at pixelScale 6, for the
# small grip close-up), about.png (pixelScale 3, mode about), menu.png, tips.png and vu.png
# (pixelScale 3). See figures.sh.
import sys
from pathlib import Path

from PIL import Image, ImageDraw

raw, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)

full = Image.open(raw / "full.png").convert("RGBA")
full6 = Image.open(raw / "full6.png").convert("RGBA")
about = Image.open(raw / "about.png").convert("RGBA")
assert full.size == (2595, 2622), full.size  # boxes below assume width 865 at pixelScale 3
assert full6.size == (5190, 5244), full6.size

# Boxes in pixels of the 3x render: (left, top, right, bottom).
crops = {
    "interface.png": (full, (0, 0, 2595, 2622)),
    "topbar.png": (full, (40, 30, 2555, 365)),
    "eq.png": (full, (50, 390, 1302, 2572)),
    "sat.png": (full, (1290, 390, 2545, 2572)),
    "switches.png": (full, (1420, 1180, 2400, 1370)),
    "grip.png": (full6, (4790, 4940, 5190, 5244)),
    "about.png": (about, (666, 966, 1926, 1656)),  # exactly the panel, border included
}
for name, (image, box) in crops.items():
    image.crop(box).save(out / name, optimize=True)

# The about panel has rounded corners; clear the editor showing through outside them.
panel = Image.open(out / "about.png")
mask = Image.new("L", panel.size, 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, panel.width - 1, panel.height - 1), radius=28, fill=255)
panel.putalpha(mask)
panel.save(out / "about.png", optimize=True)

for name in ("menu.png", "tips.png"):
    Image.open(raw / name).save(out / name, optimize=True)

# The meter render has wide transparent margins; trim to the meter so the figure edge is the meter edge.
vu = Image.open(raw / "vu.png").convert("RGBA")
vu.crop(vu.getchannel("A").getbbox()).save(out / "vu.png", optimize=True)

print("figures written to", out)
