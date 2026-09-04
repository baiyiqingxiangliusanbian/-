"""Generate a small, non-8-bit audio prototype pack for AscendSpire.

These files are intentionally replaceable placeholders for the later AI/recorded
asset pass. They use layered envelopes, filtered-ish noise, inharmonic partials,
and looping musical beds so the runtime has real WAV assets to exercise.
"""

from __future__ import annotations

import math
import random
import wave
from pathlib import Path


RATE = 48_000
TAU = math.tau
ROOT = Path(__file__).resolve().parents[2]
SFX_DIR = ROOT / "Content" / "Audio" / "SFX"
MUSIC_DIR = ROOT / "Content" / "Audio" / "Music"


def clamp(v: float, lo: float = -1.0, hi: float = 1.0) -> float:
    return max(lo, min(hi, v))


def env(t: float, length: float, attack: float = 0.008, decay: float = 5.0) -> float:
    if t < 0.0 or t > length:
        return 0.0
    a = min(1.0, t / max(0.0001, attack))
    return a * math.exp(-decay * t)


def exp_sweep(t: float, f0: float, f1: float, length: float) -> float:
    p = max(0.0, min(1.0, t / max(0.0001, length)))
    freq = f0 * ((f1 / f0) ** p)
    return math.sin(TAU * freq * t)


def pluck(t: float, freq: float, length: float, brightness: float = 1.0) -> float:
    if t < 0.0 or t > length:
        return 0.0
    e = math.exp(-4.8 * t / max(0.001, length))
    return e * (
        0.62 * math.sin(TAU * freq * t)
        + 0.24 * math.sin(TAU * freq * 2.01 * t) * brightness
        + 0.11 * math.sin(TAU * freq * 3.97 * t) * brightness
    )


def noise(rng: random.Random, state: list[float]) -> float:
    raw = rng.uniform(-1.0, 1.0)
    state[0] = state[0] * 0.86 + raw * 0.14
    return state[0]


def render_sfx(name: str, length: float, seed: int) -> list[float]:
    rng = random.Random(seed)
    state = [0.0]
    out: list[float] = []
    for i in range(int(length * RATE)):
        t = i / RATE
        n = noise(rng, state)
        hi = rng.uniform(-1.0, 1.0) - n
        s = 0.0

        if name in {"ui_hover", "ui_confirm", "ui_back", "target_lock", "reward", "gold", "map_node"}:
            root = {"ui_hover": 880, "ui_confirm": 660, "ui_back": 440,
                    "target_lock": 1060, "reward": 740, "gold": 980,
                    "map_node": 520}[name]
            s = pluck(t, root, length, 1.0) * 0.52
            if name in {"ui_confirm", "reward", "gold"}:
                s += pluck(t - 0.045, root * 1.25, length, 0.9) * 0.42
        elif name in {"card_pick", "card_drag", "card_play", "card_invalid", "end_turn"}:
            if name == "card_invalid":
                s = (exp_sweep(t, 380, 160, length) * 0.45 + n * 0.12) * math.exp(-5.2 * t)
            else:
                click = math.exp(-42.0 * t) * math.sin(TAU * 1550 * t)
                paper = n * math.exp(-18.0 * t)
                body = math.sin(TAU * (110 if name == "card_play" else 180) * t) * math.exp(-24.0 * t)
                s = click * (0.28 if name == "card_drag" else 0.40) + paper * 0.42 + body * 0.30
                if name == "end_turn":
                    s += n * math.exp(-4.0 * t) * 0.32 + math.sin(TAU * 92 * t) * math.exp(-6.0 * t) * 0.18
        elif name in {"sword_slash", "sword_heavy", "sword_flurry", "sword_wave"}:
            if name == "sword_heavy":
                impact = max(0.0, t - 0.22)
                s = n * math.exp(-4.0 * t) * 0.28
                s += exp_sweep(t, 180, 58, 0.28) * 0.25
                s += math.sin(TAU * 62 * impact) * math.exp(-14 * impact) * 0.70
                s += pluck(impact, 280, 0.55, 1.0) * 0.32
            elif name == "sword_flurry":
                for strike in range(6):
                    local = t - strike * 0.072
                    if local >= 0:
                        e = math.exp(-33 * local)
                        s += exp_sweep(local, 2500 + strike * 130, 220, 0.16) * e * 0.22
                        s += (n - 0.4 * hi) * e * 0.13
                s += math.sin(TAU * 112 * max(0, t - 0.43)) * math.exp(-21 * max(0, t - 0.43)) * 0.28
            else:
                sweep_len = 0.50 if name == "sword_wave" else 0.32
                s = exp_sweep(t, 2100 if name == "sword_slash" else 1350, 95, sweep_len) * math.exp(-7.2 * t) * 0.62
                s += hi * math.exp(-21 * t) * 0.20
                s += math.sin(TAU * 126 * max(0, t - 0.055)) * math.exp(-34 * max(0, t - 0.055)) * 0.28
        elif name in {"thunder_crack", "fire_burst", "poison_hiss"}:
            if name == "thunder_crack":
                s = hi * math.exp(-48 * t) * 0.88 + n * math.exp(-5 * t) * 0.28
                s += math.sin(TAU * 70 * t) * math.exp(-5.5 * t) * 0.36
            elif name == "fire_burst":
                s = n * math.exp(-3.2 * t) * 0.40 + hi * math.exp(-12 * t) * 0.18
                s += math.sin(TAU * 74 * t) * math.exp(-4.5 * t) * 0.34
                s += math.sin(TAU * 143 * t) * math.exp(-8.5 * t) * 0.18
            else:
                s = n * math.exp(-3.6 * t) * 0.48
                s += exp_sweep(t, 540, 90, length) * math.exp(-5.4 * t) * 0.27
                s += math.sin(TAU * 73 * t) * 0.12
        elif name in {"ward_raise", "block", "heal_chime", "spirit_chime", "power_surge", "talisman_cast", "seal_stamp", "curse_whisper"}:
            roots = {"ward_raise": 430, "block": 380, "heal_chime": 620,
                     "spirit_chime": 720, "power_surge": 350, "talisman_cast": 810,
                     "seal_stamp": 510, "curse_whisper": 290}
            root = roots[name]
            if name == "curse_whisper":
                s = exp_sweep(t, 330, 55, length) * math.exp(-4.2 * t) * 0.44 + n * 0.20 * math.exp(-2.5 * t)
            else:
                s = pluck(t, root, length, 1.1) * 0.56
                s += pluck(t - 0.055, root * 1.5, length, 0.9) * 0.35
                if name in {"ward_raise", "block"}:
                    s += math.sin(TAU * 1160 * t) * math.exp(-15 * t) * 0.12
        elif name in {"impact_hit", "player_hit", "enemy_death", "victory_stinger", "defeat_stinger", "breakthrough"}:
            if name == "player_hit":
                s = math.sin(TAU * 84 * t) * math.exp(-9 * t) * 0.58 + n * math.exp(-16 * t) * 0.42
            elif name == "impact_hit":
                s = math.sin(TAU * 105 * t) * math.exp(-18 * t) * 0.60 + hi * math.exp(-30 * t) * 0.26
            elif name == "enemy_death":
                s = math.sin(TAU * 180 * t) * math.exp(-4.5 * t) * 0.32 + exp_sweep(t, 740, 92, length) * math.exp(-4.0 * t) * 0.40
            elif name == "defeat_stinger":
                s = pluck(t, 220, length, 0.8) * 0.55 + pluck(t - 0.18, 165, length, 0.8) * 0.40
            else:
                s = pluck(t, 440, length, 1.0) * 0.40 + pluck(t - 0.13, 660, length, 1.0) * 0.36
                s += pluck(t - 0.26, 880 if name != "breakthrough" else 990, length, 1.0) * 0.32
                if name == "breakthrough":
                    s += math.sin(TAU * 58 * t) * math.exp(-2.5 * t) * 0.18
        elif name in {"draw", "discard", "reshuffle", "potion", "enemy_turn", "combat_enter", "forge_start", "forge_success"}:
            paper = n * math.exp(-7.0 * t) * 0.38
            s = paper + pluck(t, 520 if name != "potion" else 920, length, 1.0) * 0.38
            if name == "reshuffle":
                s += n * math.exp(-2.0 * t) * 0.22 + math.sin(TAU * 120 * t) * math.exp(-4 * t) * 0.16
            if name == "forge_start":
                s += exp_sweep(t, 180, 560, length) * math.exp(-2.8 * t) * 0.20
            if name == "forge_success":
                s += pluck(t - 0.10, 780, length, 1.0) * 0.32
        else:
            s = pluck(t, 560, length, 1.0) * 0.45 + n * math.exp(-8 * t) * 0.12

        fade = min(1.0, t / 0.008) * min(1.0, max(0.0, length - t) / 0.018)
        out.append(clamp(math.tanh(s * 1.18) * fade))
    return out


def write_wav(path: Path, samples: list[float], channels: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = max(0.001, max(abs(s) for s in samples))
    gain = 0.88 / peak
    with wave.open(str(path), "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(RATE)
        frames = bytearray()
        for sample in samples:
            value = int(clamp(sample * gain) * 32767)
            frames += int(value).to_bytes(2, "little", signed=True)
        f.writeframes(frames)


def render_music(name: str, root: float, intensity: float, mode: str) -> list[float]:
    length = 16.0
    total = int(length * RATE)
    beat = 0.5  # 120 BPM, eight bars
    scale = [1.0, 1.12246, 1.25992, 1.49831, 1.68179, 2.0]
    if mode == "dark":
        scale = [1.0, 1.05946, 1.18921, 1.33484, 1.49831, 1.7818]
    events = []
    rng = random.Random(int(root * 10) + int(intensity * 100))
    for index in range(32):
        if index % 2 == 0 or intensity > 0.6:
            events.append((index * beat, root * scale[(index // 2 + rng.randrange(0, 3)) % len(scale)]))

    stereo: list[float] = []
    for i in range(total):
        t = i / RATE
        left = right = 0.0
        # A soft, slowly moving harmonic bed; unlike a square-wave chiptune it has
        # inharmonic partials and a continuous release over the loop.
        bed = math.sin(TAU * root * t) * 0.07 + math.sin(TAU * root * 1.503 * t) * 0.045
        bed += math.sin(TAU * root * 0.501 * t) * 0.08
        bed *= 0.70 + 0.30 * math.sin(TAU * t / length)
        left += bed * (0.85 + 0.08 * math.sin(TAU * t / length))
        right += bed * (0.85 - 0.08 * math.sin(TAU * t / length))
        for start, freq in events:
            local = t - start
            if 0.0 <= local < 1.15:
                hit = pluck(local, freq, 1.15, 0.85) * (0.10 + intensity * 0.075)
                pan = math.sin(start * 0.37)
                left += hit * (0.72 - pan * 0.20)
                right += hit * (0.72 + pan * 0.20)
        # Low drum pulse and a restrained metallic tick define game-state intensity.
        phase = t / beat
        frac = phase - math.floor(phase)
        kick = math.sin(TAU * (58 - 18 * min(1.0, frac)) * (frac * beat)) * math.exp(-16 * frac * beat)
        left += kick * 0.075 * intensity
        right += kick * 0.075 * intensity
        if int(phase * 2) % 4 == 1:
            tick = math.sin(TAU * 1450 * (frac * beat)) * math.exp(-28 * frac * beat) * 0.025 * intensity
            left += tick
            right += tick * 0.8
        fade = min(1.0, t / 0.045) * min(1.0, (length - t) / 0.045)
        stereo.extend([clamp(math.tanh(left * 1.15) * fade), clamp(math.tanh(right * 1.15) * fade)])
    return stereo


def main() -> None:
    sfx_specs = {
        "ui_hover": 0.18, "ui_confirm": 0.30, "ui_back": 0.28, "ui_deny": 0.28,
        "target_lock": 0.34, "card_pick": 0.22, "card_drag": 0.18, "card_play": 0.34,
        "card_invalid": 0.30, "end_turn": 0.42, "sword_slash": 0.34, "sword_heavy": 0.72,
        "sword_flurry": 0.78, "sword_wave": 0.55, "thunder_crack": 0.62, "fire_burst": 0.52,
        "poison_hiss": 0.55, "ward_raise": 0.45, "block": 0.34, "heal_chime": 0.52,
        "spirit_chime": 0.48, "power_surge": 0.60, "talisman_cast": 0.48, "seal_stamp": 0.40,
        "curse_whisper": 0.60, "impact_hit": 0.30, "player_hit": 0.42, "enemy_death": 0.70,
        "draw": 0.34, "discard": 0.34, "reshuffle": 0.72, "potion": 0.48, "enemy_turn": 0.42,
        "combat_enter": 0.65, "reward": 0.75, "gold": 0.32, "map_node": 0.30,
        "forge_start": 0.58, "forge_success": 0.80, "victory_stinger": 0.95,
        "defeat_stinger": 0.95, "breakthrough": 1.20,
    }
    for index, (name, length) in enumerate(sfx_specs.items()):
        write_wav(SFX_DIR / f"{name}.wav", render_sfx(name, length, 20260831 + index * 17))

    music_specs = {
        "bgm_title": (220.0, 0.18, "bright"),
        "bgm_map": (196.0, 0.28, "bright"),
        "bgm_narrative": (174.0, 0.20, "dark"),
        "bgm_shop": (246.0, 0.24, "bright"),
        "bgm_rest": (164.0, 0.12, "bright"),
        "bgm_combat": (146.8, 0.52, "dark"),
        "bgm_combat_elite": (130.8, 0.74, "dark"),
        "bgm_combat_boss": (110.0, 0.94, "dark"),
    }
    for name, (root, intensity, mode) in music_specs.items():
        write_wav(MUSIC_DIR / f"{name}.wav", render_music(name, root, intensity, mode), channels=2)


if __name__ == "__main__":
    main()
