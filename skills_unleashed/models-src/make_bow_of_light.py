"""Builds skills_unleashed-bow-of-light.cub: the bow of light the Ranger's Sniper calls down. The shape is a picture traced onto a 33 x 9 grid. Then the bow is stretched by EXTEND voxels at each end (2 each side, 37 long) and made
DEPTH voxels thick.

The grid is read like the picture: the top row first, '#' a whole voxel, '+' an edge voxel, '.' empty. The shading follows the
picture's light, which falls from above: a voxel with nothing above it is lit white, one with nothing below it is the deep
blue of the underside, and the rest are the pale blue of the glow. The bow's long axis is z (up, as in Cube World), its bulge
goes up the y axis, and its thickness is x. The string is the bottom row's '+' cells, one voxel deep, so it stays a thin line.
The mod draws assets/models/skills_unleashed-bow-of-light_edited.cub, a copy of this output edited by hand.

    python make_bow_of_light.py [out.cub] [preview.png]    (default: models-src/out/, which git ignores)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

GRID = """
........++###+.....+###++........
.......+#######...#######+.......
......#####################......
.....#####..+#######+..#####.....
....+###.....##.+.##.....###+....
....##+...................+##....
...##+.....................+##...
####+.......................+####
+##+++++++++++++++++++++++++++##+
"""
COLOURS = {"lit": (255, 255, 255), "glow": (214, 234, 252), "under": (172, 206, 240)}
EXTEND = 2               # voxels added at each end of the bow
DEPTH = 3                # voxels thick (x)


def stretch(rows, width):
    """Each row stretched to `width` columns, nearest neighbour: the ends move out, the middle stays where it is."""
    old = len(rows[0])
    return ["".join(row[round(j * (old - 1) / (width - 1))] for j in range(width)) for row in rows]


def build():
    rows = [r for r in GRID.split("\n") if r]
    assert all(len(r) == len(rows[0]) for r in rows), "every row of GRID must be the same length"
    width = len(rows[0]) + 2 * EXTEND
    rows = stretch(rows, width)
    height = len(rows)
    filled = {}                             # (y, z) -> the picture's character
    for i, row in enumerate(rows):
        for z, ch in enumerate(row):
            if ch != ".":
                filled[(height - 1 - i, z)] = ch   # the top row of the picture is the top of the bow; the bottom row is y 0
    vox = {}
    for (y, z), ch in filled.items():
        if (y + 1, z) not in filled:
            colour = COLOURS["lit"]
        elif (y - 1, z) not in filled:
            colour = COLOURS["under"]
        else:
            colour = COLOURS["glow"]
        if y == 0 and ch == "+":            # the string: one voxel deep
            vox[(0, y, z)] = COLOURS["lit"]
            continue
        for x in range(DEPTH):
            vox[(x, y, z)] = colour
    return (DEPTH, height, width), vox


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "skills_unleashed-bow-of-light.cub")
    size, vox = build()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    cubvox.write_cub(out, size, vox)
    print(f"wrote {out}: {size[0]}x{size[1]}x{size[2]}, {len(vox)} voxels, {len(set(vox.values()))} colours")
    if len(sys.argv) > 2:
        cubvox.preview(out, sys.argv[2], 16)


if __name__ == "__main__":
    main()
