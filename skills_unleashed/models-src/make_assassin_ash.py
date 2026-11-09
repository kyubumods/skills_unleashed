"""Builds the model of the Assassin's aura:

    assets/models/skills_unleashed-assassin-ash.cub    the black ash swirling round the feet while casting: 70 specks on a rising
                                                       spiral of three turns, black, charcoal and dark grey (25 x 25 x 13 voxels)

The script turns it (and a second copy the other way) round the player's feet through EMTweaks' ghost. The smoke and the puffs are
not models: they are the game's particles (tuning.lua, `assassin`). Z is up.

    python make_assassin_ash.py [out folder]     (default: the mod's assets/models)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

BLACK = (12, 12, 16)
CHAR = (34, 34, 40)
DARK = (58, 58, 64)

SIZE = 25                        # the model's width and depth in voxels
HEIGHT = 13                      # and its height
SPECKS = 70
TURNS = 3


def h(x, y, z, k=0):
    """A repeatable noise in 0..1 for a point and a key."""
    n = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791) ^ (k * 2654435761)
    n &= 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def ash():
    c = SIZE / 2
    vox = {}
    for i in range(SPECKS):
        u = i / SPECKS
        ang = u * TURNS * 2 * math.pi + h(i, 1, 1, 20) * 0.4
        r = 8.5 + 2.5 * math.sin(u * 7) + 1.5 * (h(i, 2, 2, 21) - 0.5)
        z = int(u * 10 + 2 * h(i, 3, 3, 22))
        x, y = int(c + r * math.cos(ang)), int(c + r * math.sin(ang))
        vox[(x, y, z)] = (BLACK, CHAR, BLACK, DARK)[int(h(i, 4, 4, 23) * 3.99)]
        if h(i, 5, 5, 24) > 0.5 and 0 <= z + 1 < HEIGHT:
            vox[(x, y, z + 1)] = CHAR
    return (SIZE, SIZE, HEIGHT), vox


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "assets", "models")
    size, vox = ash()
    path = os.path.join(out, "skills_unleashed-assassin-ash.cub")
    cubvox.write_cub(path, size, vox)
    print(path, size, len(vox), "voxels")


if __name__ == "__main__":
    main()
