"""Builds assets/models/skills_unleashed-scout-trap-marker.cub: the V that hovers over each of the Scout's traps and points down at
it, to show where it lies. It is drawn the way the
Guardian's taunt bolt is (make_taunt_bolts.py): flat, 2 voxels thick, yellow with an orange edge. The Scout draws it as an EMTweaks
ghost turning round the vertical axis, so it is seen from every side.

    python make_trap_marker.py [out.cub] [preview.png]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

# A chevron pointing down, in drawing units, z up: two arms meeting at the point (6, 0).
OUTLINE = [(0.0, 10.0), (3.4, 10.0), (6.0, 4.2), (8.6, 10.0), (12.0, 10.0), (6.0, 0.0)]
HEIGHT = 9             # voxels
WIDTH = 11             # voxels, odd: the point is one middle column
YELLOW = (255, 225, 40)
ORANGE = (255, 140, 20)
THICK = 2              # voxels along y
SAMPLES = 5            # sub-samples per voxel side
FULL, PART = 0.8, 0.3  # covered share of a voxel for yellow, and for the orange edge


def inside(poly, x, z):
    hit = False
    j = len(poly) - 1
    for i, (xi, zi) in enumerate(poly):
        xj, zj = poly[j]
        if (zi > z) != (zj > z) and x < (xj - xi) * (z - zi) / (zj - zi) + xi:
            hit = not hit
        j = i
    return hit


def build():
    # Scaled so the V is exactly WIDTH voxels across; the left half (with the middle column) is rasterised and mirrored, so the
    # two arms are the same voxel for voxel (a V sampled straight onto the columns comes out lopsided).
    unit = HEIGHT / max(z for _, z in OUTLINE)
    sx = WIDTH / max(x for x, _ in OUTLINE)
    poly = [(x * sx, z * unit) for x, z in OUTLINE]
    w, h = WIDTH, int(max(z for _, z in poly)) + 1
    vox = {}
    for vx in range((w + 1) // 2):
        for vz in range(h):
            n = sum(inside(poly, vx + (i + 0.5) / SAMPLES, vz + (j + 0.5) / SAMPLES)
                    for i in range(SAMPLES) for j in range(SAMPLES))
            share = n / (SAMPLES * SAMPLES)
            colour = YELLOW if share >= FULL else ORANGE if share >= PART else None
            if colour:
                for y in range(THICK):
                    vox[(vx, y, vz)] = colour
                    vox[(w - 1 - vx, y, vz)] = colour
    return (w, THICK, h), vox


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "assets", "models",
                                                             "skills_unleashed-scout-trap-marker.cub")
    size, vox = build()
    cubvox.write_cub(out, size, vox)
    print(f"wrote {out}: {size[0]}x{size[1]}x{size[2]}, {len(vox)} voxels")
    if len(sys.argv) > 2:
        from PIL import Image
        sx, _, sz = size
        img = Image.new("RGB", (sx, sz), (40, 40, 48))
        for (x, _, z), c in vox.items():
            img.putpixel((x, sz - 1 - z), c)
        img.resize((sx * 16, sz * 16), Image.NEAREST).save(sys.argv[2])


if __name__ == "__main__":
    main()
