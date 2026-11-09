"""Builds the Ninja's Flash Bomb burst sound, in place of the game's Explosion (id 81). Synthesized, nothing copied: the building blocks (kick, crack, noise bands, the compressor and
limiter) are make_water_mage_sounds.py's. A smoke bomb is a snap and a soft push of air, then smoke hissing out, without the
rumble of a real explosion:

    bang_a   poof: a small snap, a low thump, a puff of air and a short hiss of smoke (0.9 s)
    bang_b   thud: muffled and heavy, a deep thump under a dark puff, little snap or hiss (0.8 s)
    bang_c   snap: a sharp firecracker crack, a quick light puff and a hiss (0.6 s)
    bang_d   hiss: no snap, a soft thump and a long stream of smoke hissing out (1.2 s)

A softer set: no crack, no compressor or limiter, every part eased in over
10 to 60 ms, the noise rounded off by a gentle low pass, and a quieter peak:

    bang_e   whump: a round low thump under a dark puff of air, nothing bright (0.7 s)
    bang_f   pfff: a soft puff swelling in and a gentle airy tail of smoke (0.9 s)
    bang_g   fwoosh: a cloud billowing out, its brightness falling as it spreads, over a faint thump (1.0 s)
    bang_h   distant: a deep soft boom and a long low rush, as if heard from afar (1.1 s)

A third set, on the lines of the game's booms. The game's booms (Explosion, smash1,
destroy2, salvo2, Fireball, slam-ground) were measured, not copied. They are dark: 80 to 97% of their energy is under 300 Hz and
almost none is over 2 kHz, where a to h had their hiss. Explosion opens with a low tone near 70 Hz and a body at 250 to 500 Hz
(loudest near 300 Hz) in its first 80 ms; then it crackles: its loudness jumps 35 times in 200..500 ms and 100 times in
1..2.5 s, over a rumble of 40 to 120 Hz, its brightness falling from 1.4 kHz to 450 Hz. destroy2 and salvo2 are a few hits in a
row, 50 to 150 ms apart. So the third set is built from those parts: a pop near 300 Hz, a low tone, a rolling rumble, and
crackle (many tiny dark bursts, fewer and duller as it dies), all under 2.5 kHz:

    bang_i   crackle: pop, low tone and a crackle thinning out over a rumble, like the game's Explosion in 1.2 s
    bang_j   roll: pop and low tone, then a rolling rumble of smoke with only a little crackle (1.5 s)
    bang_k   pellets: four pops in a row 40 to 60 ms apart, each with its own thump, then a short crackle (1.0 s)
    bang_l   puff: pop and a dark puff of air billowing out, embers fizzling over it (1.2 s)
    bang_m   deep: a heavy sub thump like smash1, a short dark crackle over it (0.9 s)

bang_l (the one in assets/sounds), brighter step by step (same seed, same timing):

    bang_n   the low pass 2.4 to 4.5 kHz, the puffs starting at 2.8 and 2.4 kHz, the embers up to 3.5 kHz
    bang_o   bang_n with a higher pop (340 Hz), louder embers and a short breath of 1 to 5 kHz air at the start
    bang_p   bang_o brighter still: the low pass at 7 kHz, the puffs from 4 and 3.2 kHz, the embers up to 5 kHz

    python make_ninja_sounds.py [bang=a..p]    every variant to preview/, the chosen one (default l) to assets/sounds/nj_bang.wav
"""
import os
import sys

import numpy as np
import pedalboard

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_water_mage_sounds import (ASSETS, PREVIEW, RATE, blank, crack, kick, noise_band, place, strike,  # noqa: E402
                                    write_wav)


def puff(rng, dur, lo, hi, swell=0.04):
    """The push of air: noise between lo and hi Hz, in at once and fading over `dur` s."""
    return noise_band(dur, lo, hi, rng, swell=swell)


def hiss(rng, dur, lo=1500, hi=9000, swell=0.15, flutter=7.0):
    """The smoke streaming out: bright noise swelling in and dying away, wavering `flutter` times a second."""
    x = noise_band(dur, lo, hi, rng, swell=swell)
    t = np.arange(len(x)) / RATE
    return x * (0.8 + 0.2 * np.sin(2 * np.pi * flutter * t))


def silence_like(track): return np.zeros_like(track)


def soft_kick(f0, f1, dur, attack_ms=10.0, decay=6.0):
    """A round low thump: a sine gliding from f0 to f1 Hz, eased in over `attack_ms`, dying away exponentially."""
    t = np.arange(int(dur * RATE)) / RATE
    f = f1 + (f0 - f1) * np.exp(-6 * t / dur)
    x = np.sin(2 * np.pi * np.cumsum(f) / RATE)
    a = np.minimum(1.0, t / (attack_ms / 1000))
    a = a * a * (3 - 2 * a)
    return x * a * np.exp(-decay * t / dur)


def eased(x, attack_ms):
    """`x` faded in over `attack_ms` (a smooth step), so nothing starts with a click."""
    n = min(len(x), int(attack_ms / 1000 * RATE))
    u = np.linspace(0, 1, n)
    x = x.copy()
    x[:n] *= u * u * (3 - 2 * u)
    return x


def falling(rng, dur, top0, top1, lo=80, swell=0.12):
    """Noise whose brightness falls from `top0` to `top1` Hz over `dur`: made of short overlapping windows, each low-passed at its
    own cutoff (a billowing cloud getting duller as it spreads)."""
    n = int(dur * RATE)
    win = int(0.04 * RATE)
    hop = win // 2
    out = np.zeros(n + win)
    w = np.hanning(win)
    src = rng.standard_normal(n + win)
    for i in range(0, n, hop):
        top = top0 * (top1 / top0) ** (i / n)
        spec = np.fft.rfft(src[i:i + win] * w)
        fr = np.fft.rfftfreq(win, 1 / RATE)
        spec *= 1 / (1 + (fr / top) ** 4)
        spec[fr < lo] = 0
        out[i:i + win] += np.fft.irfft(spec, win)
    out = out[:n] / (np.abs(out[:n]).max() + 1e-9)
    t = np.arange(n) / n
    return out * np.where(t < swell, t / swell, np.exp(-3.5 * (t - swell) / (1 - swell)))


def gentle(track, lp_hz, peak=0.7, fade_ms=120):
    """No compressor: a gentle low pass and a high pass for the rumble, a fade out, the peak set low."""
    board = pedalboard.Pedalboard([pedalboard.HighpassFilter(cutoff_frequency_hz=30),
                                   pedalboard.LowpassFilter(cutoff_frequency_hz=lp_hz)])
    track = board(np.ascontiguousarray(track.T, dtype=np.float32), RATE).T.astype(np.float64)
    n = int(fade_ms / 1000 * RATE)
    track[-n:] *= np.linspace(1, 0, n)[:, None]
    track -= track.mean(axis=0)
    return track * (peak / (np.abs(track).max() + 1e-9))


def bang_e(rng):
    tr = blank(0.7)
    place(tr, soft_kick(85, 40, 0.5, attack_ms=8), 0.0, level=1.0)
    place(tr, eased(puff(rng, 0.5, 60, 900, swell=0.08), 15), 0.0, level=0.6)
    return gentle(tr, 1500, peak=0.7)


def bang_f(rng):
    tr = blank(0.9)
    place(tr, soft_kick(110, 50, 0.3, attack_ms=12), 0.0, level=0.45)
    place(tr, eased(puff(rng, 0.4, 150, 2500, swell=0.12), 25), 0.0, pan=-0.1, level=0.9)
    place(tr, eased(hiss(rng, 0.8, 600, 3000, swell=0.25, flutter=4.0), 40), 0.04, pan=0.15, level=0.3)
    return gentle(tr, 3000, peak=0.65)


def bang_g(rng):
    tr = blank(1.0)
    place(tr, soft_kick(90, 45, 0.4, attack_ms=15), 0.0, level=0.5)
    place(tr, falling(rng, 0.95, 3500, 400, swell=0.1), 0.0, pan=0.1, level=0.9)
    place(tr, falling(rng, 0.9, 2800, 300, swell=0.14), 0.02, pan=-0.15, level=0.7)
    return gentle(tr, 4000, peak=0.65)


def bang_h(rng):
    tr = blank(1.1)
    place(tr, soft_kick(70, 32, 0.8, attack_ms=20, decay=5.0), 0.0, level=1.0)
    place(tr, eased(puff(rng, 1.0, 40, 600, swell=0.15), 60), 0.01, level=0.55)
    return gentle(tr, 900, peak=0.7, fade_ms=200)


def bang_a(rng):
    tr = blank(0.9)
    place(tr, crack(rng, ms=18, top=12000), 0.0, level=0.35)
    place(tr, kick(120, 40, 30, 60, 0.35), 0.0, level=0.8)
    place(tr, puff(rng, 0.45, 120, 3000), 0.004, pan=-0.1, level=1.0)
    place(tr, hiss(rng, 0.8), 0.03, pan=0.15, level=0.3)
    return strike(silence_like(tr), tr, gain_db=8.0, fade_ms=80, ratio=3.0, peak=0.9)


def bang_b(rng):
    tr = blank(0.8)
    place(tr, crack(rng, ms=12, top=5000), 0.0, level=0.15)
    place(tr, kick(95, 34, 40, 120, 0.5), 0.0, level=1.0)
    place(tr, puff(rng, 0.55, 60, 1200, swell=0.06), 0.006, level=0.9)
    place(tr, hiss(rng, 0.6, 1200, 5000), 0.05, level=0.15)
    return strike(silence_like(tr), tr, gain_db=9.0, fade_ms=80, ratio=3.5, peak=0.9)


def bang_c(rng):
    tr = blank(0.6)
    place(tr, crack(rng, ms=30, top=16000), 0.0, level=0.9)
    place(tr, kick(160, 50, 25, 40, 0.22), 0.0, level=0.5)
    place(tr, puff(rng, 0.25, 300, 5000, swell=0.02), 0.002, pan=0.1, level=0.7)
    place(tr, hiss(rng, 0.5, 2500, 11000, swell=0.1), 0.02, pan=-0.15, level=0.35)
    return strike(silence_like(tr), tr, gain_db=7.0, fade_ms=60, ratio=3.0, peak=0.88)


def bang_d(rng):
    tr = blank(1.2)
    place(tr, kick(110, 38, 30, 70, 0.4), 0.0, level=0.6)
    place(tr, puff(rng, 0.4, 100, 2500, swell=0.08), 0.0, level=0.8)
    place(tr, hiss(rng, 1.1, 1200, 8000, swell=0.2, flutter=5.0), 0.02, pan=0.2, level=0.55)
    place(tr, hiss(rng, 1.0, 1800, 9000, swell=0.25, flutter=6.5), 0.05, pan=-0.2, level=0.45)
    return strike(silence_like(tr), tr, gain_db=7.0, fade_ms=150, ratio=2.5, peak=0.85)


def dark_noise(rng, n, lo, hi):
    """`n` samples of noise kept between lo and hi Hz with soft edges (no ringing), peak 1."""
    spec = np.fft.rfft(rng.standard_normal(n))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    spec *= 1 / (1 + (fr / hi) ** 4) * (1 - 1 / (1 + (fr / lo) ** 2))
    x = np.fft.irfft(spec, n)
    return x / (np.abs(x).max() + 1e-9)


def pop(rng, f=300, dur=0.08, glide=0.7, noise=0.35):
    """The game's Explosion's first body: a tone near `f` Hz sliding down to `glide` times itself, with a little dark noise,
    eased in over 3 ms and gone in `dur` s."""
    t = np.arange(int(dur * RATE)) / RATE
    fr = f * (glide + (1 - glide) * np.exp(-5 * t / dur))
    x = np.sin(2 * np.pi * np.cumsum(fr) / RATE) + noise * dark_noise(rng, len(t), f * 0.5, f * 3)
    return eased(x * np.exp(-5 * t / dur), 3)


def rumble(rng, dur, lo=30, hi=220, swell=0.06, roll_hz=9.0, roll=0.5, decay=3.0):
    """The low roll under it: noise from lo to hi Hz whose loudness wanders `roll_hz` times a second by up to `roll`."""
    n = int(dur * RATE)
    x = dark_noise(rng, n, lo, hi)
    k = max(2, int(RATE / roll_hz))
    wobble = np.convolve(rng.standard_normal(n + k), np.hanning(k), "same")[:n]
    wobble /= np.abs(wobble).max() + 1e-9
    t = np.arange(n) / n
    shape = np.where(t < swell, t / swell, np.exp(-decay * (t - swell) / (1 - swell)))
    return x * shape * (1 - roll + roll * (0.5 + 0.5 * wobble))


def crackle(track, rng, dur, level=1.0, rate0=350, rate1=40, top0=1800, top1=450, lo=90, ms=(2, 12), decay=2.5, start=0.0):
    """The game's Explosion's crackle: tiny bursts of dark noise, `rate0` a second at first thinning to `rate1`, each between lo Hz
    and a top falling from `top0` to `top1` Hz, at random levels and places left to right, added into the stereo `track`."""
    t = start
    while t < dur:
        u = t / dur
        rate = rate0 * (rate1 / rate0) ** u
        top = top0 * (top1 / top0) ** u
        g = int(rng.uniform(*ms) / 1000 * RATE)
        burst = dark_noise(rng, g, lo, top) * np.exp(-5 * np.arange(g) / g)
        burst = eased(burst, 0.6)
        place(track, burst, t, pan=rng.uniform(-0.9, 0.9), level=level * np.exp(-decay * u) * np.exp(rng.uniform(-2.0, 0)))
        t += rng.exponential(1 / rate)


def dark(track, lp_hz, peak=0.85, fade_ms=150, ratio=2.0):
    """The game's booms are loud and dark: a low pass, a gentle compressor (slow attack, so the pop stays round) to hold the tail
    up, a limiter, a fade out."""
    board = pedalboard.Pedalboard([pedalboard.HighpassFilter(cutoff_frequency_hz=25),
                                   pedalboard.LowpassFilter(cutoff_frequency_hz=lp_hz),
                                   pedalboard.Compressor(threshold_db=-18, ratio=ratio, attack_ms=15, release_ms=150),
                                   pedalboard.Gain(gain_db=4.0),
                                   pedalboard.Limiter(threshold_db=-1.0, release_ms=80)])
    track = track / (np.abs(track).max() + 1e-9) * 0.5
    track = board(np.ascontiguousarray(track.T, dtype=np.float32), RATE).T.astype(np.float64)
    n = int(fade_ms / 1000 * RATE)
    track[-n:] *= np.linspace(1, 0, n)[:, None] ** 2
    track -= track.mean(axis=0)
    return track * (peak / (np.abs(track).max() + 1e-9))


def wide_rumble(track, rng, dur, at=0.0, level=1.0, **kw):
    """Two rumbles of their own noise, one to each side: the game's booms are wide (their sides differ by about 0.4)."""
    place(track, rumble(rng, dur, **kw), at, pan=-0.7, level=level)
    place(track, rumble(rng, dur, **kw), at, pan=0.7, level=level)


def bang_i(rng):
    tr = blank(1.2)
    place(tr, soft_kick(110, 45, 0.45, attack_ms=3), 0.0, level=0.8)
    place(tr, pop(rng, 300, 0.1), 0.0, level=1.0)
    wide_rumble(tr, rng, 1.15, 0.01, level=0.5, lo=30, hi=250, roll_hz=10, decay=2.0)
    crackle(tr, rng, 1.1, level=1.6, lo=150, top0=2400, top1=600, decay=1.6, start=0.03)
    return dark(tr, 2400)


def bang_j(rng):
    tr = blank(1.5)
    place(tr, soft_kick(100, 40, 0.5, attack_ms=4), 0.0, level=0.8)
    place(tr, pop(rng, 280, 0.12, noise=0.6), 0.0, level=0.9)
    wide_rumble(tr, rng, 1.45, 0.0, level=0.7, lo=30, hi=300, swell=0.1, roll_hz=7, roll=0.7, decay=1.8)
    wide_rumble(tr, rng, 1.4, 0.03, level=0.4, lo=120, hi=600, swell=0.12, roll_hz=5, roll=0.8, decay=2.2)
    crackle(tr, rng, 1.3, level=0.9, rate0=120, rate1=15, lo=150, top0=1800, top1=500, decay=1.6, start=0.05)
    return dark(tr, 1800, fade_ms=250)


def bang_k(rng):
    tr = blank(1.0)
    for at, f, lvl, pan in ((0.0, 320, 1.0, 0.0), (0.045, 270, 0.8, -0.5), (0.095, 360, 0.7, 0.5), (0.15, 240, 0.55, -0.2)):
        place(tr, soft_kick(f * 0.36, f * 0.15, 0.22, attack_ms=2), at, pan=pan, level=0.7 * lvl)
        place(tr, pop(rng, f, 0.07, noise=0.6), at, pan=pan, level=lvl)
    wide_rumble(tr, rng, 0.8, 0.02, level=0.35, lo=30, hi=250, roll_hz=12, decay=2.5)
    crackle(tr, rng, 0.8, level=1.4, rate0=280, rate1=30, lo=150, top0=2200, top1=600, start=0.17, decay=2.2)
    return dark(tr, 2400)


def l_family(rng, lp=2400, tops=(1600, 1300), ends=(250, 200), embers=(2400, 1200, 400, 0.6), pop_hz=260, air=0.0):
    """bang_l and its brighter versions: `lp` the final low pass, `tops` and `ends` where the two puffs' brightness starts and
    ends, `embers` the fizzle's (top0, top1, lo, level), `air` the level of a short breath of 1 to 5 kHz noise at the start."""
    tr = blank(1.2)
    place(tr, soft_kick(95, 42, 0.4, attack_ms=4), 0.0, level=0.7)
    place(tr, pop(rng, pop_hz, 0.09), 0.0, level=0.8)
    place(tr, falling(rng, 1.1, tops[0], ends[0], lo=60, swell=0.08), 0.0, pan=-0.6, level=0.8)
    place(tr, falling(rng, 1.0, tops[1], ends[1], lo=60, swell=0.12), 0.03, pan=0.6, level=0.8)
    if air:
        place(tr, eased(puff(rng, 0.3, 1000, 5000, swell=0.05), 5), 0.0, pan=0.1, level=air)
    crackle(tr, rng, 1.15, level=embers[3], rate0=60, rate1=20, top0=embers[0], top1=embers[1], lo=embers[2], ms=(1, 5),
            decay=1.2, start=0.1)
    return dark(tr, lp, fade_ms=200)


def bang_l(rng): return l_family(rng)
def bang_n(rng): return l_family(rng, lp=4500, tops=(2800, 2400), ends=(400, 350), embers=(3500, 1800, 500, 0.7))
def bang_o(rng): return l_family(rng, lp=4500, tops=(2800, 2400), ends=(400, 350), embers=(3500, 1800, 500, 0.9), pop_hz=340,
                                 air=0.25)
def bang_p(rng): return l_family(rng, lp=7000, tops=(4000, 3200), ends=(600, 500), embers=(5000, 2500, 700, 0.8), pop_hz=340,
                                 air=0.35)


def bang_m(rng):
    tr = blank(0.9)
    place(tr, kick(75, 32, 30, 120, 0.6, attack_ms=2), 0.0, level=1.0)
    place(tr, pop(rng, 220, 0.08, glide=0.6), 0.0, level=0.7)
    wide_rumble(tr, rng, 0.7, 0.01, level=0.45, lo=25, hi=180, roll_hz=12, decay=3.0)
    crackle(tr, rng, 0.6, level=1.2, rate0=300, rate1=50, lo=120, top0=1600, top1=400, decay=2.5, start=0.02)
    return dark(tr, 1800)


VARIANTS = {"a": bang_a, "b": bang_b, "c": bang_c, "d": bang_d, "e": bang_e, "f": bang_f, "g": bang_g, "h": bang_h,
            "i": bang_i, "j": bang_j, "k": bang_k, "l": bang_l, "m": bang_m, "n": bang_n, "o": bang_o, "p": bang_p}
TARGET = "nj_bang.wav"


def main(args):
    pick = "l"                                # the variant in assets/sounds
    for a in args:
        kind, _, letter = a.partition("=")
        if kind != "bang" or letter not in VARIANTS:
            sys.exit("usage: make_ninja_sounds.py [bang=a..p]")
        pick = letter
    os.makedirs(PREVIEW, exist_ok=True)
    for letter, build in VARIANTS.items():
        track = build(np.random.RandomState(19))
        write_wav(os.path.join(PREVIEW, "nj_bang_%s.wav" % letter), track)
        if letter == pick:
            write_wav(os.path.join(ASSETS, TARGET), track)
            print("bang: variant %s -> assets/sounds/%s" % (letter, TARGET))


if __name__ == "__main__":
    main(sys.argv[1:])
