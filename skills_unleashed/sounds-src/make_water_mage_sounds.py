"""Builds the Water Mage's Chain of the Deep sounds: the cast, and the hit of the chains.

The game's water mage sounds (watersplash, watersplash-hit) were measured, not copied: each is a short "bloop", a triangle-like
tone (odd harmonics, the third at about a ninth) rising fast over about 85 ms (the splash from 560 to 940 Hz, the hit from 430 to
1570 Hz with a side tone 150 Hz under it), played twice and then echoed from side to side every 180 ms at about half the level,
for one second. The sounds here are synthesized on the same lines and made deeper and heavier for the chains:

    cast_a   three rolling bloops climbing (280 to 780 Hz) over a low thump and a soft rush of water, the bloops' tops cut and
             their starts eased: the pulse leaving the feet
    cast_b   the game's splash a fifth lower (370 to 630 Hz), doubled, with the thump
    cast_c   a gurgle: many small bubbles over 0.4 s rising in pitch, with the thump
    hit_a    the game's hit bloop (430 to 1570 Hz, side tone) twice, no echo: 0.18 s

A second set of hits, on the lines of the game's own. The
game's hits (hit, hit1, hit2, punch1, smash-hit2) were measured: nearly all of their energy is under 200 Hz (20 to 130 times what
is over 1.5 kHz). Each is a kick: a low tone falling from about 165 Hz to under 50 Hz in the first 30 ms, held loud for about
90 ms and gone by 0.3 s; over it a crack of bright noise that is loudest in the first 10 ms and over by 30 ms. So each hit here is
that kick and crack, dry, with the water mage's bloops over it and only the bloops echoed (once, faintly); then a compressor and a limiter
(Pedalboard). Then made mellower, and 0.5 s long (LENGTH):

    hit_m    the game's hit.wav shape (kick and crack) under its watersplash-hit (the bloop twice), its echo cut to one

hit_m is a step brighter than that (more crack, its top at 8 kHz, the bloops cut at 2 kHz, quicker starts, a little more
compression), and n to r are hit_m with one thing changed:

    hit_n    brighter still: the bloops cut at 2.6 kHz, more crack
    hit_o    heavier: a deeper kick (130 to 40 Hz) held longer
    hit_p    moving: the second bloop higher (500 to 1800 Hz) and to the right
    hit_q    wetter: a short splash of noise (500 Hz to 3 kHz) under it
    hit_r    snappier: shorter bloops, the second sooner (80 ms)

Then fewer or slower bubbles (the bloops), each hit_m with:

    hit_s    one bloop, not two, its peak at 0.97 (the one in assets/sounds)
    hit_t    both bloops slowed (180 ms each, 160 ms apart), a little quieter so they are no louder in all
    hit_u    both bloops at about half their level
    hit_v    one slow bloop: 200 ms, rising only to 1100 Hz
    hit_w    two slow, quieter bloops: 160 ms each, rising to 1200 Hz, 150 ms apart

Every variant goes to preview/ (44,100 Hz, stereo, 16-bit PCM, as the engine takes); the chosen ones (cast a, hit s) are written
to assets/sounds/wm_cast.wav and assets/sounds/wm_chain.wav (names of 15 characters at most: lib/sound).

    python make_water_mage_sounds.py [cast=a] [hit=s]
"""
import os
import struct
import sys

import numpy as np

try:
    import pedalboard  # Spotify's audio effects (pip install pedalboard): the hits' compressor and limiter
except ImportError:
    sys.exit("make_water_mage_sounds.py needs pedalboard: python -m pip install pedalboard")

HERE = os.path.dirname(os.path.abspath(__file__))
PREVIEW = os.path.join(HERE, "preview")
ASSETS = os.path.join(HERE, "..", "assets", "sounds")
RATE = 44100
PEAK = 0.9                 # the loudest sample after the mix; the script's volume does the rest

# the game's echo, measured: from side to side every 180 ms, each about half the one before
ECHO_MS, ECHO_FEEDBACK, ECHO_TAPS = 180, 0.5, 5


def seconds(n): return np.arange(int(n * RATE)) / RATE


def tri(phase):
    """A triangle wave of `phase` in turns: odd harmonics, the third at a ninth, as the game's bloops."""
    p = phase % 1.0
    return 4 * np.abs(p - 0.5) - 1


def sweep(f0, f1, dur, curve=1.0):
    """The phase (turns) of a tone going from f0 to f1 Hz over `dur` s; curve > 1 rises late, < 1 rises early."""
    t = seconds(dur)
    f = f0 + (f1 - f0) * (t / dur) ** curve
    return np.cumsum(f) / RATE


def env(n, attack_ms=2.0, decay=4.0):
    """Fast attack, then an exponential fall reaching exp(-decay) at the end."""
    t = np.arange(n) / RATE
    a = np.minimum(1.0, t / (attack_ms / 1000))
    return a * np.exp(-decay * t / (n / RATE))


def bloop(f0, f1, dur=0.085, side=0.0, curve=1.0, decay=1.3, attack_ms=1.5):
    """One bloop: a triangle rising from f0 to f1 Hz; `side` adds a tone 150 Hz under it at that level (the hit's)."""
    ph = sweep(f0, f1, dur, curve)
    x = tri(ph)
    if side:
        ph2 = sweep(f0 - 150, f1 - 150, dur, curve)
        x = x + side * np.sin(2 * np.pi * ph2)
    return x * env(len(x), attack_ms, decay)


def low_pass(x, hz):
    """A one-pole low pass at `hz`, run twice (12 dB an octave): takes the edge off the triangle's harmonics."""
    a = 1 - np.exp(-2 * np.pi * hz / RATE)
    for _ in range(2):
        y = np.empty_like(x)
        acc = 0.0
        for i, v in enumerate(x):
            acc += a * (v - acc)
            y[i] = acc
        x = y
    return x


def thump(f0=95, f1=50, dur=0.18, level=1.0):
    """A low sine falling in pitch: the weight of deep water."""
    return level * np.sin(2 * np.pi * sweep(f0, f1, dur, 0.6)) * env(int(dur * RATE), 3, 5)


def noise_band(dur, lo, hi, rng, swell=0.35):
    """Water rush: noise kept between lo and hi Hz, swelling in over `swell` of its length and fading out."""
    n = int(dur * RATE)
    spec = np.fft.rfft(rng.standard_normal(n))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    spec[(fr < lo) | (fr > hi)] = 0
    x = np.fft.irfft(spec, n)
    x /= np.abs(x).max() + 1e-9
    t = np.arange(n) / n
    shape = np.where(t < swell, t / swell, np.exp(-4 * (t - swell) / (1 - swell)))
    return x * shape


def place(track, sound, at_s, pan=0.0, level=1.0):
    """Adds `sound` into the stereo `track` at `at_s` seconds; pan -1 left to 1 right."""
    i = int(at_s * RATE)
    j = min(len(track), i + len(sound))
    left, right = np.sqrt((1 - pan) / 2), np.sqrt((1 + pan) / 2)
    track[i:j, 0] += sound[:j - i] * left * level
    track[i:j, 1] += sound[:j - i] * right * level


def echo(track, ms=ECHO_MS, feedback=ECHO_FEEDBACK, taps=ECHO_TAPS):
    """The game's echo: copies every `ms`, each `feedback` of the last, swapping sides."""
    out = track.copy()
    d = int(ms / 1000 * RATE)
    tap = track.copy()
    for k in range(1, taps + 1):
        tap = tap[:, ::-1] * feedback
        if k * d >= len(out):
            break
        out[k * d:] += tap[:len(out) - k * d]
    return out


def finish(track, fade_ms=40, peak=PEAK):
    n = int(fade_ms / 1000 * RATE)
    track[-n:] *= np.linspace(1, 0, n)[:, None]
    track -= track.mean(axis=0)
    return track * (peak / (np.abs(track).max() + 1e-9))


def blank(dur): return np.zeros((int(dur * RATE), 2))


def kick(f0=165, f1=45, drop_ms=35, hold_ms=90, dur=0.32, attack_ms=1.0):
    """The game's hits' low body: a sine falling from f0 to f1 Hz over `drop_ms`, then held at f1, flat for `hold_ms` and
    falling away by `dur` s."""
    t = seconds(dur)
    drop = drop_ms / 1000
    f = np.where(t < drop, f1 + (f0 - f1) * np.exp(-4 * t / drop), f1)
    x = np.sin(2 * np.pi * np.cumsum(f) / RATE)
    hold = hold_ms / 1000
    a = np.minimum(1.0, t / (attack_ms / 1000)) * np.where(t < hold, 1.0, np.exp(-7 * (t - hold) / (dur - hold)))
    return x * a


def crack(rng, ms=25, top=20000):
    """The game's hits' bright top: noise from 1.5 kHz to `top`, loudest at once and gone in `ms`."""
    n = int(ms / 1000 * RATE)
    spec = np.fft.rfft(rng.standard_normal(n))
    fr = np.fft.rfftfreq(n, 1 / RATE)
    spec[(fr < 1500) | (fr > top)] = 0
    x = np.fft.irfft(spec, n)
    x /= np.abs(x).max() + 1e-9
    return x * np.exp(-6 * np.arange(n) / n)


def strike(tones, dry, gain_db=10.0, fade_ms=60, ratio=4.0, peak=0.9):
    """A hit: the echoed bloops `tones` with the dry kick and crack `dry` (both stereo tracks of one length), through a compressor
    whose 6 ms attack lets the onset through and a limiter holding the peaks (Pedalboard)."""
    track = dry + tones
    board = pedalboard.Pedalboard([
        pedalboard.HighpassFilter(cutoff_frequency_hz=25),
        pedalboard.Compressor(threshold_db=-20, ratio=ratio, attack_ms=6, release_ms=120),
        pedalboard.Gain(gain_db=gain_db),
        pedalboard.Limiter(threshold_db=-1.0, release_ms=60),
    ])
    track = track / (np.abs(track).max() + 1e-9) * 0.5
    track = board(np.ascontiguousarray(track.T, dtype=np.float32), RATE).T.astype(np.float64)
    return finish(track, fade_ms=fade_ms, peak=peak)


# ---- the casts ---------------------------------------------------------------------------------------------------------------

def cast_a(rng):
    tr = blank(1.25)
    place(tr, thump(), 0.0, level=0.9)
    place(tr, noise_band(0.45, 250, 1100, rng), 0.0, level=0.1)
    for k, (f0, f1) in enumerate([(280, 560), (330, 660), (390, 780)]):
        place(tr, low_pass(bloop(f0, f1, 0.09, attack_ms=8), 1200), 0.07 * k, pan=(-0.3, 0.0, 0.3)[k], level=0.8 - 0.12 * k)
    return finish(echo(tr, feedback=0.4), peak=0.7)


def cast_b(rng):
    tr = blank(1.1)
    place(tr, thump(85, 48, 0.2), 0.0, level=0.8)
    place(tr, bloop(373, 627, 0.085), 0.0, level=0.9)
    place(tr, bloop(373, 627, 0.085), 0.088, level=0.55)
    return finish(echo(tr))


def cast_c(rng):
    tr = blank(1.2)
    place(tr, thump(), 0.0, level=0.8)
    t = 0.0
    while t < 0.4:
        f0 = 300 + 900 * t + rng.uniform(-60, 60)
        place(tr, bloop(f0, f0 * rng.uniform(1.5, 1.9), rng.uniform(0.02, 0.04), decay=2.5), t, pan=rng.uniform(-0.6, 0.6),
              level=0.7 * (1 - t))
        t += rng.uniform(0.018, 0.04)
    return finish(echo(tr, feedback=0.4))


# ---- the hits ----------------------------------------------------------------------------------------------------------------

def hit_a(rng):
    tr = blank(0.18)
    place(tr, bloop(430, 1570, 0.085, side=0.15), 0.0, level=0.8)
    place(tr, bloop(430, 1570, 0.085, side=0.15), 0.088, pan=0.25, level=0.45)
    return finish(tr, fade_ms=8)


# Every hit from hit_m on is 0.5 s, and its bloops have one echo at a quarter of their level.
LENGTH = 0.5
FAINT = dict(ms=150, feedback=0.25, taps=1)
BRIGHTER = dict(gain_db=8.5, ratio=3.5, fade_ms=80, peak=0.82)


def m_family(rng, crack_level=0.3, crack_top=8000, cut=2000, kick_args=(150, 45, 40, 100, 0.38), first=(430, 1570),
             second=(430, 1570), second_pan=0.0, splash=0.0, bloop_s=0.11, gap=0.1, levels=(0.55, 0.35), peak=None):
    """hit_m and its variations: the kick and crack dry, the hit bloop twice over them, the second `gap` s after the first
    (`second` None: once). A longer `bloop_s`, or a narrower `first`, is a slower rise."""
    tones, dry = blank(LENGTH), blank(LENGTH)
    place(dry, kick(*kick_args, attack_ms=2), 0.0, level=1.0)
    place(dry, crack(rng, 30, crack_top), 0.0, level=crack_level)
    if splash:
        place(dry, noise_band(0.08, 500, 3000, rng, swell=0.15), 0.005, level=splash)
    place(tones, low_pass(bloop(first[0], first[1], bloop_s, side=0.15, attack_ms=2.5), cut), 0.0, level=levels[0])
    if second:
        place(tones, low_pass(bloop(second[0], second[1], bloop_s, side=0.15, attack_ms=2.5), cut), gap, pan=second_pan,
              level=levels[1])
    return strike(echo(tones, **FAINT), dry, **dict(BRIGHTER, peak=peak or BRIGHTER["peak"]))


def hit_m(rng): return m_family(rng)
def hit_n(rng): return m_family(rng, crack_level=0.4, crack_top=10000, cut=2600)
def hit_o(rng): return m_family(rng, kick_args=(130, 40, 45, 130, 0.42))
def hit_p(rng): return m_family(rng, second=(500, 1800), second_pan=0.4)
def hit_q(rng): return m_family(rng, splash=0.18)
def hit_r(rng): return m_family(rng, bloop_s=0.085, gap=0.08)


# Fewer or slower bubbles (the bloops).
def hit_s(rng): return m_family(rng, second=None, peak=0.97)    # the one in assets/sounds: 1.4 dB louder
def hit_t(rng): return m_family(rng, bloop_s=0.18, gap=0.16, levels=(0.38, 0.24))
def hit_u(rng): return m_family(rng, levels=(0.3, 0.18))
def hit_v(rng): return m_family(rng, first=(430, 1100), second=None, bloop_s=0.2, levels=(0.45, 0))
def hit_w(rng): return m_family(rng, first=(430, 1200), second=(430, 1200), bloop_s=0.16, gap=0.15, levels=(0.3, 0.18))


VARIANTS = {"cast": {"a": cast_a, "b": cast_b, "c": cast_c}, "hit": {"a": hit_a, "m": hit_m, "n": hit_n, "o": hit_o, "p": hit_p, "q": hit_q, "r": hit_r, "s": hit_s, "t": hit_t, "u": hit_u,
                                                               "v": hit_v, "w": hit_w}}
TARGETS = {"cast": "wm_cast.wav", "hit": "wm_chain.wav"}


def write_wav(path, track):
    pcm = (np.clip(track, -1, 1) * 32767).astype("<i2").tobytes()
    head = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    head += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16)
    head += b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as f:
        f.write(head + pcm)


def main(args):
    picks = {"cast": "a", "hit": "s"}       # the variants in assets/sounds
    for a in args:
        kind, _, letter = a.partition("=")
        if kind not in VARIANTS or letter not in VARIANTS[kind]:
            sys.exit("usage: make_water_mage_sounds.py [cast=a|b|c] [hit=a|m to w]")
        picks[kind] = letter
    os.makedirs(PREVIEW, exist_ok=True)
    made = {}
    for kind, table in VARIANTS.items():
        for letter, build in table.items():
            track = build(np.random.RandomState(19))
            write_wav(os.path.join(PREVIEW, "wm_%s_%s.wav" % (kind, letter)), track)
            made[(kind, letter)] = track
    for kind, letter in picks.items():
        write_wav(os.path.join(ASSETS, TARGETS[kind]), made[(kind, letter)])
        print("%s: variant %s -> assets/sounds/%s" % (kind, letter, TARGETS[kind]))


if __name__ == "__main__":
    main(sys.argv[1:])
