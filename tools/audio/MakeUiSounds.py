"""UIの仮の効果音(assets/sounds/ui/*.wav)を合成する。音は仮で、差し替え前提。
使い方: python tools/audio/MakeUiSounds.py
"""
import math
import struct
import wave
from pathlib import Path

RATE = 44100
OUT = Path(__file__).resolve().parents[2] / "assets" / "sounds" / "ui"


def tone(notes, decay=18.0, gain=0.5):
    """notes: (開始秒, 周波数Hz, 長さ秒)の列。減衰する正弦波を足し合わせる。"""
    total = max(start + length for start, _, length in notes)
    samples = [0.0] * int(RATE * total)
    for start, freq, length in notes:
        first = int(RATE * start)
        for i in range(int(RATE * length)):
            t = i / RATE
            attack = min(1.0, i / (RATE * 0.003))
            samples[first + i] += math.sin(2 * math.pi * freq * t) * math.exp(-decay * t) * attack
    peak = max(1e-9, max(abs(s) for s in samples))
    return [s / peak * gain for s in samples]


def noise_burst(length, decay, gain, seed=1):
    state = seed
    samples = []
    for i in range(int(RATE * length)):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        samples.append(((state / 0x7FFFFFFF) * 2 - 1) * math.exp(-decay * i / RATE) * gain)
    return samples


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / name), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767)) for s in samples))


write("click.wav", tone([(0.0, 1200, 0.05)], decay=70, gain=0.35))
write("confirm.wav", tone([(0.0, 660, 0.12), (0.07, 990, 0.18)], decay=14, gain=0.45))
write("train_place.wav", tone([(0.0, 220, 0.2), (0.0, 330, 0.2)], decay=16, gain=0.5))
write("train_remove.wav", tone([(0.0, 330, 0.18), (0.08, 196, 0.22)], decay=14, gain=0.45))
shutter = noise_burst(0.03, 120, 0.6, 3) + [0.0] * int(RATE * 0.04) + noise_burst(0.05, 80, 0.5, 7)
write("shutter.wav", shutter)
