"""Builds the two models of the Scout's bear trap: a round trap of two half rings with
teeth (the jaws) and a pressure plate in the middle.

    python make_scout_trap.py --cub <folder>     scout-trap-jaw.cub and scout-trap-plate.cub (the mod's assets/models, the Animator's
                                                 --models)
    python make_scout_trap.py --vox <folder>     the same two as .vox
    python make_scout_trap.py --art              the layers as text, to read the shape

Z is up, y is forward, x is to the right. The open trap is a circle of DIAMETER voxels lying on the ground:

    plate   the bar across the middle (the row y = 0, the hinge line), the disc on it and the raised pad: one rigid piece.
    jaw     the front half of the ring, the rows y = 1 and up, lying flat: one layer of steel and, on every second voxel of its
            outer edge, a tooth standing on it. The back jaw is the same model turned half a turn round z.

The trap is the same seen from either side and from the front or the back: the teeth are counted from the top of the jaw's
arch, which has none, outward both ways, and no tooth is on the inside of the ring. So the last
voxel beside the bar has none either, and a tooth of one jaw faces a tooth of the other.

A jaw turns round the line at the inner edge of its first row, one voxel above the ground (in the model: the lower edge in y,
the plane between its two layers). At a quarter turn it stands on the bar with its steel layer on its own side and its teeth
over the bar, where the two jaws' teeth fill the same voxels.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

DIAMETER = 13          # voxels, odd: the centre is the middle of a voxel
RING_INNER = 4.4       # a voxel is ring when its centre is farther than this from the trap's centre
DISC = 3.3             # radius of the plate's disc
PAD = 2.3              # radius of the raised pad on it
STEEL = (170, 176, 190)
TOOTH = (232, 236, 244)
IRON = (74, 76, 88)
DISC_IRON = (98, 100, 114)
PAD_BRONZE = (204, 146, 58)

R = DIAMETER // 2
OUTER = R + 0.6


def jaw():
    """The front jaw lying flat: rows y = 1 .. R at index 0 .. R - 1, layer 0 steel, layer 1 the teeth."""
    ring = [(x, y) for x in range(-R, R + 1) for y in range(1, R + 1) if RING_INNER < math.hypot(x, y) <= OUTER]
    # the outer edge: a ring voxel with nothing of the circle beside it, on one of its four sides
    edge = [(x, y) for x, y in ring
            if any(math.hypot(x + dx, y + dy) > OUTER for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    right = sorted((p for p in edge if p[0] >= 0), key=lambda p: -math.atan2(p[1], p[0]))   # from the top (x = 0) down to the bar
    teeth = {(sx * x, y) for x, y in right[1::2] for sx in (1, -1)}
    vox = {}
    for x, y in ring:
        vox[(x + R, y - 1, 0)] = STEEL
        if (x, y) in teeth:
            vox[(x + R, y - 1, 1)] = TOOTH
    return (DIAMETER, R, 2), vox


def plate():
    """The bar, the disc and the pad: rows y = -3 .. 3 at index 0 .. 6."""
    half = int(DISC)
    vox = {}
    for x in range(-R, R + 1):
        vox[(x + R, half, 0)] = IRON
        for y in range(-half, half + 1):
            r = math.hypot(x, y)
            if r <= DISC:
                vox[(x + R, y + half, 0)] = DISC_IRON if y else IRON
            if r <= PAD:
                vox[(x + R, y + half, 1)] = PAD_BRONZE
    return (DIAMETER, 2 * half + 1, 2), vox


MODELS = {"scout-trap-jaw": jaw, "scout-trap-plate": plate}


def art(size, vox):
    sx, sy, sz = size
    marks = {STEEL: "o", TOOTH: "A", IRON: "=", DISC_IRON: "+", PAD_BRONZE: "#"}
    out = []
    for z in range(sz):
        out.append(f"layer {z}")
        for y in range(sy - 1, -1, -1):
            out.append("  " + "".join(marks.get(vox.get((x, y, z)), ".") for x in range(sx)))
    return "\n".join(out)


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--art":
        for name, build in MODELS.items():
            size, vox = build()
            print(f"{name}: {size[0]}x{size[1]}x{size[2]}, {len(vox)} voxels")
            print(art(size, vox))
        return
    if len(sys.argv) != 3 or sys.argv[1] not in ("--cub", "--vox"):
        sys.exit(__doc__)
    kind, folder = sys.argv[1][2:], sys.argv[2]
    os.makedirs(folder, exist_ok=True)
    write = cubvox.write_cub if kind == "cub" else cubvox.write_vox
    for name, build in MODELS.items():
        size, vox = build()
        out = os.path.join(folder, f"{name}.{kind}")
        write(out, size, vox)
        print(f"wrote {out}: {size[0]}x{size[1]}x{size[2]}, {len(vox)} voxels")


if __name__ == "__main__":
    main()
