"""Prepare a small CC0 tactile/gameplay SFX replacement pack.

The source pack is downloaded from Kenney and kept in staging so the source
license travels with the generated derivatives.  The resulting files are
mono, 48 kHz, PCM16 WAVs: short sounds do not need stereo width, and keeping
them mono reduces cooked memory and runtime decode work.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, resample_poly, sosfilt


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "Tools" / "AudioWorkbench" / "staging" / "cc0_sources" / "kenney"
OUTPUT_ROOT = ROOT / "Tools" / "AudioWorkbench" / "staging" / "cc0_processed" / "SFX"
MANIFEST_PATH = OUTPUT_ROOT.parent / "manifest.json"
TARGET_RATE = 48_000


# Keep the magical spell layer authored for AscendSpire.  These replacements
# target the tactile/UI/physical sounds that were most fatiguing or brittle.
SOURCES: dict[str, dict[str, object]] = {
    "card_pick": {"source": "kenney_casino-audio/Audio/card-slide-4.ogg", "lowpass": 3600, "peak": 0.62},
    "card_drag": {"source": "kenney_casino-audio/Audio/card-shove-1.ogg", "lowpass": 3200, "peak": 0.52},
    "card_play": {"source": "kenney_casino-audio/Audio/card-place-2.ogg", "lowpass": 4200, "peak": 0.66},
    "draw": {"source": "kenney_casino-audio/Audio/card-fan-1.ogg", "lowpass": 4800, "peak": 0.60},
    "discard": {"source": "kenney_casino-audio/Audio/card-shove-2.ogg", "lowpass": 3600, "peak": 0.50},
    "reshuffle": {"source": "kenney_casino-audio/Audio/card-shuffle.ogg", "lowpass": 5200, "peak": 0.48},
    "target_lock": {"source": "kenney_interface-sounds/Audio/bong_001.ogg", "lowpass": 3000, "peak": 0.56},
    "ui_confirm": {"source": "kenney_interface-sounds/Audio/confirmation_003.ogg", "lowpass": 4800, "peak": 0.52},
    "ui_back": {"source": "kenney_interface-sounds/Audio/back_002.ogg", "lowpass": 3600, "peak": 0.48},
    "ui_deny": {"source": "kenney_interface-sounds/Audio/error_003.ogg", "lowpass": 3400, "peak": 0.46},
    "map_node": {"source": "kenney_interface-sounds/Audio/pluck_002.ogg", "lowpass": 5000, "peak": 0.50},
    "potion": {"source": "kenney_interface-sounds/Audio/glass_003.ogg", "lowpass": 5000, "peak": 0.50},
    "impact_hit": {"source": "kenney_impact-sounds/Audio/impactPunch_medium_001.ogg", "lowpass": 5200, "peak": 0.64},
    "block": {"source": "kenney_impact-sounds/Audio/impactMetal_light_002.ogg", "lowpass": 5200, "peak": 0.56},
    "player_hit": {"source": "kenney_impact-sounds/Audio/impactSoft_medium_001.ogg", "lowpass": 3600, "peak": 0.58},
    "enemy_death": {"source": "kenney_impact-sounds/Audio/impactWood_heavy_002.ogg", "lowpass": 3000, "peak": 0.54},
    "sword_slash": {"source": "kenney_rpg-audio/Audio/knifeSlice2.ogg", "lowpass": 6500, "peak": 0.54},
    "sword_heavy": {"source": "kenney_impact-sounds/Audio/impactMetal_heavy_002.ogg", "lowpass": 5000, "peak": 0.60},
    "gold": {"source": "kenney_casino-audio/Audio/chips-collide-2.ogg", "lowpass": 6200, "peak": 0.48},
    "reward": {"source": "kenney_casino-audio/Audio/chips-stack-3.ogg", "lowpass": 5800, "peak": 0.50},
    "combat_enter": {"source": "kenney_impact-sounds/Audio/impactBell_heavy_002.ogg", "lowpass": 4200, "peak": 0.60},
    "enemy_turn": {"source": "kenney_impact-sounds/Audio/impactSoft_heavy_002.ogg", "lowpass": 2800, "peak": 0.44},
}


def resample(audio: np.ndarray, source_rate: int) -> np.ndarray:
    if source_rate == TARGET_RATE:
        return audio.astype(np.float32, copy=False)
    return resample_poly(audio, TARGET_RATE, source_rate).astype(np.float32, copy=False)


def low_pass(audio: np.ndarray, cutoff: int) -> np.ndarray:
    normalized = min(0.98, cutoff / (TARGET_RATE * 0.5))
    sos = butter(4, normalized, btype="lowpass", output="sos")
    return sosfilt(sos, audio).astype(np.float32, copy=False)


def trim_and_fade(audio: np.ndarray) -> np.ndarray:
    if audio.size == 0:
        return audio
    threshold = max(0.002, float(np.max(np.abs(audio))) * 0.008)
    active = np.flatnonzero(np.abs(audio) > threshold)
    if active.size:
        start = max(0, int(active[0]) - int(TARGET_RATE * 0.004))
        end = min(audio.size, int(active[-1]) + int(TARGET_RATE * 0.045))
        audio = audio[start:end]
    frames = audio.size
    attack = min(int(TARGET_RATE * 0.004), max(1, frames // 8))
    release = min(int(TARGET_RATE * 0.12), max(1, frames // 4))
    if attack:
        audio[:attack] *= np.linspace(0.0, 1.0, attack, endpoint=True, dtype=np.float32)
    if release:
        audio[-release:] *= np.linspace(1.0, 0.0, release, endpoint=True, dtype=np.float32)
    return audio


def prepare(name: str, spec: dict[str, object]) -> dict[str, object]:
    source = SOURCE_ROOT / str(spec["source"])
    audio, source_rate = sf.read(str(source), always_2d=True, dtype="float32")
    mono = audio.mean(axis=1)
    mono = resample(mono, int(source_rate))
    mono = low_pass(mono, int(spec["lowpass"]))
    mono = trim_and_fade(mono)
    peak = float(np.max(np.abs(mono))) if mono.size else 0.0
    target_peak = float(spec["peak"])
    if peak > 1e-8:
        mono = np.clip(mono / peak * target_peak, -1.0, 1.0)
    output = OUTPUT_ROOT / f"{name}.wav"
    output.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(output), mono, TARGET_RATE, subtype="PCM_16", format="WAV")
    return {
        "event": name,
        "source": str(source.relative_to(ROOT)),
        "output": str(output.relative_to(ROOT)),
        "license": "CC0 1.0 / Kenney",
        "sample_rate": TARGET_RATE,
        "channels": 1,
        "duration_seconds": round(len(mono) / TARGET_RATE, 4),
        "peak": round(float(np.max(np.abs(mono))) if mono.size else 0.0, 4),
        "lowpass_hz": int(spec["lowpass"]),
    }


def main() -> None:
    if OUTPUT_ROOT.exists():
        for old in OUTPUT_ROOT.glob("*.wav"):
            old.unlink()
    records = [prepare(name, SOURCES[name]) for name in sorted(SOURCES)]
    MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST_PATH.write_text(json.dumps({"source_license": "CC0 1.0 / Kenney", "assets": records}, ensure_ascii=False, indent=2) + "\n")
    for record in records:
        print(f"{record['event']}: {record['duration_seconds']:.3f}s peak={record['peak']:.3f}")
    print(f"wrote {len(records)} assets to {OUTPUT_ROOT}")


if __name__ == "__main__":
    main()
