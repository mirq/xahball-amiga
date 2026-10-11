#!/usr/bin/env python3
"""Generates 8-bit unsigned PCM WAV sound effects for XAHBall.

All samples are synthesized from scratch (no external assets, no licensing
concerns). Output goes to game/res/sfx/*.wav and is converted to ACE .sfx
by audio_conv at build time.
"""
import math
import os
import random
import struct
import wave

SR = 8363  # sample rate, classic Amiga-friendly
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "game", "res", "sfx")


def write_wav(name, samples):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name + ".wav")
    # Scale float layers to the full 8-bit range BEFORE quantizing: the raw
    # floats live in about [-1, 1] (layer sums may exceed it), and writing
    # them unscaled produced a +-1 LSB square wave which audio_conv's
    # NORMALIZE then blew up into a full-scale square buzz.
    peak = max((abs(s) for s in samples), default=0.0) or 1.0
    data = bytes(max(0, min(255, int(round(s / peak * 127)) + 128)) for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(SR)
        w.writeframes(data)
    print(f"{path}: {len(samples)} samples ({len(samples) / SR:.2f}s)")


def env_exp(i, n, k=5.0):
    """Exponential decay envelope."""
    return math.exp(-k * i / n)


def tone(freq, n, amp=0.8, decay_k=5.0, slide=1.0):
    out = []
    phase = 0.0
    for i in range(n):
        f = freq * (slide ** (i / n))
        phase += 2 * math.pi * f / SR
        out.append(amp * env_exp(i, n, decay_k) * math.sin(phase))
    return out


def noise(n, amp=0.5, decay_k=3.0):
    rnd = random.Random(42)
    out = []
    last = 0.0
    for i in range(n):
        # cheap lowpassed noise
        last = 0.6 * last + 0.4 * (rnd.random() * 2 - 1)
        out.append(amp * env_exp(i, n, decay_k) * last)
    return out


def mix(*layers):
    n = max(len(l) for l in layers)
    return [sum(l[i] if i < len(l) else 0.0 for l in layers) for i in range(n)]


def silence(secs):
    return [0.0] * int(SR * secs)


# --- menu move: short soft blip
n = int(SR * 0.035)
write_wav("move", tone(760, n, amp=0.45, decay_k=6))

# --- menu confirm: two quick rising tones
a = tone(620, int(SR * 0.05), amp=0.5, decay_k=4)
b = tone(930, int(SR * 0.07), amp=0.5, decay_k=4)
write_wav("confirm", mix(a, silence(0.04), b))

# --- menu back: falling tone
a = tone(700, int(SR * 0.05), amp=0.5, decay_k=4)
b = tone(430, int(SR * 0.07), amp=0.5, decay_k=4)
write_wav("back", mix(a, silence(0.04), b))

# --- kick: low thump with click attack
n = int(SR * 0.14)
thump = tone(105, n, amp=1.0, decay_k=9, slide=0.45)
click = noise(int(SR * 0.012), amp=0.5, decay_k=12)
write_wav("kick", mix(thump, click))

# --- bounce: mid blip, wall hit
n = int(SR * 0.06)
write_wav("bounce", mix(tone(240, n, amp=0.85, decay_k=9, slide=0.7)))

# --- referee whistle: ~2.2kHz with 30Hz trill + breath noise
n = int(SR * 0.55)
rnd = random.Random(7)
out = []
phase = 0.0
for i in range(n):
    trill = 0.75 + 0.25 * math.sin(2 * math.pi * 31 * i / SR)
    f = 2250 + 60 * math.sin(2 * math.pi * 6 * i / SR)
    phase += 2 * math.pi * f / SR
    s = 0.7 * trill * math.sin(phase)
    s += 0.12 * trill * (rnd.random() * 2 - 1)
    a = min(1.0, 8 * i / n) * min(1.0, 6 * (n - i) / n)
    out.append(0.9 * a * s)
write_wav("whistle", out)

# --- goal: whistle trio + crowd roar swell
wh = []
phase = 0.0
n_wh = int(SR * 0.30)
for i in range(n_wh):
    trill = 0.75 + 0.25 * math.sin(2 * math.pi * 31 * i / SR)
    phase += 2 * math.pi * 2350 / SR
    wh.append(0.5 * trill * math.sin(phase) * env_exp(i, n_wh, 2.5))
n_cr = int(SR * 1.4)
rnd = random.Random(99)
roar = []
last = 0.0
for i in range(n_cr):
    last = 0.94 * last + 0.06 * (rnd.random() * 2 - 1)
    swell = math.sin(math.pi * i / n_cr) ** 1.5
    roar.append(0.85 * swell * last)
# crowd = noise at two lowpass rates for texture
roar2 = []
last = 0.0
for i in range(n_cr):
    last = 0.8 * last + 0.2 * (rnd.random() * 2 - 1)
    roar2.append(0.35 * math.sin(math.pi * i / n_cr) ** 1.5 * last)
crowd = mix(roar, roar2, silence(0.10))
write_wav("goal", mix(crowd, wh))

print("done")
