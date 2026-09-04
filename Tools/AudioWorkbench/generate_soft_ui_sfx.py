"""Author the three close-listener UI sounds with a warm, low-mid mix.

These are deliberately generated offline and imported as ordinary SoundWaves.
The layer design uses a rounded low body, filtered tactile noise, and a long
smooth release. It avoids the brittle 1.5-8 kHz click that made selection
feedback tiring, while keeping enough attack to remain readable in a busy turn.
"""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path


RATE = 48_000
TAU = 2.0 * math.pi
OUTPUT_DIR = Path(__file__).resolve().parent / "staging" / "soft_ui_sfx"


def smoothstep(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def envelope(time: float, duration: float, attack: float, release: float) -> float:
    if time <= 0.0 or time >= duration:
        return 0.0
    attack_gain = smoothstep(time / max(attack, 1e-4)) if time < attack else 1.0
    release_start = duration - release
    release_gain = smoothstep((duration - time) / max(release, 1e-4)) if time > release_start else 1.0
    return attack_gain * release_gain


def filtered_noise(length: int, cutoff_hz: float, seed: int) -> list[float]:
    randomizer = random.Random(seed)
    alpha = 1.0 - math.exp(-TAU * cutoff_hz / RATE)
    # Three gentle one-pole stages give the texture a natural low-pass slope;
    # one stage alone leaves too much ultrasonic energy in a short UI sound.
    states = [0.0, 0.0, 0.0]
    result: list[float] = []
    for _ in range(length):
        value = randomizer.uniform(-1.0, 1.0)
        for stage in range(len(states)):
            states[stage] += alpha * (value - states[stage])
            value = states[stage]
        result.append(value)
    return result


def tone(time: float, frequency: float, decay: float, phase: float = 0.0) -> float:
    return math.sin(TAU * frequency * time + phase) * math.exp(-time / decay)


def write_wav(name: str, samples: list[float], target_peak: float = 0.82) -> None:
    peak = max((abs(sample) for sample in samples), default=0.0)
    if peak > 0.0:
        scale = target_peak / peak
        samples = [max(-1.0, min(1.0, sample * scale)) for sample in samples]
    pcm = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, sample)) * 32767.0)) for sample in samples)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUTPUT_DIR / f"{name}.wav"
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(pcm)
    rms = math.sqrt(sum(sample * sample for sample in samples) / max(1, len(samples)))
    print(f"wrote {path} duration={len(samples) / RATE:.3f}s peak={max(abs(s) for s in samples):.3f} rms={rms:.3f}")


def make_card_pick() -> list[float]:
    duration = 0.30
    length = int(duration * RATE)
    paper = filtered_noise(length, 1_050.0, 1701)
    samples: list[float] = []
    for index in range(length):
        time = index / RATE
        env = envelope(time, duration, 0.006, 0.125)
        body = 0.52 * tone(time, 142.0, 0.115)
        body += 0.18 * tone(time, 284.0, 0.090, 0.2)
        tactile = 0.12 * paper[index] * math.exp(-time / 0.075)
        samples.append((body + tactile) * env)
    return samples


def make_target_lock() -> list[float]:
    duration = 0.44
    length = int(duration * RATE)
    breath = filtered_noise(length, 720.0, 2702)
    samples: list[float] = []
    for index in range(length):
        time = index / RATE
        env = envelope(time, duration, 0.010, 0.165)
        pulse = 0.40 * tone(time, 118.0, 0.145)
        first = 0.22 * tone(time, 196.0, 0.190)
        second_time = max(0.0, time - 0.115)
        second = 0.25 * tone(second_time, 247.0, 0.165) if time >= 0.115 else 0.0
        air = 0.035 * breath[index] * math.exp(-time / 0.24)
        samples.append((pulse + first + second + air) * env)
    return samples


def make_card_drag() -> list[float]:
    duration = 0.24
    length = int(duration * RATE)
    friction = filtered_noise(length, 680.0, 3903)
    samples: list[float] = []
    for index in range(length):
        time = index / RATE
        env = envelope(time, duration, 0.008, 0.115)
        body = 0.20 * tone(time, 92.0, 0.13) + 0.08 * tone(time, 184.0, 0.10)
        texture = 0.095 * friction[index] * (0.70 + 0.30 * math.sin(TAU * 4.0 * time) ** 2)
        samples.append((body + texture) * env)
    return samples


def main() -> None:
    write_wav("card_pick", make_card_pick(), 0.78)
    write_wav("target_lock", make_target_lock(), 0.78)
    write_wav("card_drag", make_card_drag(), 0.62)


if __name__ == "__main__":
    main()
