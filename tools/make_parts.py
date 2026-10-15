#!/usr/bin/env python3
"""Cut the Gemini sheets in assets/src into the pictures in assets/art, and write
src/art.h (every picture, for the program to build in).

  01-harpo.jpeg   Harpo, full length, 4 x 3 poses: harpo0 .. harpo11, in reading order
                  (each pose is everything in its cell, so motion lines stay with it)
  02-things.jpeg  the 16 things in his coat, found by where each lies on the sheet
  03-stage.jpeg   the theatre (the Groucho Clock's), as it is

    tools/make_parts.py
"""
import os, subprocess
import numpy as np
from PIL import Image, ImageFilter

SRC, OUT = "assets/src", "assets/art"
# the things, and where the middle of each is on its sheet (as part of the sheet)
THINGS = {
    "candle": (0.13, 0.11), "coffee": (0.38, 0.11), "fish": (0.62, 0.15), "torch": (0.88, 0.15),
    "mallet": (0.14, 0.34), "duck": (0.38, 0.34), "scissors": (0.63, 0.39), "sandwich": (0.88, 0.34),
    "telescope": (0.14, 0.59), "alarm": (0.37, 0.59), "icecream": (0.55, 0.61), "boat": (0.88, 0.58),
    "flowers": (0.14, 0.83), "lamp": (0.38, 0.84), "ladder": (0.65, 0.75), "anchor": (0.88, 0.85),
}

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def blobs(mask):
    lab = np.zeros(mask.shape, np.int32); n = 0; H, W = mask.shape
    for y0, x0 in zip(*np.nonzero(mask)):
        if lab[y0, x0]: continue
        n += 1; lab[y0, x0] = n; st = [(y0, x0)]
        while st:
            y, x = st.pop()
            for ny, nx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= ny < H and 0 <= nx < W and mask[ny, nx] and not lab[ny, nx]:
                    lab[ny, nx] = n; st.append((ny, nx))
    return lab, n

def save_part(k, keep, name):
    ys, xs = np.nonzero(keep)
    a, b, c, d = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    part = k[b:d, a:c].copy()
    part[..., 3] = np.where(keep[b:d, a:c], part[..., 3], 0)
    Image.fromarray(part, "RGBA").save(f"{OUT}/{name}.png", optimize=True)
    print(f"{name}.png {c - a}x{d - b}")
    return a, b, c, d

def harpo():
    k = key(np.asarray(Image.open(f"{SRC}/01-harpo.jpeg").convert("RGB")).astype(float))
    H, W = k.shape[:2]
    solid = k[..., 3] > 100
    lines = []
    for r in range(3):
        for c in range(4):
            cell = np.zeros_like(solid)
            cell[r * H // 3:(r + 1) * H // 3, c * W // 4:(c + 1) * W // 4] = True
            keep = solid & cell
            a, b, cc, d = save_part(k, keep, f"harpo{r * 4 + c}")
            lines.append((a, b, cc, d))
    return lines

def things():
    k = key(np.asarray(Image.open(f"{SRC}/02-things.jpeg").convert("RGB")).astype(float))
    H, W = k.shape[:2]
    grown = np.asarray(Image.fromarray(((k[..., 3] > 100) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9))) > 0
    lab, n = blobs(grown)
    owner = {}
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        if len(ys) < 150: continue
        cx, cy = xs.mean() / W, ys.mean() / H
        name = min(THINGS, key=lambda t: (THINGS[t][0] - cx) ** 2 + (THINGS[t][1] - cy) ** 2)
        owner.setdefault(name, []).append(i)
    for name in THINGS:
        if name not in owner: print("MISSING", name); continue
        keep = np.isin(lab, owner[name]) & (k[..., 3] > 100)
        save_part(k, keep, name)

def write_list():
    files = sorted(f for f in os.listdir(OUT) if f.endswith((".png", ".jpg")))
    items = [f'X({os.path.splitext(f)[0].replace("-", "_")}, "{f}")' for f in files]
    lines = ["/* Made by tools/make_parts.py: every picture in assets/art. */", "#define SPRITES \\"]
    for i in range(0, len(items), 4):
        lines.append("    " + " ".join(items[i:i + 4]) + (" \\" if i + 4 < len(items) else ""))
    open("src/art.h", "w").write("\n".join(lines) + "\n")
    print(f"src/art.h: {len(files)} pictures")

def main():
    os.makedirs(OUT, exist_ok=True)
    harpo()
    things()
    subprocess.run(["magick", f"{SRC}/03-stage.jpeg", "-resize", "2560x", "-quality", "88", f"{OUT}/stage.jpg"], check=True)
    print("stage.jpg")
    write_list()

if __name__ == "__main__":
    main()
