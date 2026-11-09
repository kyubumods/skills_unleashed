"""Builds the two models of the Ninja's Flash Bomb:

    assets/models/skills_unleashed-ninja-bomb.cub    the smoke bomb: a charcoal ball with a black band round its middle, a pale
                                                     glint on the upper left and a lit fuse on top (7 x 7 x 9 voxels)
    assets/models/skills_unleashed-ninja-flash.cub   the flash at the impact: a white ball with a pale grey rim (11 voxels across)

The smoke is not a model: it is the game's particles (tuning.lua, `ninja.smoke`). Z is up.

    python make_ninja_bomb.py [out folder]     (default: the mod's assets/models)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "cwmod"))
import cubvox  # noqa: E402

WHITE = (246, 246, 240)
PALE = (206, 208, 210)
DARK = (70, 72, 78)
CHAR = (44, 45, 50)
BLACK = (18, 18, 22)
SPARK = (255, 236, 150)
FIRE = (255, 150, 40)

BALL_R = 3                       # the bomb's ball: radius in voxels (7 across)
GLINT = ((1, 1, 5), (2, 1, 5), (1, 2, 5), (1, 1, 4))
FLASH_R = 5                      # the flash: radius in voxels (11 across)


def bomb():
    n = 2 * BALL_R + 1
    size = (n, n, n + 2)
    vox = {}
    c = BALL_R
    for x in range(n):
        for y in range(n):
            for z in range(n):
                if math.sqrt((x - c) ** 2 + (y - c) ** 2 + (z - c) ** 2) <= c + 0.2:
                    col = BLACK if z == c else CHAR
                    if (x, y, z) in GLINT:
                        col = PALE
                    vox[(x, y, z)] = col
    vox[(c, c, n)] = DARK            # the fuse
    vox[(c, c + 1, n)] = FIRE
    vox[(c, c + 1, n + 1)] = SPARK
    return size, vox


def ball(r, inner, outer):
    n = 2 * r + 1
    vox = {}
    for x in range(n):
        for y in range(n):
            for z in range(n):
                d = math.sqrt((x - r) ** 2 + (y - r) ** 2 + (z - r) ** 2)
                if d <= r + 0.3:
                    vox[(x, y, z)] = inner if d < r * 0.6 else outer
    return (n, n, n), vox


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "assets", "models")
    for name, (size, vox) in (("skills_unleashed-ninja-bomb", bomb()), ("skills_unleashed-ninja-flash", ball(FLASH_R, WHITE, PALE))):
        path = os.path.join(out, name + ".cub")
        cubvox.write_cub(path, size, vox)
        print(path, size, len(vox))


if __name__ == "__main__":
    main()
