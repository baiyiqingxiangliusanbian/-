"""Clean generated audio for real-time playback.

The model outputs are valid PCM, but short effects can end on a non-zero sample
and the music files do not have a mathematically continuous loop seam. This
pass removes DC offset, adds short natural attack/release ramps, and builds a
crossfaded loop for each music state without changing the public asset names.
"""

from __future__ import annotations

import math
import shutil
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "Content" / "Audio"
STAGING = Path(__file__).resolve().parent / "staging" / "cleaned_audio"
BACKUP = Path(__file__).resolve().parent / "staging" / "pre_cleanup_audio"
RATE = 48_000


def read_wave(path: Path) -> tuple[int, int, list[int]]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        rate = source.getframerate()
        frames = source.getnframes()
        values = list(struct.unpack("<" + "h" * (frames * channels), source.readframes(frames)))
    if rate != RATE:
        raise RuntimeError(f"{path} is {rate} Hz, expected {RATE} Hz")
    return channels, rate, values


def write_wave(path: Path, channels: int, rate: int, values: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as target:
        target.setnchannels(channels)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(struct.pack("<" + "h" * len(values), *values))


def remove_dc(values: list[int], channels: int) -> list[float]:
    means = [
        sum(values[channel::channels]) / max(1, len(values[channel::channels]))
        for channel in range(channels)
    ]
    return [float(value) - means[index % channels] for index, value in enumerate(values)]


def normalize(values: list[float], peak: float = 0.90) -> list[int]:
    source_peak = max(1.0, max(abs(value) for value in values))
    gain = peak * 32767.0 / source_peak
    return [max(-32768, min(32767, round(value * gain))) for value in values]


def clean_sfx(source: Path, target: Path) -> tuple[int, int]:
    channels, rate, source_values = read_wave(source)
    values = remove_dc(source_values, channels)
    frames = len(values) // channels
    attack = min(int(rate * 0.006), frames // 4)
    release = min(int(rate * min(0.15, max(0.045, frames / rate * 0.15))), frames // 3)
    for frame in range(attack):
        amount = (frame + 1) / max(1, attack)
        for channel in range(channels):
            values[frame * channels + channel] *= amount
    for frame in range(release):
        amount = (release - frame - 1) / max(1, release)
        index = (frames - release + frame) * channels
        for channel in range(channels):
            values[index + channel] *= amount
    write_wave(target, channels, rate, normalize(values))
    return frames, release


def make_seamless_loop(source: Path, target: Path) -> tuple[int, int, int]:
    channels, rate, source_values = read_wave(source)
    values = remove_dc(source_values, channels)
    frames = len(values) // channels
    # Keep the musical phrase intact.  The previous 0.85s overlap was long
    # enough to blur a downbeat in a short model clip; the authored long loops
    # already include wrapped tails, so a compact overlap is sufficient for a
    # click-free handoff while preserving the melody.
    overlap = min(int(rate * 0.42), frames // 4)
    if frames <= overlap * 2:
        raise RuntimeError(f"{source} is too short for a loop crossfade")

    output: list[float] = []
    for frame in range(overlap):
        theta = (frame / max(1, overlap - 1)) * (math.pi * 0.5)
        tail_gain = math.cos(theta)
        head_gain = math.sin(theta)
        tail_index = (frames - overlap + frame) * channels
        head_index = frame * channels
        for channel in range(channels):
            output.append(
                values[tail_index + channel] * tail_gain
                + values[head_index + channel] * head_gain
            )
    output.extend(values[overlap * channels : (frames - overlap) * channels])
    write_wave(target, channels, rate, normalize(output, peak=0.84))
    return frames, len(output) // channels, overlap


def main() -> None:
    if STAGING.exists():
        shutil.rmtree(STAGING)
    STAGING.mkdir(parents=True, exist_ok=True)
    BACKUP.mkdir(parents=True, exist_ok=True)

    for category in ("SFX", "Music"):
        for source in sorted((CONTENT / category).glob("*.wav")):
            backup = BACKUP / category / source.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(source, backup)

            target = STAGING / category / source.name
            if category == "SFX":
                frames, release = clean_sfx(source, target)
                print(f"SFX {source.name}: {frames / RATE:.3f}s, release={release / RATE:.3f}s")
            else:
                original, result, overlap = make_seamless_loop(source, target)
                print(
                    f"BGM {source.name}: {original / RATE:.3f}s -> {result / RATE:.3f}s, "
                    f"crossfade={overlap / RATE:.3f}s"
                )


if __name__ == "__main__":
    main()
