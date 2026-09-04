"""Compose deterministic, bar-aligned AscendSpire music loops.

The first BGM pass was useful for testing the runtime path, but its short
model outputs made the loop boundary and musical identity too obvious.  This
composer keeps the musical material explicit: every scene has a motif, an
answer/variation, and a return to the opening phrase.  Notes are rendered
with wrap-around tails, so a loop repeats at a bar boundary without chopping
off a release or a reverb-like delay.

This is intentionally dependency-light and runs offline with numpy and
soundfile.  It is an authored fallback/finishing layer, not a runtime audio
system.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import numpy as np
import soundfile as sf


RATE = 48_000
TAU = math.tau


def midi_hz(note: float) -> float:
    return 440.0 * (2.0 ** ((note - 69.0) / 12.0))


def equal_power_pan(pan: float) -> tuple[float, float]:
    angle = (max(-1.0, min(1.0, pan)) + 1.0) * math.pi * 0.25
    return math.cos(angle), math.sin(angle)


def one_pole(signal: np.ndarray, cutoff: float) -> np.ndarray:
    """Small, stable low-pass used for soft percussion and breath noise."""
    alpha = 1.0 - math.exp(-TAU * cutoff / RATE)
    out = np.empty_like(signal)
    state = 0.0
    for index, sample in enumerate(signal):
        state += alpha * (float(sample) - state)
        out[index] = state
    return out


def high_pass(signal: np.ndarray, cutoff: float) -> np.ndarray:
    return signal - one_pole(signal, cutoff)


def adsr(length: int, attack: float, release: float, sustain: float = 1.0) -> np.ndarray:
    envelope = np.ones(length, dtype=np.float32) * sustain
    attack_frames = min(length, max(1, int(attack * RATE)))
    release_frames = min(max(1, int(release * RATE)), max(1, length - attack_frames))
    if attack_frames > 1:
        envelope[:attack_frames] = np.linspace(0.0, 1.0, attack_frames, endpoint=False)
    if release_frames > 1:
        envelope[-release_frames:] *= np.linspace(1.0, 0.0, release_frames)
    return envelope


def pluck(freq: float, duration: float, velocity: float) -> np.ndarray:
    length = max(32, int((duration + 0.36) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    phase = TAU * freq * t
    # Bright at the attack, then naturally darker as the string settles.
    brightness = np.exp(-t * 2.5)
    tone = (
        np.sin(phase)
        + (0.27 * brightness) * np.sin(phase * 2.0 + 0.07)
        + (0.10 * brightness) * np.sin(phase * 3.0 + 0.21)
        + (0.035 * brightness) * np.sin(phase * 5.0)
    )
    envelope = (1.0 - np.exp(-t / 0.004)) * np.exp(-t / max(0.16, duration * 0.62))
    return (tone * envelope * velocity * 0.38).astype(np.float32)


def lead(freq: float, duration: float, velocity: float, seed: int) -> np.ndarray:
    length = max(64, int((duration + 0.28) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    vibrato = 0.0032 * np.sin(TAU * 5.15 * t + seed * 0.13)
    phase = np.cumsum((TAU * freq * (1.0 + vibrato)) / RATE, dtype=np.float32)
    tone = (
        np.sin(phase)
        + 0.16 * np.sin(phase * 2.0 + 0.12)
        + 0.045 * np.sin(phase * 3.0 + 0.35)
    )
    breath = np.random.default_rng(seed).standard_normal(length).astype(np.float32)
    breath = one_pole(breath, 1250.0) * 0.020
    envelope = adsr(length, attack=0.065, release=0.22, sustain=1.0)
    return ((tone + breath) * envelope * velocity * 0.30).astype(np.float32)


def pad(freq: float, duration: float, velocity: float, seed: int) -> np.ndarray:
    length = max(128, int((duration + 0.95) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    detune = 0.0021 + (seed % 5) * 0.00025
    left_phase = TAU * freq * (1.0 - detune) * t
    right_phase = TAU * freq * (1.0 + detune) * t
    tone = 0.52 * np.sin(left_phase) + 0.52 * np.sin(right_phase)
    tone += 0.14 * np.sin(TAU * freq * 2.0 * t)
    tone += 0.055 * np.sin(TAU * freq * 3.0 * t)
    lfo = 0.92 + 0.08 * np.sin(TAU * 0.17 * t + seed)
    envelope = adsr(length, attack=0.42, release=0.78, sustain=1.0)
    return (tone * lfo * envelope * velocity * 0.105).astype(np.float32)


def bass(freq: float, duration: float, velocity: float) -> np.ndarray:
    length = max(32, int((duration + 0.20) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    phase = TAU * freq * t
    triangle = (2.0 / math.pi) * np.arcsin(np.sin(phase))
    tone = 0.78 * np.sin(phase) + 0.20 * triangle + 0.04 * np.sin(phase * 2.0)
    envelope = adsr(length, attack=0.014, release=0.16, sustain=0.84)
    return (tone * envelope * velocity * 0.30).astype(np.float32)


def pulse(freq: float, duration: float, velocity: float) -> np.ndarray:
    length = max(24, int((duration + 0.10) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    phase = (freq * t) % 1.0
    saw = (phase * 2.0) - 1.0
    # The two-pole approximation keeps the modern pulse present without a
    # brittle high-frequency edge.
    tone = one_pole(one_pole(saw.astype(np.float32), 1800.0), 1800.0)
    envelope = adsr(length, attack=0.006, release=0.075, sustain=0.70)
    return (tone * envelope * velocity * 0.13).astype(np.float32)


def bell(freq: float, duration: float, velocity: float) -> np.ndarray:
    length = max(32, int((duration + 1.1) * RATE))
    t = np.arange(length, dtype=np.float32) / RATE
    tone = (
        0.72 * np.sin(TAU * freq * t)
        + 0.24 * np.sin(TAU * freq * 2.01 * t + 0.2)
        + 0.10 * np.sin(TAU * freq * 3.96 * t + 0.6)
    )
    envelope = (1.0 - np.exp(-t / 0.012)) * np.exp(-t / max(0.32, duration))
    return (tone * envelope * velocity * 0.14).astype(np.float32)


def taiko(velocity: float, seed: int, bright: bool = False) -> np.ndarray:
    length = int(0.64 * RATE)
    t = np.arange(length, dtype=np.float32) / RATE
    start = 132.0 if bright else 98.0
    end = 58.0 if bright else 46.0
    pitch = start * ((end / start) ** np.minimum(t / 0.28, 1.0))
    phase = np.cumsum(TAU * pitch / RATE, dtype=np.float32)
    rng = np.random.default_rng(seed)
    noise = one_pole(rng.standard_normal(length).astype(np.float32), 750.0)
    body = np.sin(phase) * 0.90 + noise * 0.12
    envelope = (1.0 - np.exp(-t / 0.003)) * np.exp(-t / 0.22)
    return (body * envelope * velocity * 0.52).astype(np.float32)


def wood_click(velocity: float, seed: int) -> np.ndarray:
    length = int(0.16 * RATE)
    t = np.arange(length, dtype=np.float32) / RATE
    rng = np.random.default_rng(seed)
    noise = high_pass(rng.standard_normal(length).astype(np.float32), 800.0)
    tone = np.sin(TAU * 720.0 * t) * 0.42 + np.sin(TAU * 1180.0 * t) * 0.18
    envelope = (1.0 - np.exp(-t / 0.0015)) * np.exp(-t / 0.030)
    return ((noise * 0.22 + tone) * envelope * velocity * 0.24).astype(np.float32)


def shaker(velocity: float, seed: int) -> np.ndarray:
    length = int(0.095 * RATE)
    t = np.arange(length, dtype=np.float32) / RATE
    rng = np.random.default_rng(seed)
    noise = high_pass(rng.standard_normal(length).astype(np.float32), 3200.0)
    envelope = (1.0 - np.exp(-t / 0.001)) * np.exp(-t / 0.032)
    return (noise * envelope * velocity * 0.055).astype(np.float32)


def add_wrapped(mix: np.ndarray, voice: np.ndarray, start_frame: int, pan: float, gain: float = 1.0) -> None:
    """Add a voice to a cyclic buffer, including tails crossing the seam."""
    frames = mix.shape[0]
    start_frame %= frames
    left, right = equal_power_pan(pan)
    for offset in range(0, len(voice), frames):
        chunk = voice[offset : offset + frames]
        destination = (start_frame + offset) % frames
        first = min(len(chunk), frames - destination)
        mix[destination : destination + first, 0] += chunk[:first] * left * gain
        mix[destination : destination + first, 1] += chunk[:first] * right * gain
        if first < len(chunk):
            rest = len(chunk) - first
            mix[:rest, 0] += chunk[first:] * left * gain
            mix[:rest, 1] += chunk[first:] * right * gain


def degree_note(root: int, scale: list[int], degree: int, octave: int = 0) -> float:
    index = degree + octave * len(scale)
    octave_shift, scale_index = divmod(index, len(scale))
    return root + scale[scale_index] + octave_shift * 12


def event(mix: np.ndarray, bpm: float, beat: float, note: float, duration: float, velocity: float, kind: str, pan: float, seed: int) -> None:
    frame = int(round(beat * 60.0 / bpm * RATE))
    if kind == "pluck":
        voice = pluck(midi_hz(note), duration * 60.0 / bpm, velocity)
    elif kind == "lead":
        voice = lead(midi_hz(note), duration * 60.0 / bpm, velocity, seed)
    elif kind == "pad":
        voice = pad(midi_hz(note), duration * 60.0 / bpm, velocity, seed)
    elif kind == "bass":
        voice = bass(midi_hz(note), duration * 60.0 / bpm, velocity)
    elif kind == "pulse":
        voice = pulse(midi_hz(note), duration * 60.0 / bpm, velocity)
    elif kind == "bell":
        voice = bell(midi_hz(note), duration * 60.0 / bpm, velocity)
    elif kind == "taiko":
        voice = taiko(velocity, seed, bright=note > 0)
    elif kind == "wood":
        voice = wood_click(velocity, seed)
    elif kind == "shaker":
        voice = shaker(velocity, seed)
    else:
        raise ValueError(f"unknown voice kind: {kind}")
    add_wrapped(mix, voice, frame, pan, 1.0)


def phrase_events(
    mix: np.ndarray,
    bpm: float,
    root: int,
    scale: list[int],
    phrase: list[tuple[float, int, float, float]],
    start_bar: int,
    kind: str,
    octave: int,
    pan: float,
    seed: int,
    velocity_scale: float = 1.0,
) -> None:
    for repeat in range(start_bar, start_bar + 8):
        phrase_bar = repeat - start_bar
        for beat_in_bar, degree, duration, velocity in phrase[phrase_bar % len(phrase)]:
            note = degree_note(root, scale, degree, octave)
            event(
                mix,
                bpm,
                repeat * 4.0 + beat_in_bar,
                note,
                duration,
                velocity * velocity_scale,
                kind,
                pan + ((phrase_bar % 3) - 1) * 0.025,
                seed + repeat * 17 + degree,
            )


def chord_events(mix: np.ndarray, bpm: float, root: int, scale: list[int], bars: int, progression: list[list[int]], seed: int, intensity: float) -> None:
    for bar in range(bars):
        degrees = progression[bar % len(progression)]
        for index, degree in enumerate(degrees):
            note = degree_note(root, scale, degree, 0 if index == 0 else 1)
            event(mix, bpm, bar * 4.0, note, 3.92, 0.72 * intensity, "pad", (-0.18 + index * 0.18), seed + bar * 11 + index)
        bass_note = degree_note(root, scale, degrees[0], -1)
        event(mix, bpm, bar * 4.0, bass_note, 2.3, 0.70 * intensity, "bass", 0.0, seed + bar)
        if intensity > 0.62:
            event(mix, bpm, bar * 4.0 + 0.0, bass_note + 12, 0.34, 0.42 * intensity, "pulse", -0.08, seed + bar * 3)
            event(mix, bpm, bar * 4.0 + 1.5, bass_note + 12, 0.30, 0.28 * intensity, "pulse", 0.08, seed + bar * 3 + 1)
            event(mix, bpm, bar * 4.0 + 2.0, bass_note + 12, 0.34, 0.42 * intensity, "pulse", -0.08, seed + bar * 3 + 2)
            event(mix, bpm, bar * 4.0 + 3.5, bass_note + 12, 0.30, 0.27 * intensity, "pulse", 0.08, seed + bar * 3 + 3)


def drums(mix: np.ndarray, bpm: float, bars: int, seed: int, intensity: float, boss: bool = False) -> None:
    for bar in range(bars):
        section = bar % 8
        # The downbeat and the late-beat pickup make the loop feel like a
        # phrase instead of a metronomic 8-bit click track.
        hits = [(0.0, 0.88), (2.0, 0.58)]
        if intensity > 0.70:
            hits.append((2.75, 0.35 if section not in (3, 7) else 0.55))
        if section in (3, 7):
            hits.append((3.5, 0.48))
        for hit, velocity in hits:
            event(mix, bpm, bar * 4.0 + hit, 1.0 if hit == 0 else 0.0, 0.1, velocity * intensity, "taiko", -0.05 if hit == 0 else 0.05, seed + bar * 23 + int(hit * 10))
        for beat in (1.0, 3.0):
            event(mix, bpm, bar * 4.0 + beat, 0.0, 0.05, 0.70 * intensity, "wood", 0.19 if beat == 1.0 else -0.19, seed + bar * 7 + int(beat))
        if intensity > 0.50:
            for eighth in range(8):
                if eighth % 2 == 1 or (section in (2, 6) and eighth in (2, 6)):
                    event(mix, bpm, bar * 4.0 + eighth * 0.5, 0.0, 0.03, 0.5 * intensity, "shaker", 0.30 if eighth % 2 else -0.30, seed + bar * 31 + eighth)
        if boss and section in (3, 7):
            event(mix, bpm, bar * 4.0 + 3.75, 1.0, 0.1, 0.55, "taiko", 0.10, seed + 900 + bar)


def add_delay(mix: np.ndarray, seconds: float, gain: float, pan_flip: bool = True) -> None:
    delay = int(seconds * RATE)
    if delay <= 0 or delay >= mix.shape[0]:
        return
    delayed = np.roll(mix, delay, axis=0) * gain
    if pan_flip:
        delayed = delayed[:, ::-1]
    mix += delayed


def crossfade_loop(mix: np.ndarray, seconds: float) -> np.ndarray:
    """Move the cyclic seam into a short equal-power musical overlap."""
    overlap = min(int(seconds * RATE), mix.shape[0] // 4)
    if overlap < 2:
        return mix
    theta = np.linspace(0.0, math.pi * 0.5, overlap, dtype=np.float32)[:, None]
    blend = mix[-overlap:] * np.cos(theta) + mix[:overlap] * np.sin(theta)
    # The overlap starts exactly where the retained middle ends, and ends
    # exactly where the retained head continues.  This avoids a waveform
    # jump while keeping the downbeat from sounding like a hard cut.
    return np.concatenate((blend, mix[overlap:-overlap]), axis=0)


def render_track(name: str, bpm: float, bars: int, root: int, intensity: float, seed: int, kind: str) -> np.ndarray:
    scale = [0, 3, 5, 7, 10]  # minor pentatonic: clear xianxia colour, easy to remember
    total_beats = bars * 4
    frames = int(round(total_beats * 60.0 / bpm * RATE))
    mix = np.zeros((frames, 2), dtype=np.float32)

    progression = [
        [0, 2, 4], [3, 0, 2], [4, 1, 3], [2, 4, 0],
        [0, 2, 4], [3, 0, 2], [1, 3, 0], [0, 2, 4],
    ]
    chord_events(mix, bpm, root, scale, bars, progression, seed, intensity)

    combat_phrase_a = [
        [(0.0, 0, 0.75, 0.95), (1.0, 2, 0.45, 0.72), (1.5, 3, 0.45, 0.78), (2.0, 4, 0.90, 0.90), (3.25, 3, 0.42, 0.62), (3.75, 2, 0.22, 0.55)],
        [(0.0, 1, 0.55, 0.72), (0.75, 2, 0.45, 0.76), (1.5, 3, 0.45, 0.80), (2.0, 4, 0.80, 0.90), (3.0, 2, 0.45, 0.68), (3.6, 0, 0.28, 0.60)],
        [(0.0, 3, 0.65, 0.78), (0.85, 4, 0.50, 0.88), (1.75, 3, 0.45, 0.72), (2.5, 2, 0.45, 0.68), (3.25, 1, 0.55, 0.70)],
        [(0.0, 0, 1.10, 0.90), (1.75, 2, 0.42, 0.67), (2.25, 3, 0.42, 0.72), (3.0, 0, 0.70, 0.82)],
    ]
    combat_phrase_b = [
        [(0.0, 4, 0.70, 0.92), (1.0, 3, 0.50, 0.78), (1.75, 2, 0.45, 0.70), (2.5, 1, 0.42, 0.68), (3.0, 2, 0.55, 0.74)],
        [(0.0, 0, 0.50, 0.78), (0.75, 1, 0.45, 0.70), (1.5, 2, 0.45, 0.74), (2.25, 3, 0.62, 0.82), (3.25, 4, 0.55, 0.86)],
        [(0.0, 3, 0.55, 0.76), (0.75, 4, 0.48, 0.84), (1.5, 0, 0.48, 0.72), (2.25, 2, 0.45, 0.68), (3.0, 3, 0.62, 0.80)],
        [(0.0, 4, 0.95, 0.94), (1.5, 3, 0.42, 0.70), (2.25, 2, 0.42, 0.68), (3.0, 0, 0.82, 0.86)],
    ]
    gentle_phrase = [
        [(0.0, 0, 1.0, 0.86), (1.5, 2, 0.62, 0.68), (2.5, 3, 0.62, 0.72), (3.35, 2, 0.42, 0.58)],
        [(0.0, 1, 0.82, 0.72), (1.25, 2, 0.55, 0.66), (2.0, 4, 0.95, 0.82), (3.25, 3, 0.45, 0.58)],
        [(0.0, 3, 0.86, 0.78), (1.35, 4, 0.62, 0.76), (2.25, 2, 0.68, 0.62), (3.35, 1, 0.42, 0.58)],
        [(0.0, 0, 1.38, 0.84), (1.9, 2, 0.55, 0.62), (2.7, 3, 0.50, 0.64), (3.35, 0, 0.45, 0.60)],
    ]
    bright_phrase = [
        [(0.0, 0, 0.42, 0.82), (0.5, 2, 0.42, 0.70), (1.0, 3, 0.42, 0.78), (1.5, 4, 0.62, 0.88), (2.5, 3, 0.42, 0.70), (3.0, 2, 0.42, 0.66), (3.5, 0, 0.32, 0.58)],
        [(0.0, 1, 0.42, 0.70), (0.5, 2, 0.42, 0.74), (1.0, 4, 0.62, 0.86), (2.0, 3, 0.42, 0.72), (2.5, 2, 0.42, 0.64), (3.0, 1, 0.42, 0.62), (3.5, 0, 0.32, 0.58)],
        [(0.0, 3, 0.42, 0.78), (0.5, 4, 0.42, 0.84), (1.0, 3, 0.42, 0.72), (1.5, 2, 0.42, 0.68), (2.0, 1, 0.62, 0.66), (3.0, 2, 0.42, 0.70), (3.5, 0, 0.32, 0.56)],
        [(0.0, 0, 0.95, 0.86), (1.5, 2, 0.45, 0.66), (2.0, 3, 0.45, 0.70), (2.5, 4, 0.45, 0.78), (3.25, 0, 0.45, 0.66)],
    ]

    if kind in {"combat", "elite", "boss"}:
        phrase_events(mix, bpm, root, scale, combat_phrase_a, 0, "lead", 2, 0.08, seed + 10, 1.02)
        phrase_events(mix, bpm, root, scale, combat_phrase_b, 8, "lead", 2, -0.08, seed + 30, 1.05)
        phrase_events(mix, bpm, root, scale, combat_phrase_a, 16, "lead", 2, 0.08, seed + 50, 0.93)
        if bars >= 32:
            phrase_events(mix, bpm, root, scale, combat_phrase_b, 24, "lead", 2, -0.08, seed + 70, 0.96)
        phrase_events(mix, bpm, root, scale, combat_phrase_a, 0, "pluck", 1, -0.22, seed + 80, 0.58)
        phrase_events(mix, bpm, root, scale, combat_phrase_a, 16, "pluck", 1, 0.22, seed + 90, 0.44)
        drums(mix, bpm, bars, seed + 100, intensity, boss=kind == "boss")
    elif kind in {"shop", "map"}:
        phrase_events(mix, bpm, root, scale, bright_phrase, 0, "lead", 2, 0.07, seed + 10, 0.86)
        phrase_events(mix, bpm, root, scale, bright_phrase, 8, "lead", 2, -0.08, seed + 30, 0.76)
        phrase_events(mix, bpm, root, scale, bright_phrase, 16, "lead", 2, 0.08, seed + 50, 0.88)
        phrase_events(mix, bpm, root, scale, bright_phrase, 0, "pluck", 1, -0.18, seed + 60, 0.56)
        if kind == "shop":
            for bar in range(bars):
                if bar % 2 == 0:
                    event(mix, bpm, bar * 4.0 + 3.5, 24 + root, 0.1, 0.72, "bell", 0.28, seed + 500 + bar)
        drums(mix, bpm, bars, seed + 100, intensity * 0.72)
    else:
        phrase_events(mix, bpm, root, scale, gentle_phrase, 0, "lead", 2, 0.06, seed + 10, 0.82)
        phrase_events(mix, bpm, root, scale, gentle_phrase, 8, "lead", 2, -0.07, seed + 30, 0.70)
        if bars >= 16:
            phrase_events(mix, bpm, root, scale, gentle_phrase, 8, "pluck", 1, -0.18, seed + 50, 0.38)
        for bar in range(0, bars, 2):
            event(mix, bpm, bar * 4.0 + 3.25, root + 24 + (bar % 4), 0.15, 0.55 if kind == "title" else 0.35, "bell", 0.20 if bar % 4 else -0.20, seed + 700 + bar)

    # A short wrapped delay is part of the musical tail, so it is present at
    # the beginning of the next pass instead of being cut at the asset end.
    add_delay(mix, 0.19 if intensity > 0.7 else 0.27, 0.13 if intensity > 0.7 else 0.10)
    add_delay(mix, 0.43 if intensity > 0.7 else 0.52, 0.055)

    # Gentle bus saturation and headroom for overlapping in-game SFX.
    mix = np.tanh(mix * (1.08 if intensity > 0.7 else 0.92))
    mix -= np.mean(mix, axis=0, keepdims=True)
    mix = crossfade_loop(mix, 0.42 if intensity > 0.7 else 0.30)
    peak = float(np.max(np.abs(mix)))
    if peak > 0.0:
        mix *= 0.78 / peak
    return mix.astype(np.float32)


TRACKS = {
    "bgm_title": (88.0, 24, 50, 0.48, "title"),
    "bgm_map": (104.0, 24, 50, 0.58, "map"),
    "bgm_narrative": (76.0, 16, 50, 0.38, "narrative"),
    "bgm_shop": (112.0, 24, 50, 0.56, "shop"),
    "bgm_rest": (68.0, 16, 50, 0.30, "rest"),
    "bgm_combat": (112.0, 32, 50, 0.92, "combat"),
    "bgm_combat_elite": (118.0, 24, 50, 0.86, "elite"),
    "bgm_combat_boss": (108.0, 24, 50, 0.98, "boss"),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--seed", type=int, default=20260902)
    args = parser.parse_args()
    names = args.only if args.only else list(TRACKS)
    unknown = [name for name in names if name not in TRACKS]
    if unknown:
        raise SystemExit(f"Unknown BGM ids: {', '.join(unknown)}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(names):
        bpm, bars, root, intensity, kind = TRACKS[name]
        output = args.output_dir / f"{name}.wav"
        audio = render_track(name, bpm, bars, root, intensity, args.seed + index * 101, kind)
        sf.write(str(output), audio, RATE, subtype="PCM_16", format="WAV")
        seconds = audio.shape[0] / RATE
        print(f"{name}: {seconds:.3f}s, {bars} bars @ {bpm:g} BPM -> {output}")


if __name__ == "__main__":
    main()
