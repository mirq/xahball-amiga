#!/usr/bin/env python3
"""Generates the menu music MOD for XAHBall (ProTracker M.K. format).

All instruments are synthesized from scratch (no external assets). Output:
game/res/music/menu.mod - converted by mod_tool at build time into
data/menu.mod + data/menu.samplepack (chaosArena-style ptplayer pipeline).

MOD convention used here: every pitched instrument is rendered at SR=8363Hz
(= ptplayer reference period 424 for PAL), so a note of frequency f is
played with period P = 424 * F0 / f, where F0 is the instrument's rendered
fundamental. Periods stay inside the legal Paula range 113..856.
"""
import math
import os
import random
import struct
import wave

SR = 8363
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "game", "res", "music")
CLOCK = 3546895  # PAL
REF_PERIOD = 424  # CLOCK / SR


def synth_bass(f0, secs):
    n = int(SR * secs)
    out = []
    phase = 0.0
    for i in range(n):
        env = math.exp(-4.0 * i / n)
        phase += 2 * math.pi * f0 / SR
        # fundamental + soft octave: rounded synth-bass tone
        s = 0.7 * math.sin(phase) + 0.25 * math.sin(2 * phase) * env
        out.append(0.9 * env * s)
    return out


def synth_lead(f0, secs):
    n = int(SR * secs)
    out = []
    phase = 0.0
    rnd = random.Random(5)
    for i in range(n):
        env = min(1.0, 10 * i / n) * math.exp(-3.0 * i / n)
        vib = 1.0 + 0.004 * math.sin(2 * math.pi * 5.5 * i / SR)
        phase += 2 * math.pi * f0 * vib / SR
        s = 0.6 * math.sin(phase) + 0.2 * math.sin(3 * phase)
        s += 0.05 * (rnd.random() * 2 - 1)  # breath
        out.append(0.8 * env * s)
    return out


def synth_kick(secs):
    n = int(SR * secs)
    out = []
    phase = 0.0
    for i in range(n):
        f = 150 * (0.4 ** (3 * i / n))
        phase += 2 * math.pi * f / SR
        env = math.exp(-9 * i / n)
        out.append(0.95 * env * math.sin(phase))
    return out


def synth_hat(secs):
    n = int(SR * secs)
    rnd = random.Random(11)
    out = []
    last = 0.0
    for i in range(n):
        last = 0.5 * last + 0.5 * (rnd.random() * 2 - 1)
        out.append(0.5 * math.exp(-18 * i / n) * last)
    return out


def quantize(samples):
    # Signed 8-bit (MOD sample data is signed), peak-normalized, starting
    # and ending at zero level so one-shot tails don't click
    peak = max(abs(s) for s in samples) or 1.0
    peak = max(peak, 1e-9)
    out = [0, 0]  # leading zero word: ptplayer loops instruments cleanly
    out += [max(-127, min(127, int(round(s / peak * 120)))) for s in samples]
    out += [0, 0]
    if len(out) & 1:
        out.append(0)  # MOD lengths are word-based
    return bytes(b & 0xFF for b in out)


# (name, data, rendered fundamental F0 or None for unpitched)
INSTRUMENTS = [
    ("SYNTH-BASS", quantize(synth_bass(110.0, 0.55)), 110.0),  # 1
    ("SOFT-LEAD", quantize(synth_lead(440.0, 0.40)), 440.0),   # 2
    ("KICK", quantize(synth_kick(0.12)), None),                # 3
    ("HAT", quantize(synth_hat(0.035)), None),                 # 4
]

# Note frequencies used by the tune
NOTES = {
    "A1": 55.0, "C2": 65.4, "D2": 73.4, "E2": 82.4, "F2": 87.3, "G2": 98.0,
    "A2": 110.0, "B2": 123.5, "C3": 130.8, "D3": 146.8, "E3": 164.8,
    "F3": 174.6, "G3": 196.0, "A3": 220.0, "B3": 246.9, "C4": 261.6,
    "D4": 293.7, "E4": 329.6, "F4": 349.2, "G4": 392.0, "A4": 440.0,
    "B4": 493.9, "C5": 523.3, "D5": 587.3, "E5": 659.3, "G5": 784.0,
    "A5": 880.0,
}


def period_for(instr_idx, note):
    f0 = INSTRUMENTS[instr_idx][2]
    f = NOTES[note]
    p = round(REF_PERIOD * f0 / f)
    assert 113 <= p <= 856, f"period {p} out of legal range for {note}"
    return p


class Pattern:
    def __init__(self):
        # 4 channels x 64 rows x 4 bytes, zeroed
        self.rows = [[(0, 0, 0, 0)] * 4 for _ in range(64)]

    def note(self, row, ch, period, sample, effect=0x0, arg=0x00):
        self.rows[row][ch] = (period, sample, effect, arg)

    def data(self):
        out = bytearray()
        for row in self.rows:
            for (period, sample, effect, arg) in row:
                out.append(((sample & 0xF0)) | ((period >> 8) & 0x0F))
                out.append(period & 0xFF)
                out.append(((sample & 0x0F) << 4) | (effect & 0x0F))
                out.append(arg & 0xFF)
        return bytes(out)


def build_patterns():
    # 8-bar loop: Am - F - C - G, melodic minor pentatonic on top.
    # 1 row = 16th note at speed 6 (125 BPM PAL).
    roots = ["A2", "F2", "C3", "G2"] * 2
    fifths = ["E3", "C3", "G3", "D3"] * 2
    # Simple pentatonic-ish lead phrase, one bar each (row = start, 8 rows)
    lead_bars = [
        [("A4", 0), ("C5", 8), ("E5", 16), ("D5", 24), ("C5", 32), ("A4", 40)],
        [("A4", 0), ("C5", 8), ("F4", 20), ("E4", 28), ("C4", 40)],
        [("E4", 0), ("G4", 8), ("C5", 16), ("B4", 24), ("G4", 36), ("E4", 44)],
        [("D4", 0), ("G4", 8), ("B4", 16), ("D5", 28), ("B4", 40), ("G4", 48)],
    ] * 2
    patterns = []
    for bar in range(8):
        p = Pattern()
        if bar == 0:
            p.note(0, 0, 0, 0, 0xF, 0x06)  # set speed 6
        # Bass: driving eighths root/fifth (rows every 4 = every 8th note
        # twice per 4 rows would be 16ths - we want 8ths: every 8 rows)
        for beat in range(8):  # 8 eighth notes per bar
            row = beat * 8
            n = roots[bar] if beat % 4 != 3 else fifths[bar]
            p.note(row, 0, period_for(0, n), 1)
        # Kick: 1 and 3 of each half-bar
        for row in (0, 16, 32, 48):
            p.note(row, 2, REF_PERIOD, 3)
        # Hat: off-beats
        for row in (8, 24, 40, 56):
            p.note(row, 3, 424, 4)
        # Lead: from bar 5 (0-based 4) onward, first half only phrases
        if bar >= 4:
            for (n, row) in lead_bars[bar - 4]:
                p.note(row, 1, period_for(1, n), 2)
        patterns.append(p)
    return patterns


def write_mod():
    os.makedirs(OUT_DIR, exist_ok=True)
    patterns = build_patterns()
    out = bytearray()
    out += b"XAHBALL MENU THEME".ljust(20, b"\0")
    # 31 sample headers
    for i in range(31):
        if i < len(INSTRUMENTS):
            name, data, _ = INSTRUMENTS[i]
            length_words = len(data) // 2
            out += name.encode("ascii").ljust(22, b"\0")
            out += struct.pack(">H", length_words)
            out += bytes([0])       # finetune
            out += bytes([64])      # volume
            out += struct.pack(">H", 0)  # repeat offset
            out += struct.pack(">H", 1)  # repeat length 1 word = one-shot
        else:
            out += b"\0" * 30
    out += bytes([len(patterns)])  # arrangement length
    out += bytes([0])              # song end / restart pos -> loop to start
    arrangement = list(range(len(patterns))) + [0] * (128 - len(patterns))
    out += bytes(arrangement)
    out += b"M.K."
    for p in patterns:
        out += p.data()
    for (_, data, _) in INSTRUMENTS:
        assert len(data) % 2 == 0
        out += data
    path = os.path.join(OUT_DIR, "menu.mod")
    with open(path, "wb") as f:
        f.write(out)
    print(f"{path}: {len(out)} bytes, {len(patterns)} patterns, "
          f"samples {[len(d) for (_, d, _) in INSTRUMENTS]}")


if __name__ == "__main__":
    write_mod()
