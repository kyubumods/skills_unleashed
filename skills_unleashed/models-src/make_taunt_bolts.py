"""Builds assets/models/skills_unleashed-taunt-bolts.vox: the lightning bolt of the Guardian's taunt, drawn over the head of a
taunted creature in the place of the stun's mark. It is one upright bolt;
`BOLTS = 2` adds a second, tilted one.

The game draws the mark's model at the creature's scale x 0.1 per voxel, turning round the vertical axis, so the model is flat
(2 voxels thick) and about a block tall for a creature of scale 1. Z is up.

    python make_taunt_bolts.py [out.vox] [preview.png]

The outline below is the source (a hand drawing); a second bolt would be the same outline turned.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

# The outline of one bolt as drawn, in drawing units, z up: a wide top, a step, and
# a point. The second bolt is the same outline turned clockwise by TILT_DEG; both are rasterised into voxels (a voxel the outline
# covers fully is yellow, one it covers in part is the orange edge).
OUTLINE = [(5.9, 14.7), (9.6, 14.8), (5.9, 8.9), (9.6, 8.8), (2.1, 0.0), (5.7, 7.7), (0.0, 7.4)]
HEIGHT = 9             # voxels: the bolt's height (the mark draws a voxel as a tenth of a block at the creature's scale 1)
TILT_DEG = 70.0
BOLTS = 1              # 1: the upright bolt only; 2: and a tilted copy beside it
GAP = 2                # empty columns between the two bolts
YELLOW = (255, 225, 40)
ORANGE = (255, 140, 20)
THICK = 2              # voxels along y
SAMPLES = 5            # sub-samples per voxel side
FULL, PART = 0.8, 0.3  # covered share of a voxel for yellow, and for the orange edge


def turned(points, degrees):
    """The points turned clockwise by `degrees` round their centre."""
    cx = sum(x for x, _ in points) / len(points)
    cz = sum(z for _, z in points) / len(points)
    a = -math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return [(cx + (x - cx) * c - (z - cz) * s, cz + (x - cx) * s + (z - cz) * c) for x, z in points]


def inside(poly, x, z):
    hit = False
    j = len(poly) - 1
    for i, (xi, zi) in enumerate(poly):
        xj, zj = poly[j]
        if (zi > z) != (zj > z) and x < (xj - xi) * (z - zi) / (zj - zi) + xi:
            hit = not hit
        j = i
    return hit


def raster(poly):
    """{(x, z): colour} with the lower left voxel at (0, 0)."""
    unit = HEIGHT / max(z for _, z in OUTLINE)
    poly = [(x * unit, z * unit) for x, z in poly]
    x0, z0 = min(x for x, _ in poly), min(z for _, z in poly)
    poly = [(x - x0, z - z0) for x, z in poly]
    w, h = int(max(x for x, _ in poly)) + 1, int(max(z for _, z in poly)) + 1
    cells = {}
    for vx in range(w):
        for vz in range(h):
            n = sum(inside(poly, vx + (i + 0.5) / SAMPLES, vz + (j + 0.5) / SAMPLES)
                    for i in range(SAMPLES) for j in range(SAMPLES))
            share = n / (SAMPLES * SAMPLES)
            if share >= FULL:
                cells[(vx, vz)] = YELLOW
            elif share >= PART:
                cells[(vx, vz)] = ORANGE
    return cells


def build():
    one = raster(OUTLINE)
    cells = dict(one)
    height = max(z for _, z in one) + 1
    if BOLTS > 1:
        two = raster(turned(OUTLINE, TILT_DEG))
        width = max(x for x, _ in one) + 1
        height = max(height, max(z for _, z in two) + 1)
        for (x, z), c in two.items():
            cells[(x + width + GAP, z)] = c
    sx = max(x for x, _ in cells) + 1
    vox = {}
    for (x, z), c in cells.items():
        for y in range(THICK):
            vox[(x, y, z)] = c
    return (sx, THICK, height), vox


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "assets", "models", "skills_unleashed-taunt-bolts.vox")
    size, vox = build()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    cubvox.write_vox(out, size, vox)
    print(f"wrote {out}: {size[0]}x{size[1]}x{size[2]}, {len(vox)} voxels, 2 colours, {BOLTS} bolt(s)")
    if len(sys.argv) > 2:
        from PIL import Image
        sx, sy, sz = size
        img = Image.new("RGB", (sx, sz), (40, 40, 48))
        for (x, y, z), c in vox.items():
            img.putpixel((x, sz - 1 - z), c)
        img.resize((sx * 16, sz * 16), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()
