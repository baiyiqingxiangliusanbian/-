"""Generate short loopable scene beds with Stable Audio Open Small."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from einops import rearrange
from stable_audio_tools import get_pretrained_model
from stable_audio_tools.inference.generation import generate_diffusion_cond


BGM_PROMPTS = {
    "bgm_title": "instrumental Chinese xianxia fantasy title theme, guqin and xiao bamboo flute, warm cinematic strings, jade bell accents, hopeful and spacious, elegant modern game soundtrack, 72 BPM, no vocals, no lyrics, no drums, loopable",
    "bgm_map": "instrumental wandering mountain map music for a Chinese xianxia roguelike, guqin, xiao flute, soft hand percussion, airy strings, curious and adventurous, calm forward motion, 84 BPM, no vocals, no lyrics, loopable",
    "bgm_narrative": "instrumental intimate xianxia narrative underscore, sparse guqin, breathy xiao flute, distant bowed strings, mystical and reflective, restrained tension, 68 BPM, no vocals, no lyrics, loopable",
    "bgm_shop": "instrumental whimsical fantasy shop theme, plucked guzheng, light wooden percussion, jade bells, warm playful chamber strings, charming polished modern game soundtrack, 96 BPM, no vocals, no lyrics, loopable",
    "bgm_rest": "instrumental serene cultivation rest theme, slow guqin, soft xiao flute, gentle ambient strings, floating jade chimes, peaceful meditation after battle, 58 BPM, no vocals, no lyrics, loopable",
    "bgm_combat": "instrumental cinematic Chinese fantasy card battle music, tense guqin ostinato, low taiko and wooden percussion, sharp xiao flute accents, driving modern game soundtrack, 122 BPM, no vocals, no lyrics, loopable",
    "bgm_combat_elite": "instrumental elite encounter music for a dark xianxia roguelike, aggressive guqin patterns, deep war drums, metallic ritual percussion, ominous low strings, escalating but controlled, 132 BPM, no vocals, no lyrics, loopable",
    "bgm_combat_boss": "instrumental xianxia boss battle theme, ominous ritual drums, dark guqin, piercing xiao flute, massive low strings, ancient mountain demon atmosphere, intense cinematic modern game soundtrack, 108 BPM, no vocals, no lyrics, loopable",
}


def select_device() -> str:
    if torch.backends.mps.is_available():
        return "mps"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def make_loopable(source: np.ndarray, crossfade_seconds: float = 0.55) -> np.ndarray:
    if source.ndim == 1:
        source = source[:, None]
    length = source.shape[0]
    fade = min(int(crossfade_seconds * 48_000), length // 4)
    if fade >= 2:
        t = np.linspace(0.0, 1.0, fade, dtype=np.float32)[:, None]
        source = source.copy()
        source[:fade] = source[:fade] * (1.0 - t) + source[-fade:] * t
        source[-1] = source[0]
    peak = float(np.max(np.abs(source)))
    if peak > 0.0:
        source = np.clip(source / peak * 0.72, -1.0, 1.0)
    return source


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*", default=None)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--steps", type=int, default=8)
    parser.add_argument("--seed", type=int, default=20260831)
    args = parser.parse_args()

    names = args.only if args.only else list(BGM_PROMPTS)
    unknown = [name for name in names if name not in BGM_PROMPTS]
    if unknown:
        raise SystemExit(f"Unknown BGM ids: {', '.join(unknown)}")
    device = select_device()
    print(f"Loading Stable Audio Open Small on {device}...")
    model, model_config = get_pretrained_model("stabilityai/stable-audio-open-small")
    model = model.to(device)
    sample_rate = int(model_config["sample_rate"])
    sample_size = int(model_config["sample_size"])
    print(f"Model sample rate={sample_rate}, sample size={sample_size}, count={len(names)}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(names):
        path = args.output_dir / f"{name}.wav"
        if path.exists() and path.stat().st_size > 4096:
            print(f"[{index + 1}/{len(names)}] skip {name} (exists)")
            continue
        print(f"[{index + 1}/{len(names)}] generating {name}")
        conditioning = [{"prompt": BGM_PROMPTS[name], "seconds_total": args.seconds}]
        with torch.no_grad():
            output = generate_diffusion_cond(
                model,
                steps=args.steps,
                cfg_scale=7.0,
                conditioning=conditioning,
                sample_size=sample_size,
                sampler_type="pingpong",
                device=device,
                seed=args.seed + index * 97,
            )
        output = rearrange(output, "b d n -> d (b n)")
        audio = output.detach().to(torch.float32).cpu().numpy().T
        # The model may return the configured maximum length; use the requested
        # loop length and resample to the game's 48 kHz convention.
        if sample_rate != 48_000:
            import torchaudio

            tensor = torch.from_numpy(audio.T)
            tensor = torchaudio.functional.resample(tensor, sample_rate, 48_000)
            audio = tensor.numpy().T
        target = int(args.seconds * 48_000)
        if audio.shape[0] < target:
            audio = np.pad(audio, ((0, target - audio.shape[0]), (0, 0)))
        audio = audio[:target]
        audio = make_loopable(audio)
        sf.write(str(path), audio, 48_000, subtype="PCM_16", format="WAV")
        print(f"  wrote {path}")
        if device == "mps":
            torch.mps.empty_cache()


if __name__ == "__main__":
    main()
